#include "ThreadPool.hpp"
extern "C" {
    #include "kronkpool/kronkpool.h"
}
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
    using Task = std::function<void(void)>;

    extern "C" void* runTask(void* data)
    {
        std::unique_ptr<Task> task(static_cast<Task*>(data));

        (*task)();
        return nullptr;
    }
}

kuge::ThreadPool::ThreadPool(std::size_t workers)
    : m_pool(kpThreadPool_create(static_cast<ssize_t>(workers == 0 ? std::max(1u, std::thread::hardware_concurrency()) : workers)))
{
    if (!m_pool) {
        throw std::runtime_error("ThreadPool: cannot create the threads");
    }
}

kuge::ThreadPool::~ThreadPool(void)
{
    kpThreadPool_waitIdle(m_pool);
    kpThreadPool_destroy(m_pool);
}

void kuge::ThreadPool::post(std::function<void(void)> task)
{
    auto owned = std::make_unique<Task>(std::move(task));

    if (kpThreadPool_pushTask(m_pool, &runTask, owned.get()) != 0) {
        throw std::runtime_error("ThreadPool: the task was refused");
    }
    owned.release();   // the worker owns it now
}

void kuge::ThreadPool::waitIdle(void)
{
    kpThreadPool_waitIdle(m_pool);
}

std::size_t kuge::ThreadPool::workers(void) const noexcept
{
    return kpThreadPool_getWorkers(m_pool);
}
