#include "Metrics/Metrics.h"

std::atomic<uint64_t> Metrics::total_connections{0};
std::atomic<uint64_t> Metrics::active_connections{0};
std::atomic<uint64_t> Metrics::total_requests{0};
std::atomic<uint64_t> Metrics::error_requests{0};
std::atomic<uint64_t> Metrics::bytes_read{0};
std::atomic<uint64_t> Metrics::bytes_written{0};