# EchoNet

C++17 高性能 Reactor 网络框架 + 实时聊天服务 + 分布式网关。

## 一句话

基于 epoll + eventfd 的多线程 Reactor，自研二进制协议，异步批量持久化，
在单机框架之上实现服务发现、健康检查、Metrics 可观测的轻量分布式网关。

## 核心数据

| 指标 | 数值 |
|------|------|
| 单机 QPS（500 并发） | **90,356** |
| P50 延迟 | **0.28 ms** |
| P99 延迟 | **1.07 ms** |
| 数据完整率 | **100%** |
| MySQL 写入优化 | **1.2K → 9 万 QPS（75 倍）** |

### 分布式网关

| 场景 | 结果 |
|------|------|
| 网关转发 | 4/4 成功（含 requestId 冲突场景） |
| 轮询负载均衡 | 9001/9002 各 2 请求，均衡 |
| 动态节点上线 | 5s 内自动连接 |
| 动态节点下线 | 10s 内自动摘除 |
| 健康检查（假死） | 18s 内标记 unhealthy |
| 优雅退出 | < 100 ms |

## 架构

    Client
      │
    Acceptor (mainloop)
      │
    IoThreadPool × 8 (每个 EventLoop 绑一个线程)
      │
    Connection (Buffer + Protocol)
      │
    WorkThreadPool × 4
      │
    ChatService
      ├── Redis（在线状态 / 房间成员 / 服务注册）
      └── MySQL（异步批量写，db_writer 线程）

### 分布式网关

    ┌─────────┐
    │ Client  │
    └────┬────┘
         │  MyProtocol
         ▼
    ┌──────────────────┐
    │ EchoNetGateway   │  8080
    └────┬─────────────┘
         │  轮询转发
    ┌────┴────┬────────┐
    ▼         ▼        ▼
  Backend1  Backend2  Backend3     ← 原有 EchoNet
  9001      9002      9003
    ▲         ▲        ▲
    └─────────┴────────┘
              │  TTL 心跳注册
         ┌────┴────┐
         │  Redis  │
         └─────────┘

## 技术亮点

### 1. Reactor 模型
- epoll + eventfd 跨线程唤醒 + timerfd 空闲超时
- 一线程一循环，`channels_` 无锁
- `assertInLoopThread` 强制线程亲和

### 2. 异步批量持久化
- 业务线程只入队，db_writer 每 100ms 批量 INSERT
- **QPS 1,200 → 90,356（75 倍）**，P99 从 19.7ms 降到 1.07ms
- Trade-off：崩溃时最多丢 100ms 数据

### 3. 连接内 FIFO 串行
- 同一连接任务严格按序执行，`taskRunning_` 保证不并发
- Task 完成回调触发下一个，链条不阻塞其他连接

### 4. 生命周期管理
- Channel 回调捕获 `weak_ptr`，防循环引用
- `pendingConnections_` 延迟析构，避免回调中销毁自己
- 关闭用 CAS 保证幂等

### 5. 自研二进制协议
- 长度前缀解决半包/粘包，1MB 上限防 DoS
- 协议类型白名单校验
- 协议抽象：Worker 只执行 `std::function<void()>`，不感知协议

### 6. 瓶颈定位

    Step 1: 同步 INSERT → QPS 1,200
            top -H 显示 work 线程 90% 时间在等 MySQL

    Step 2: 异步批量写 BATCH=200 → QPS 63,023

    Step 3: BATCH=2000 → QPS 90,356

    Step 4: QPS 突破 12 万后 MySQL 写入成硬上限，
            队列满后丢弃（异步写的本质 trade-off）

### 7. 分布式网关

- **服务注册**：后端启动时向 Redis 注册，TTL 10s，心跳 3s 续租
- **服务发现**：网关每 2s 从 Redis 拉取节点列表
- **动态上下线**：新节点上线自动连接，下线自动摘除
- **请求转发**：轮询负载均衡
- **requestId 重写**：全局唯一 ID 防多客户端冲突，响应回来还原
- **超时清理**：pending 表 30s 未响应自动清理，防内存泄漏
- **心跳保活**：网关每 5s 向后端发心跳
- **健康检查**：15s 无 pong 标记 unhealthy，恢复后自动加回
- **Metrics**：每 5s 打印转发数、成功数、超时数、每后端请求数
- **优雅退出**：5 个后台线程用 `condition_variable` 唤醒，毫秒级退出

