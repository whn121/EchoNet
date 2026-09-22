# EchoNet

基于 C++17 的 Reactor 高性能实时通信服务系统。

## 特性

- **Reactor 网络模型**：epoll + eventfd + 多 EventLoop
- **多线程架构**：IO 线程池 + Work 线程池
- **自定义二进制协议**：长度前缀，解决 TCP 半包/粘包
- **异步 MySQL 写入**：批量 INSERT，QPS 从 1200 提升到 9 万
- **Redis 缓存**：在线状态、房间成员
- **协议解耦**：Worker 层不感知具体协议类型
- **空闲连接超时**：基于 timerfd 的 epoll 事件驱动
- **异步日志**：AsyncLogger，不阻塞业务线程
- **Metrics**：连接数、请求数、字节数统计
- **优雅关闭**：Worker 处理完队列任务再退出

## 架构

    Client (TCP)
        │
        ▼
    ┌──────────┐
    │ Server   │  mainloop + Acceptor（只负责 accept）
    └────┬─────┘
         │
    ┌────┼────┬────────┐
    ▼    ▼    ▼        ▼
    EventLoop × N（IO 线程）
         │
    Connection (Buffer + Protocol)
         │
    WorkThreadPool
         │
    ChatService
      /       \
    Redis     MySQL
      │        │
    Session   async INSERT (db_writer)
      │
    Room

## 目录结构

    EchoNet/
    ├── include/
    │   ├── Net/          Socket / Buffer / Connection
    │   ├── Reactor/      EventLoop / Channel / Acceptor
    │   ├── ThreadPool/   IO / Work 线程池
    │   ├── Protocol/     MyProtocol / Protocol 抽象
    │   ├── ChatService/  业务核心
    │   ├── Session/      会话
    │   ├── Room/         房间
    │   ├── DB/           MySQLPool / RedisPool
    │   ├── Logger/       AsyncLogger
    │   ├── Metrics/      统计
    │   └── Server/       服务器主类
    ├── src/
    ├── text/             测试脚本
    └── CMakeLists.txt

## 编译

    mkdir -p build && cd build
    cmake ..
    make -j4

## 运行

    # 启动依赖
    sudo systemctl start mysql
    redis-server --daemonize yes

    # 启动服务器
    ./EchoNet --port=8080 --io=8 --work=4

## 测试

    cd text
    python3 text_all.py              # 完整回归（27 项）
    python3 test_connection_lifecycle.py  # Connection 生命周期（4 场景）

## 性能数据

### 测试环境

- WSL2, 8 核, SSD
- 自研 C++ 压测客户端（协议为自定义二进制，wrk/ab 不适用）
- 每个 TCP 连接模拟一个用户，客户端多线程管理连接

### 核心数据

| 版本 | 客户端线程 | 并发 | QPS | P50 | P99 | 数据完整率 |
|------|-----------|------|-----|-----|-----|-----------|
| 同步 INSERT MySQL | 4 | 500 | 1,193 | 12.4ms | 19.7ms | 100% |
| 异步批量写 (BATCH=200) | 32 | 500 | 63,023 | 0.43ms | 1.06ms | 100% |
| **异步批量写 (BATCH=2000)** | **32** | **500** | **90,356** | **0.28ms** | **1.07ms** | **100%** |
| 异步批量写 (BATCH=2000) | 64 | 500 | 126,578 | 0.38ms | 1.84ms | 96.5% |

### 瓶颈定位路径

**Step 1：同步写 MySQL → QPS 1200**
- 每条消息 INSERT 约 0.8ms
- `top -H` 显示 work 线程 CPU 仅 4%，`%wa` 5.5%
- 结论：work 线程 90% 时间在等 MySQL

**Step 2：异步批量写 → QPS 63000（零丢失）**
- 业务线程只入队，db_writer 每 100ms 批量 INSERT
- BATCH=200 时写入速度 2000 条/秒

**Step 3：加大批量 → QPS 90000（零丢失）**
- BATCH_SIZE 从 200 调到 2000
- 写入速度提升到 2 万条/秒，匹配入队速度
- MySQL 记录数 == 请求数

**Step 4：QPS 突破 12 万后的 trade-off**
- QPS 126578 时，MySQL 写入速度（2 万/秒）跟不上入队速度
- 50 万条队列上限 5 秒填满
- 开始丢弃消息，丢约 3.5%

### 关键结论

- **QPS 9 万以下**：异步写数据 100% 落盘
- **QPS 9 万以上**：MySQL 写入速度成为硬上限，队列满后丢弃
- 这是**异步写的本质 trade-off**：用可靠性换吞吐
- 聊天消息可容忍少量丢失，登录/房间等关键数据仍然同步写

## 关键设计

### 1. 一线程一循环（Reactor 模型）

- 每个 EventLoop 绑定一个线程，内部只有一个 `epoll_wait`
- `channels_` / `connections_` 无锁，只由所属线程访问
- 线程亲和性由 `assertInLoopThread` 强制检查

### 2. runInLoop 跨线程调度

- 工作线程通过 `runInLoop` 投递任务到连接所属的 IO 线程
- 保证所有 `epoll_ctl` 和 Connection 操作都在同一线程

### 3. 自定义二进制协议

    | 4字节长度 | 2字节类型 | 4字节请求ID | 变长payload |

- 长度前缀解决 TCP 半包/粘包
- 长度上限 1MB 防 DoS
- 消息类型白名单校验

### 4. 异步批量写 MySQL

- 业务线程处理完消息后只入队，不阻塞
- 后台 db_writer 线程每 100ms 或攒够 2000 条批量 INSERT
- 队列有 50 万条上限，满了丢弃并计数
- 代价：崩溃时最多丢 100ms 数据

### 5. 空闲连接超时（timerfd）

- 每个 EventLoop 一个 timerfd，5 秒周期
- 到期时扫描本线程的 Connection，超过 60 秒无活动则关闭
- 无额外线程、精确触发、统一事件循环

### 6. 协议解耦

- Worker 只执行 `std::function<void()>` 闭包，不感知协议类型
- 加新协议只需换 handler，Worker 一行不改

### 7. 优雅关闭

- `TaskQueue::pop` 在"队列空 + 停止"时返回空 Task
- Worker 看到空 Task 才退出，保证已入队任务全部执行

### 8. 数据层

- MySQL：用户、房间、消息持久化（预处理语句防 SQL 注入）
- Redis：在线状态、房间成员缓存（失败降级，不阻断主流程）
- 连接池：RAII 自动归还 + 健康检查

## 已知限制

- 未实现 RTC（UDP/RTP/QoS）
- 消息持久化异步，可能丢最后 100ms
- 密码明文存储（可改 SHA256）

## 后续计划

- [ ] UDP + RTP 最小支持
- [ ] 密码哈希
- [ ] 多 writer 线程优化（突破 9 万 QPS 上限）

## 技术栈

- C++17
- Linux / epoll / eventfd / timerfd
- MySQL / Redis
- CMake
