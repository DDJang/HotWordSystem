#pragma once 

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <type_traits>
#include <atomic>
#include <memory>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()) : 
        stop(false), 
        active_threads(0),
        max_threads(threads),
        min_threads(std::max(size_t(1), threads / 2))
    {
        for (size_t i = 0; i < min_threads; ++i) {
            workers.emplace_back([this, id = i] {
                worker_loop(id);
            });
        }
        active_threads = min_threads;
    }

    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() {
                try {
                    (*task)();
                } catch (const std::exception& e) {
                    std::cerr << "Task execution failed: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Task execution failed with unknown exception" << std::endl;
                }
            });
        }
        
        // 动态调整线程数
        adjust_thread_count();
        condition.notify_one();
        return res;
    }

    // 批量提交任务
    template <class Iterator>
    void enqueue_bulk(Iterator begin, Iterator end) {
        std::vector<std::function<void()>> bulk_tasks;
        bulk_tasks.reserve(std::distance(begin, end));
        
        for (auto it = begin; it != end; ++it) {
            bulk_tasks.emplace_back(*it);
        }
        
        if (!bulk_tasks.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            
            for (auto& task : bulk_tasks) {
                tasks.emplace(std::move(task));
            }
        }
        
        // 动态调整线程数
        adjust_thread_count();
        condition.notify_all();
    }

    // 显式停止线程池
    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
        active_threads = 0;
    }

    ~ThreadPool() {
        shutdown();
    }

    // 获取当前线程数
    size_t get_thread_count() const {
        return active_threads;
    }

    // 获取当前任务队列大小
    size_t get_queue_size() const {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<size_t> active_threads;
    const size_t max_threads;
    const size_t min_threads;

    // 工作线程主循环
    void worker_loop(size_t id) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this] { 
                    return stop || !tasks.empty(); 
                });
                
                if (stop && tasks.empty()) return;

                task = std::move(tasks.front());
                tasks.pop();
            }
            
            // 执行任务
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task execution failed with unknown exception" << std::endl;
            }
        }
    }

    // 动态调整线程数
    void adjust_thread_count() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        size_t queue_size = tasks.size();
        
        // 如果任务队列长度大于线程数，且线程数小于最大值，则增加线程
        if (queue_size > active_threads && active_threads < max_threads) {
            size_t threads_to_add = std::min(
                max_threads - active_threads,
                std::min(size_t(4), queue_size - active_threads)
            );
            
            for (size_t i = 0; i < threads_to_add; ++i) {
                workers.emplace_back([this, id = workers.size()] {
                    worker_loop(id);
                });
                active_threads++;
            }
        }
    }
};