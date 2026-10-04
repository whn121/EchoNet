#include "Common/Config.h"
#include "Logger/AsyncLogger.h"
#include "DB/RedisPool.h"
#include "Gateway/ServiceRegistry.h"
#include "Server/Server.h"
#include "Net/Connection.h"
#include "Reactor/Channel.h"
#include "Reactor/EventLoop.h"
#include "Protocol/MyProtocol.h"
#include "Net/Socket.h"
#include "ThreadPool/Task.h"

#include <iostream>
#include <memory>
#include <any>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <unordered_set>



// 待响应请求:记录哪个客户端在等哪个响应 让消息双方对应上不会收到别人的
struct PendingRequest
{
    std::weak_ptr<Connection> client_conn; // 客户端连接 (弱引用)
    uint32_t original_id; // 客户端原始requestId
    std::chrono::steady_clock::time_point created_at;
};


//网关状态,把要转发的打包到一起,方便,不会漏
struct GatewayState
{
    // 后端: 地址 + Connection 支持后来后端获得
    struct BackendEntry
    {
        std::string addr;   
        std::shared_ptr<Connection> conn;

        // 健康状态
        long long last_pong_ms{0}; // 最后收到heartbeat_resp的时间
        bool healthy{true};
        uint64_t request_count{0}; // 这个后端处理了多少请求
    };

    // 用于轮询
    std::vector<BackendEntry> backends;
    std::mutex backends_mutex;
    
    // 请求映射:newid->(客户端连接 + id)
    std::unordered_map<uint32_t, PendingRequest> pending;
    std::mutex pending_mutex;

    // 全局requestId 生成器
    std::atomic<uint32_t> next_new_id{1};

    // 轮询索引
    std::atomic<size_t> rr_index{0};

    // 所有后端的fd (用于区分消息时客户端来的还是后端来的)
    std::unordered_set<int> backend_fds;
    std::mutex backend_fds_mutex;

    // 选一个后端 (轮询)
    std::shared_ptr<Connection> pick_backend()
    {
        std::lock_guard<std::mutex> lock(backends_mutex);
        if (backends.empty()) return nullptr;

        // 跳过unhealthy, 最多尝试size次
        size_t n = backends.size();
        for (size_t i = 0; i < n; ++i)
        {
            size_t idx = rr_index++ % n;
            if (backends[idx].healthy)
            {
                backends[idx].request_count++;
                return backends[idx].conn;
            }
        }

        return nullptr; // 全部不健康
    }

    // 统计指标
    struct Metrics 
    {
        std::atomic<uint64_t> total_requests{0}; // 客户端请求总数
        std::atomic<uint64_t> forwarded{0}; // 成功转发数
        std::atomic<uint64_t> no_backend{0}; // 请求因没有健康后端的数量
        std::atomic<uint64_t> backend_responses{0}; // 后端相应数
        std::atomic<uint64_t> pending_timeouts{0}; // pending表超时数
        std::atomic<uint64_t> unknown_responses{0}; // 未知相应 (没人等的相应)
        std::atomic<uint64_t> dropped_client_gone{0}; // 客户端已经断开 (响应到了但客户端断了)
    };
    Metrics metrics;

    // 检查地址是否已经链接
    bool has_addr(const std::string& addr)
    {
        std::lock_guard<std::mutex> lock(backends_mutex);
        for (auto& e : backends) if (e.addr == addr) return true;
        return false;
    }
};