## 目录结构

    EchoNet/
    ├── include/
    │   ├── Net/          Socket / Buffer / Connection
    │   ├── Reactor/      EventLoop / Channel / Acceptor
    │   ├── ThreadPool/   IoThreadPool / WorkThreadPool
    │   ├── Protocol/     Protocol 抽象 / MyProtocol
    │   ├── ChatService/  业务核心
    │   ├── Gateway/      ServiceRegistry（网关服务发现）
    │   ├── ServiceRegistry/  ServiceInstance
    │   ├── Session/ Room/ DB/ Logger/ Metrics/ RateLimit/
    │   └── Server/
    ├── src/
    │   ├── main.cpp              后端入口
    │   └── gateway_main.cpp      网关入口
    └── CMakeLists.txt

## 编译运行

    mkdir build && cd build
    cmake .. && make -j4

    # 启动后端（可多实例）
    source .env && ./EchoNet --port=9001
    source .env && ./EchoNet --port=9002
    source .env && ./EchoNet --port=9003

    # 启动网关
    source .env && ./EchoNetGateway --port=8080

## 测试

    cd text
    python3 text_all.py                    # 27 项回归测试
    python3 test_connection_lifecycle.py   # 4 个生命周期场景

## 分布式网关扩展

在单机 Reactor 框架之上，实现了一个轻量服务网关，支持服务发现、负载均衡、
请求转发、健康检查和可观测性。

### 关键设计

- **复用 Connection**：网关出站连接与客户端连接复用同一个 `Connection` 类
- **共享状态**：`shared_ptr<GatewayState>` 打包后端列表、pending 表、metrics，通过 lambda 按值捕获
- **请求关联**：`PendingRequest` 记录 `newId → (客户端连接, 原始 id)`
- **心跳检测**：后端返回 `HEARTBEAT_RESP`，网关据此更新该后端健康状态
- **线程管理**：5 个后台线程用 `condition_variable::wait_for` 支持可中断退出

### 典型流程

    Client → Gateway
      ↓ 收到 LOGIN_REQ, id=1
      ↓ 轮询选后端 9001
      ↓ new_id=1, pending[1] = (Client, orig_id=1)
      ↓ 重写 id=1, 转发到 9001
    Backend 9001
      ↓ 处理，返回 LOGIN_RESP, id=1
    Gateway
      ↓ 查 pending[1] → 找到 client + orig_id=1
      ↓ 还原 id=1
      ↓ 发回 Client
    Client 收到 LOGIN_RESP, id=1

### 实现中的 3 个坑

1. **EPOLLOUT 不生效**：`EventLoop::updateChannelEvent` 里有个 `if (getEvents() == events) return;` 检查，跳过了 `epoll_ctl MOD`，导致数据卡在 `outBuffer_`。根因是"调用者先改了 Channel 内存状态，再传相同值比较"，比较永远相等。修法：删掉这个错误的优化。

2. **出站 Connection 漏设 handler**：手动创建的 Connection 不走 IoThreadPool，忘了设 `businessHandler_` 和 `setCallBack`，后端响应入了队但没人投递到 Worker，卡在 `pendingTasks_` 里。

3. **线程退出慢**：`sleep_for` 不可中断，退出最坏等 50 秒。改成 `condition_variable::wait_for` + `notify_all`，毫秒级退出。关键：持锁设 stop 标志（防错过 notify），用 predicate 防虚假唤醒，用 `{}` 作用域让 `unique_lock` 提前释放（防死锁）。

## 已知限制

- 单机部署，未实现多机分布式
- 消息持久化异步，崩溃可能丢最后 100ms
- 连接数受单机 fd 上限约束
- Gateway 是单实例，未实现多网关高可用
- 未实现请求重试 / 熔断 / 限流
- 服务注册用 `KEYS` 命令（生产环境应改 `SCAN`）

## 后续计划

- [ ] 请求重试 + 熔断（Gateway）
- [ ] `SCAN` 替代 `KEYS`（Gateway）
- [ ] 多网关高可用
- [ ] Raft 分布式一致性

## 技术栈

C++17 / Linux / epoll / eventfd / timerfd / MySQL / Redis / CMake / Prometheus 风格 Metrics