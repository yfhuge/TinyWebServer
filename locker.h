#ifndef _LOCKER_H_
#define _LOCKER_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 互斥锁
class mtx {
public:
    mtx() {
        if( pthread_mutex_init(&m_mutex, NULL) != 0 ) {
            throw std::exception();
        }
    }
    ~mtx() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;

};

// 信号量
class sem {
public:
    sem() {
        if( sem_init(&m_sem, 0, 0) != 0 ) {
            throw std::exception();
        }
    }
    sem(int val) {
        if( sem_init(&m_sem, 0, val) != 0 ) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

// 条件变量
class cond {
public:
    cond() {
        if( pthread_cond_init(&m_cond, NULL) != 0 ) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
    bool timedwait(pthread_mutex_t *mtx, struct timespec* t) {
        return pthread_cond_timedwait(&m_cond, mtx, t) == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

#endif