int main(int argc, char* argv[])
{
    Config::instance().parseArgs(argc, argv);

    // 1.启动服务发现
    auto redis_pool = std::make_shared<RedisPool>("127.0.0.1", 6379, 4);
    ServiceRegistry registry(redis_pool, "chat_service");
    registry.Start(2);

    LOG_INFO("[Gateway] service discovery started, watching chat_service");

    // 2.创建共享状态
    auto state = std::make_shared<GatewayState>();

    // 3.启动服务器，接收客户端连接
    Server server;

    // 3.1连接工厂：用 MyProtocol
    server.setcreator([](int afd, EventLoop* loop) -> std::shared_ptr<Connection>
    {
        auto channel  = std::make_unique<Channel>(afd);
        auto protocol = std::make_unique<MyProtocol>();
        return std::make_shared<Connection>(
            afd, std::move(channel), loop, std::move(protocol));
    });

    // 3.2业务处理器: 按值捕获state
    // 定义lambda
    auto business_handler = [state](std::shared_ptr<Connection> conn, const std::any& message)
    {
        try
        {
            auto msg = std::any_cast<MyMessage>(message);
            int fd = conn->getChannel()->getFd();

            // 判断消息来源 是后端为真
            bool is_backend = false; 
            {
                std::lock_guard<std::mutex> lock(state->backend_fds_mutex);
                is_backend = (state->backend_fds.count(fd) > 0);
            }
                
            // 是后端相应
            if (is_backend)
            {
                // 心跳响应体直接忽略, 并复活吧我的爱人!!!!
                if (msg.type_ == MyType::HEARTBEAT_RESP)
                {
                    // 更新对应后端的last_pong_ms
                    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>
                    (std::chrono::steady_clock::now().time_since_epoch()).count();

                    std::lock_guard<std::mutex> lock(state->backends_mutex);
                    for (auto& e : state->backends)
                    {
                        if (e.conn->getChannel()->getFd() == fd)
                        {
                            e.last_pong_ms = now_ms; // stroe 原子写入
                            e.healthy = true; // 恢复健康
                            break;
                        }
                    }
                    return;
                }

                // 后端响应 -> 查表找原客户端
                PendingRequest req;
                bool found = false;
                uint32_t new_id = msg.id_; 

                {
                    std::lock_guard<std::mutex> lock(state->pending_mutex);
                    auto it = state->pending.find(new_id);
                    if (it != state->pending.end())
                    {
                        req = it->second;
                        state->pending.erase(it);
                        found = true;
                    } 
                }

                if (!found) 
                {
                    state->metrics.unknown_responses.fetch_add(1);
                    LOG_WARN("[Gateway] backend response for unknown id=" + std::to_string(new_id));
                    return;
                }

                auto client = req.client_conn.lock(); // 判断conn是不是还活着吧weak->shared
                if (!client)
                {
                    state->metrics.dropped_client_gone.fetch_add(1);
                    LOG_WARN("[Gateway] client gone, drop response new_id=" + std::to_string(new_id));
                    return;
                }

                // 还原原始requestId
                msg.id_ = req.original_id;
                client->sendResponse(msg);

                state->metrics.backend_responses.fetch_add(1);
                LOG_INFO("[Gateway] returned: new_id=" + std::to_string(new_id) + " -> orig_id=" + std::to_string(req.original_id));
                return;
            }

            // 是客户端,处理
            // 1.选一个后端轮询
            auto backend = state->pick_backend();
            if (!backend)
            {
                state->metrics.no_backend.fetch_add(1);
                LOG_ERROR("[Gateway] no backend available");
                return;
            }

            // 2.生成新的requestid
            uint32_t new_id = state->next_new_id++; //原子加1;
            uint32_t original_id = msg.id_;

            // 3.记录映射
            {
                std::lock_guard<std::mutex> lock(state->pending_mutex);
                state->pending[new_id] = PendingRequest
                {
                    conn, //weak_ptr自动从shared_ptr转换 内部有接受shared_ptr的方法
                    original_id,
                    std::chrono::steady_clock::now()
                };
            }

            // 4.重写requestId
            msg.id_ = new_id;

            // 5.转发到后端
            state->metrics.total_requests.fetch_add(1);
            state->metrics.forwarded.fetch_add(1);

            LOG_INFO("[Gateway] forward: orig_id=" + std::to_string(original_id) + " -> new_id=" + std::to_string(new_id)
                        + " type=" + std::to_string(static_cast<int>(msg.type_)));

            backend->sendResponse(msg);

        }
        catch (const std::bad_any_cast& e)
        {
            LOG_ERROR("[Gateway] bad_any_cast: " + std::string(e.what()));
        }
    };

    // Server 用lambda定义（客户端连接）
    server.setBusinessHandler(business_handler);

    // 3.3关闭回调
    server.setCloseCallback([](int fd)
    {
        LOG_INFO("[Gateway] client closed: fd=" + std::to_string(fd));
    });

    LOG_INFO("[Gateway] listening on port " + std::to_string(Config::instance().port));

    // 4.连接所有后端
    // 拿主eventloop
    EventLoop* loop = server.getMainLoop();

    // 拿当前所有后端
    auto backends = registry.GetSnapshot();
    LOG_INFO("[Gateway] connecting to " + std::to_string(backends.size()) + " backends");

    // 连接所有后端
    for (const auto& inst : backends)
    {
        // 1.建立非阻塞fd tcp链接
        Socket sock;
        if (!sock.m_connect(inst.host, inst.port))
        {
            LOG_ERROR("[Gateway] connect to " + inst.address() + " failed");
            continue;
        }

        int fd = sock.release(); //交出fd所有权

        // 2.创建Connection
        auto channel  = std::make_unique<Channel>(fd);
        auto protocol = std::make_unique<MyProtocol>();
        auto conn = std::make_shared<Connection>(fd, std::move(channel), loop, std::move(protocol));

        // 3.绑定读写回调
        conn->init();
        conn->setBusinessHandler(business_handler); // 把之前应以的lambda传入 之前io提交做了
        // 关键：出站连接的 Task 也要能提交（否则后端响应没人处理）
        conn->setCallBack([](Task t) {
            t.run_();                                    // 直接在 IO 线程执行
            if (t.onComplete_) t.onComplete_();
        });

        // 4.设置关闭回调 
        conn->setcloseCallback([loop](int fd) {
            LOG_INFO("[Gateway] backend closed: fd=" + std::to_string(fd));
            loop->removeConnection(fd);
        });

        // 5.注册到epoll 
        loop->updateChannel(conn->getChannel(), EPOLLIN);

        {
            std::lock_guard<std::mutex> lock(state->backends_mutex);
            state->backends.push_back({inst.address(), conn});   // ← 存地址 + conn
        }

        {
            std::lock_guard<std::mutex> lock(state->backend_fds_mutex);
            state->backend_fds.insert(fd);
        }

        // 6.不调用updateConnection (便面超时给我踢了) 很关键
        LOG_INFO("[Gateway] connected to " + inst.address());
    }

    // 5.心跳线程：每 5 秒给每个后端发心跳 (同时用于健康检测)
    std::atomic<bool> heartbeat_stop{false};
    std::mutex hb_mutex;
    std::condition_variable hb_cv;

    std::thread hb_thread([state, &heartbeat_stop, &hb_mutex, &hb_cv]() 
    {
        while (true) 
        {
            {
                std::unique_lock<std::mutex> lock(hb_mutex);
                hb_cv.wait_for(lock, std::chrono::seconds(5), [&] { return heartbeat_stop.load(); });
                
                if (heartbeat_stop) break;
            }

            // 持锁拷贝一份，锁外遍历
            std::vector<std::shared_ptr<Connection>> snapshot;
            {
                std::lock_guard<std::mutex> lock(state->backends_mutex);
                for (auto& e : state->backends) snapshot.push_back(e.conn);
            }

            for (auto& conn : snapshot) 
            {
                MyMessage hb;
                hb.type_    = MyType::HEARTBEAT_REQ;
                hb.id_      = 0;
                hb.payload_ = "";
                conn->sendResponse(hb);
            }
            LOG_INFO("[Gateway] heartbeat sent to " + std::to_string(snapshot.size()) + " backends");
        }
    });

    // 6.健康检测线程: 每3秒扫一次,检测假死后端
    std::atomic<bool> health_stop {false};
    std::mutex health_mutex;
    std::condition_variable health_cv;

    std::thread health_thread ([state, &health_stop, &health_mutex, &health_cv]()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(health_mutex);
                health_cv.wait_for(lock, std::chrono::seconds(5), [&] { return health_stop.load(); });
                
                if (health_stop) break;
            }

            long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::steady_clock::now().time_since_epoch()).count();

            std::lock_guard<std::mutex> lock(state->backends_mutex);
            for (auto& e : state->backends)
            {
                long long last = e.last_pong_ms;
                if (last == 0) continue; // 刚启动还没pong, 跳过

                long long elapsed = now_ms - last;
                if (elapsed > 15000) // 15秒没pong
                {
                    if (e.healthy)
                    {
                        LOG_WARN("[Gateway] backend unhealthy: " + e.addr + " (no pong for " + std::to_string(elapsed / 1000) + "s)");
                    }
                    e.healthy = false;
                }
                else
                {
                    if (!e.healthy)
                    {
                        LOG_INFO("[Gateway] backend recovered: " + e.addr);
                        e.healthy = true;
                    }
                }
            }
        }
    });

    // 7.Metrics打印线程: 每30秒打印一次
    std::atomic<bool> metrics_stop{false};
    std::mutex metrics_mutex;
    std::condition_variable metrics_cv;

    std::thread metrics_thread([state, &metrics_stop, &metrics_mutex, &metrics_cv]()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(metrics_mutex);
                metrics_cv.wait_for(lock, std::chrono::seconds(5), [&] { return metrics_stop.load(); });
            
                if (metrics_stop) break;
            }

            auto& m = state->metrics;

            LOG_INFO("     Gateway Metrics     ");
            LOG_INFO("total_requests:      " + std::to_string(m.total_requests.load()));
            LOG_INFO("forwarded:           " + std::to_string(m.forwarded.load()));
            LOG_INFO("no_backend:          " + std::to_string(m.no_backend.load()));
            LOG_INFO("backend_responses:   " + std::to_string(m.backend_responses.load()));
            LOG_INFO("pending_timeouts:    " + std::to_string(m.pending_timeouts.load()));
            LOG_INFO("unknown_responses:   " + std::to_string(m.unknown_responses.load()));
            LOG_INFO("dropped_client_gone: " + std::to_string(m.dropped_client_gone.load()));

            size_t pending_size = 0;
            {
                std::lock_guard<std::mutex> lock(state->pending_mutex);
                pending_size = state->pending.size();
            }
            LOG_INFO("pending_size:        " + std::to_string(pending_size));

            LOG_INFO("backends: ");
            std::lock_guard<std::mutex> lock (state->backends_mutex);
            for (auto& e : state->backends)
            {
                LOG_INFO("  " + e.addr + "  healthy=" + (e.healthy ? "yes" : "no") + "  requests=" + std::to_string(e.request_count));
            }
        }
    });

    // 7.动态节点更新: 监控serviceRegistry变化
    std::atomic<bool> monitor_stop {false};
    std::mutex monitor_mutex;
    std::condition_variable monitor_cv;

    std::thread monitor_thread([state, loop, &registry, business_handler, &monitor_stop, &monitor_mutex,  &monitor_cv]()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(monitor_mutex);
                monitor_cv.wait_for(lock, std::chrono::seconds(5), [&] { return monitor_stop.load(); });
            
                if (monitor_stop) break;
            }

            auto instances = registry.GetSnapshot();

            // 期望的地址集合
            std::unordered_set<std::string> expected;
            for (auto& inst : instances) expected.insert(inst.address());

            // 移除消失的节点 (持锁找出, 锁外处理)
            std::vector<std::pair<std::string, std::shared_ptr<Connection>>> to_remove;
            {
                std::lock_guard<std::mutex> lock(state->backends_mutex);
                for (auto it = state->backends.begin(); it != state->backends.end(); )
                {
                    if (expected.count(it->addr) == 0)
                    {
                        to_remove.push_back({it->addr, it->conn});
                        it = state->backends.erase(it);
                    }
                    else ++it;
                }
            }
            for (auto& [addr, conn] : to_remove)
            {
                int fd = conn->getChannel()->getFd();
                {
                    std::lock_guard<std::mutex> lock(state->backend_fds_mutex);
                    state->backend_fds.erase(fd);
                }
                conn->close();
                LOG_INFO("[Gateway] removed backend: " + addr);
            }

            // 添加新节点
            for (auto& inst : instances)
            {
                if (state->has_addr(inst.address())) continue;

                // 在监控线程建立 TCP 链接 (阻塞connect 不卡loop)
                Socket sock;
                if (!sock.m_connect(inst.host, inst.port))
                {
                    LOG_ERROR("[Gateway] dynamic connect to " + inst.address() + " failed");
                    continue;
                }
                int fd = sock.release();

                // 投递到loop线程创建connection + 注册
                loop->runInLoop([state, loop, fd, addr = inst.address(), business_handler]()
                {
                    auto channel = std::make_unique<Channel> (fd);
                    auto protocol = std::make_unique<MyProtocol>();
                    auto conn = std::make_shared<Connection>(fd, std::move(channel), loop, std::move(protocol));

                    conn->init();
                    conn->setBusinessHandler(business_handler);
                    conn->setCallBack([](Task t)
                    {
                        t.run_();
                        if (t.onComplete_) t.onComplete_();
                    });
                    
                    loop->updateChannel(conn->getChannel(), EPOLLIN);

                    conn->setcloseCallback([loop](int fd) 
                    {
                        LOG_INFO("[Gateway] backend closed: fd=" + std::to_string(fd));
                        loop->removeConnection(fd);
                    });

                    {
                        std::lock_guard<std::mutex> lock(state->backends_mutex);
                        state->backends.push_back({addr, conn});
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->backend_fds_mutex);
                        state->backend_fds.insert(fd);
                    }

                    LOG_INFO("[Gateway] dynamically added backend: " + addr);
                });
            }
        }
    });

    // 8.超时清理线程：每 10 秒扫一次 pending，清掉超过 30 秒的
    std::atomic<bool> cleanup_stop{false};
    std::mutex cleanup_mutex;
    std::condition_variable cleanup_cv;

    std::thread cleanup_thread([state, &cleanup_stop, &cleanup_mutex, &cleanup_cv]() 
    {
        while (true) 
        {
            {
                std::unique_lock<std::mutex> lock(cleanup_mutex);
                cleanup_cv.wait_for(lock, std::chrono::seconds(5), [&] { return cleanup_stop.load(); });
            
                if (cleanup_stop) break;
            }

            auto now = std::chrono::steady_clock::now();

            // 先收集要清理的条目（持锁），再锁外发响应
            std::vector<std::pair<std::shared_ptr<Connection>, uint32_t>> timeouts;

            {
                std::lock_guard<std::mutex> lock(state->pending_mutex);
                for (auto it = state->pending.begin(); it != state->pending.end(); ) 
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at).count();

                    if (elapsed > 30) 
                    {
                        auto client = it->second.client_conn.lock();
                        if (client) 
                        {
                            timeouts.push_back({client, it->second.original_id});
                        }
                        it = state->pending.erase(it);
                    } 
                    else 
                    {
                        ++it;
                    }
                }
            }

            // 锁外发超时响应
            for (auto& [client, orig_id] : timeouts) 
            {
                state->metrics.pending_timeouts.fetch_add(1);

                MyMessage err;
                err.type_    = MyType::ERROR_RESP;
                err.id_      = orig_id;
                err.payload_ = "TIMEOUT";
                client->sendResponse(err);

                LOG_WARN("[Gateway] pending timeout: orig_id=" + std::to_string(orig_id));
            }
        }
    });

    // 9.启动主循环（阻塞）
    if (!server.start())
    {
        LOG_ERROR("[Gateway] server start failed");
        return 1;
    }

    // 10. 清理：通知所有线程退出
    {
        std::lock_guard<std::mutex> lock(health_mutex);
        health_stop = true;
    }
    health_cv.notify_all();
    if (health_thread.joinable()) health_thread.join();

    {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        metrics_stop = true;
    }
    metrics_cv.notify_all();
    if (metrics_thread.joinable()) metrics_thread.join();

    {
        std::lock_guard<std::mutex> lock(monitor_mutex);
        monitor_stop = true;
    }
    monitor_cv.notify_all();
    if (monitor_thread.joinable()) monitor_thread.join();

    {
        std::lock_guard<std::mutex> lock(cleanup_mutex);
        cleanup_stop = true;
    }
    cleanup_cv.notify_all();
    if (cleanup_thread.joinable()) cleanup_thread.join();

    {
        std::lock_guard<std::mutex> lock(hb_mutex);
        heartbeat_stop = true;
    }
    hb_cv.notify_all();
    if (hb_thread.joinable()) hb_thread.join();

    server.stop();
    registry.Stop();
    AsyncLogger::instance().stop();
    return 0;
}