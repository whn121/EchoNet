#pragma once

#include <atomic>
#include <cstdint>

struct Metrics {
    static std::atomic<uint64_t> total_connections;
    static std::atomic<uint64_t> active_connections;
    static std::atomic<uint64_t> total_requests;
    static std::atomic<uint64_t> error_requests;
    static std::atomic<uint64_t> bytes_read;
    static std::atomic<uint64_t> bytes_written;
};