#include "ThreadPool/WorkThreadPool.h"
#include "Net/Connection.h"

WorkThreadPool::WorkThreadPool(int num)
{
    nums_ = num;
    stop_ = false;
    for (int  i = 0; i < nums_; i++)
    {
        thread_vector_.emplace_back([this]{worker_();});//非静态成员函数的调用必须绑定this指针
        //平时是被偷偷加上this了worker() ->worker(this)
        //这里不会被偷偷加
    }
}

WorkThreadPool::~WorkThreadPool()
{
    stop();
}

void WorkThreadPool::submit_(Task work)
{
    taskqueue_.push_(work);
}

void WorkThreadPool::worker_()
{
    while (!stop_)
    {
        Task task = taskqueue_.pop_();
        auto conn = task.conn_;
        if (conn)
        {
            HttpResponse reqs = conn -> handle(task.req_);
            conn -> setResponse (reqs);
        }
    }
}

void WorkThreadPool::stop()
{
    stop_ = true;
    taskqueue_.setstop();
    taskqueue_.cvnotify_all();
    for (auto& v : thread_vector_)
    {
        if (v.joinable())
        {
            v.join();
        }
    }
}