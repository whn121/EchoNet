#include "ThreadPool/WorkThreadPool.h"
#include "Net/Connection.h"
#include "HTTP/HttpService.h"
#include <any>

WorkThreadPool::WorkThreadPool(int num) : num_(num) {
    for (int i = 0; i < num; ++i)
        threads_.emplace_back([this] { worker(); });
}

WorkThreadPool::~WorkThreadPool() { stop(); }

void WorkThreadPool::submit(Task task) {
    queue_.push(std::move(task));
}

void WorkThreadPool::worker() {
    while (!stop_) {
        Task task = queue_.pop();  // 阻塞获取任务
        auto conn = task.conn_;
        if (!conn) continue;       // 空任务，可能为停止信号
        // 从any中取出HttpRequest
        auto request = std::any_cast<HttpRequest>(task.message_);
        HttpResponse response = HttpService::handle(request);  // 业务处理
        conn->sendResponse(response);  // 发送响应
    }
}

void WorkThreadPool::stop() {
    if (stop_) return;
    stop_ = true;
    queue_.stop();  // 唤醒所有等待线程
    for (auto& t : threads_) if (t.joinable()) t.join();
}