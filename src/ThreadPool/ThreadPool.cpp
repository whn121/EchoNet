#include "ThreadPool/ThreadPool.h"

ThreadPool::ThreadPool(int num)
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

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::submit_(Task wok)
{
    taskqueue_.push_(move(wok));
}

void ThreadPool::worker_()
{
    while (!stop_)
    {
        Task task = taskqueue_.pop_();
        if (task)task();
    }
}

void ThreadPool::stop()
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