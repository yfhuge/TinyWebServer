#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "locker.hpp"
#include <list>
#include <exception>
#include "sql_conn_pool.hpp"

template<typename T>
class ThreadPool
{
public:
    ThreadPool(int mode, conn_pool *pool, int thread_num = 8, int max_requests = 8000);
    ~ThreadPool();
    bool append(T *request, int state);
    bool append(T *request);
public:
    static void* worker(void*);
    void run();
private:
    int _max_requests;  // 请求队列中允许的最大请求数
    int _thread_num;    // 线程池中的线程数
    pthread_t *_threads;    // 表示线程池的数组
    std::list<T*>   _queue; // 请求队列
    locker  _locker;    // 互斥锁
    sem _sem;   // 信号量
    int _promode;   // 事件处理模式
    conn_pool *_pool;   // 数据库
    bool _stop; 
};

template<typename T>
ThreadPool<T>::ThreadPool(int mode, conn_pool *pool, int thread_num, int max_requests) :_max_requests(max_requests), _pool(pool), _thread_num(thread_num), _threads(NULL), _stop(false), _promode(mode)
{
    if (max_requests <= 0 || thread_num <= 0)
    {
        throw std::exception();
    }
    _threads = new pthread_t[thread_num];
    if (!_threads)
    {
        throw std::exception();
    }

    for (int i = 0; i < thread_num; ++ i)
    {
        if (pthread_create(_threads + i, NULL, worker, this) != 0)
        {
            delete[] _threads;
            throw std::exception();
        }
        if (pthread_detach(_threads[i]))
        {
            delete[] _threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool()
{
    _stop = true;
    delete[] _threads;
}

template<typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    _locker.lock();
    if (_queue.size() >= _max_requests)
    {
        _locker.unlock();
        return false;
    }
    request->_state = state;
    _queue.push_back(request);
    _locker.unlock();
    _sem.post();
    return true;   
}

template<typename T>
bool ThreadPool<T>::append(T *request)
{
    _locker.lock();
    if (_queue.size() >= _max_requests)
    {
        _locker.unlock();
        return false;
    }
    _queue.push_back(request);
    _locker.unlock();
    _sem.post();
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void ThreadPool<T>::run()
{
    while (!_stop)
    {
        _sem.wait();
        _locker.lock();
        if (_queue.empty())
        {
            _locker.unlock();
            continue;
        }
        T *request = _queue.front();
        _queue.pop_front();
        _locker.unlock();
        
        if (_promode == 1)
        {
            // Reactor模式
            if (request->_state == 0)
            {
                if (request->read())
                {
                    request->_improv = 1;
                    conn_sql mysqlconn(&request->_conn, _pool);
                    request->process();
                }
                else 
                {
                    request->_improv = 1;
                    request->_timer_flag = 1;
                }
            }
            else 
            {
                if (request->write())
                {
                    request->_improv = 1;
                }
                else 
                {
                    request->_improv = 1;
                    request->_timer_flag = 1;
                }
            }
        }
        else 
        {
            // Proactor模式
            conn_sql mysqlconn(&request->_conn, _pool);
            request->process();
        }
    }
}


#endif