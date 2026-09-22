/**
 * bench_cpp.cpp —— EchoNet 压测客户端
 *
 * 用法: ./bench_cpp <用户数> <持续秒> [客户端线程数]
 * 例:   ./bench_cpp 100 30 4
 *
 * 流程: 每个客户端连接 → 登录 → 创建房间 → 加入房间 → 循环发消息
 * 输出: QPS、P50、P95、P99、Max
 */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <iostream>

const char* HOST = "127.0.0.1";
const int PORT = 8080;

std::atomic<uint64_t> g_requests{0};
std::atomic<uint64_t> g_errors{0};
std::vector<std::vector<double>> g_latencies_per_thread;

std::string encode(uint16_t type, uint32_t id, const std::string& payload) {
    uint32_t body_len = 2 + 4 + payload.size();
    std::string pkt(4 + body_len, 0);
    uint32_t net_len = htonl(body_len);
    memcpy(&pkt[0], &net_len, 4);
    uint16_t net_type = htons(type);
    memcpy(&pkt[4], &net_type, 2);
    uint32_t net_id = htonl(id);
    memcpy(&pkt[6], &net_id, 4);
    memcpy(&pkt[10], payload.data(), payload.size());
    return pkt;
}

bool recv_full(int fd, char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

uint16_t recv_packet(int fd, std::string* payload = nullptr, int timeout_sec = 5) {
    struct timeval tv{timeout_sec, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char header[4];
    if (!recv_full(fd, header, 4)) return 0;
    uint32_t body_len;
    memcpy(&body_len, header, 4);
    body_len = ntohl(body_len);

    std::vector<char> body(body_len);
    if (!recv_full(fd, body.data(), body_len)) return 0;

    uint16_t type;
    memcpy(&type, body.data(), 2);
    type = ntohs(type);

    if (payload && body_len >= 6) {
        payload->assign(body.data() + 6, body_len - 6);
    }
    return type;
}

bool recv_until(int fd, uint16_t expected, std::string* payload = nullptr, int max_tries = 20) {
    for (int i = 0; i < max_tries; ++i) {
        uint16_t t = recv_packet(fd, payload);
        if (t == 0) return false;
        if (t == expected) return true;
    }
    return false;
}

void worker(int tid, int duration_sec, int users_per_thread) {
    std::vector<int> fds;
    std::vector<double> latencies;

    // 建立连接
    for (int i = 0; i < users_per_thread; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, HOST, &addr.sin_addr);

        if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            g_errors++;
            continue;
        }

        // 登录
        std::string pkt = encode(0x01, 1, "whn|278813");
        send(fd, pkt.data(), pkt.size(), 0);
        if (!recv_until(fd, 0x02)) { close(fd); g_errors++; continue; }

        // 创建房间
        std::string room_name = "bench_" + std::to_string(tid) + "_" + std::to_string(i);
        pkt = encode(0x03, 2, room_name);
        send(fd, pkt.data(), pkt.size(), 0);
        std::string payload;
        if (!recv_until(fd, 0x04, &payload)) { close(fd); g_errors++; continue; }

        // 解析 room_id: "OK|{room_id}|{count}"
        size_t bar1 = payload.find('|');
        if (bar1 == std::string::npos) { close(fd); g_errors++; continue; }
        std::string room_id_str = payload.substr(bar1 + 1);
        size_t bar2 = room_id_str.find('|');
        if (bar2 != std::string::npos) room_id_str = room_id_str.substr(0, bar2);

        // 加入房间
        pkt = encode(0x05, 3, room_id_str);
        send(fd, pkt.data(), pkt.size(), 0);
        if (!recv_until(fd, 0x06)) { close(fd); g_errors++; continue; }

        fds.push_back(fd);
    }

    // 循环发消息
    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);
    uint32_t msg_id = 100;

    while (std::chrono::steady_clock::now() < end_time) {
        for (int fd : fds) {
            auto start = std::chrono::steady_clock::now();
            std::string pkt = encode(0x09, msg_id++, "bench");
            if (send(fd, pkt.data(), pkt.size(), 0) <= 0) {
                g_errors++;
                continue;
            }
            // 等 SEND_MSG_RESP (0x0A)
            if (recv_until(fd, 0x0A, nullptr, 5)) {
                auto dur = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                latencies.push_back(dur);
                g_requests++;
            } else {
                g_errors++;
            }
        }
    }

    for (int fd : fds) close(fd);
    g_latencies_per_thread[tid] = std::move(latencies);
}

int main(int argc, char** argv) {
    int total_users = argc > 1 ? std::atoi(argv[1]) : 100;
    int duration = argc > 2 ? std::atoi(argv[2]) : 30;
    int num_threads = argc > 3 ? std::atoi(argv[3]) : 4;

    int users_per_thread = total_users / num_threads;
    if (users_per_thread < 1) users_per_thread = 1;

    std::cout << "压测: " << total_users << " 用户, " << duration << " 秒, "
              << num_threads << " 客户端线程\n";

    g_latencies_per_thread.resize(num_threads);

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i, duration, users_per_thread);
    }

    // 每 2 秒打印进度
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "  [" << (int)elapsed << "s] 请求: " << g_requests
                  << ", 错误: " << g_errors
                  << ", QPS: " << (uint64_t)(g_requests / elapsed) << "\n";
        if (elapsed >= duration + 1) break;
    }

    for (auto& t : threads) t.join();

    double total_time = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    // 汇总延迟
    std::vector<double> all_latencies;
    for (auto& v : g_latencies_per_thread) {
        all_latencies.insert(all_latencies.end(), v.begin(), v.end());
    }
    std::sort(all_latencies.begin(), all_latencies.end());

    std::cout << "\n========== 结果 ==========\n";
    std::cout << "总请求数: " << g_requests << "\n";
    std::cout << "错误数: " << g_errors << "\n";
    std::cout << "总耗时: " << total_time << " s\n";
    std::cout << "QPS: " << (uint64_t)(g_requests / total_time) << "\n\n";

    if (!all_latencies.empty()) {
        size_t n = all_latencies.size();
        std::cout << "P50: " << all_latencies[n * 50 / 100] << " ms\n";
        std::cout << "P95: " << all_latencies[n * 95 / 100] << " ms\n";
        std::cout << "P99: " << all_latencies[n * 99 / 100] << " ms\n";
        std::cout << "Max: " << all_latencies.back() << " ms\n";
    }
    return 0;
}
