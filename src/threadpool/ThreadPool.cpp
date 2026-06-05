#include "threadpool/ThreadPool.h"//我只设置了include目录下找
#include <iostream>
ThreadPool::ThreadPool(int x):num_(x), stop_(false)//默认参数只在声明时出现
//thread没有拷贝构造,初始化列表不会拷贝,只能用初始化列表初始化
//拷贝会使等待join和detach混乱,两个对象同时指向一个线程会两次析构
//变量不可以放在赋值的右边
{
    for (int i = 0; i < num_; i++)
    {
        thread_array_.emplace_back([this]{woker_for();});//lambda表达式隐士构造
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::woker_for()
{
    while(true)//应该醒来后再判断stop要不死循环
    {
        task woker = task_queue_.tpop();
        if(stop_ && task_queue_.tempty())
        {
            break;
        }
        if (woker != nullptr)
        {
            woker();
        }
        else
        {
            std::cout << "任务获取失败" << std::endl;
        }
    }
}

void ThreadPool::submit(task val)
{
    task_queue_.tpush(val);
}

void ThreadPool::stop()
{
    stop_ = true;
    this->task_queue_.mynotify_all();
    for (auto& t : thread_array_)// 安全退出
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}