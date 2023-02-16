#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "locker.h"
#include <list>
#include <cstdio>
#include <exception>

template<typename T>
class threadPool {
public:
    static void* work(void *arg);
    void run();

public:
    threadPool(int number = 8, int max_v = 10000);
    ~threadPool();
    bool append(T * request);

private:
    // 存放线程的数组
    pthread_t *_threads;
    // 存放线程的最大的数量
    int _number;
    // 请求的最大数量
    int _max_requests_number;
    // 请求队列
    std::list<T*> _queue;
    // 保护请求队列的互斥锁
    mtx _locker;
    // 是否有任务需要处理
    sem _sem;
    // 是否结束进程
    bool _m_stop;
};

template<typename T>
threadPool<T>::threadPool(int number, int max_v) :
    _number(number), _max_requests_number(max_v), _threads(nullptr), _m_stop(false) {
    
    if( number <= 0 || max_v <= 0 ) {
        throw std::exception();
    }

    _threads = new pthread_t[_number];

    if(!_threads) {
        throw std::exception();
    }

    for(int i = 0; i < _number; ++ i) {
        if( pthread_create(_threads + i, NULL, work, this) != 0 ) {
            delete[] _threads;
            _threads = nullptr;
            throw std::exception();
        }
        if( pthread_detach(_threads[i]) != 0 ) {
            delete[] _threads;
            _threads = nullptr;
            throw std::exception();
        }
        printf("create %d_th thread\n", i);
    }
}

template<typename T>
threadPool<T>::~threadPool() {
    delete[] _threads;
    _threads = nullptr;
    _m_stop = true;
}

template<typename T>
bool threadPool<T>::append(T * request) {
    _locker.lock();
    if( _queue.size() >= _number ) {
        _locker.unlock();
        return false;
    }
    _queue.push_back(request);
    _sem.post();
    _locker.unlock();
    return true;
}   

template<typename T>
void* threadPool<T>::work(void *arg) {
    threadPool* pool = (threadPool *)arg;
    pool->run();
    return NULL;
}

template<typename T>
void threadPool<T>::run() {
    while( !_m_stop ) {
        _sem.wait();
        _locker.lock();
        if( _queue.empty() ) {
            _locker.unlock();
            continue;
        }
        T* request = _queue.front();
        _queue.pop_front();
        _locker.unlock();
        if( !request ) {
            continue;
        }
        request->process();
    }
}

#endif