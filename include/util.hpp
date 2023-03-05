#ifndef UTIL_H
#define UTIL_H

#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <queue>
#include <time.h>
#include <assert.h>
#include "http_conn.hpp"
#include <vector>

class util_timer;

struct client_data
{
    sockaddr_in _address;
    int _sockfd;
    util_timer *_timer;
};

class util_timer
{
public:
    bool operator<(const util_timer& W)const 
    {
        return _expire < W._expire;
    }
    bool operator==(const util_timer& W)const
    {
        return _expire == W._expire;
    }
public:
    time_t _expire;
    client_data *_user_data;
    void (*cb_func)(client_data*);
};

class SmallHeap
{
public:
    SmallHeap();
    ~SmallHeap() {}
    void up(int idx);
    void down(int idx);
    int find(util_timer &str);
    void add(util_timer &str);
    void adjust(util_timer &str);
    void del(util_timer &str);
    void tick();
private:
    std::vector<util_timer> _heap;
    int _size;
};

class Util
{
public:
    Util() {}
    ~Util();
    void init(int timeout);
    void setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool oneshot, int trig_mode);
    void addsig(int sig, void(handler)(int), bool restart = true);
    static void sig_handler(int sig);
    void time_handler();
    void show_error(int connfd, const char* info);

public:
    static int *_pipefd;
    static int _epollfd;
    int _TIMEOUT;
    SmallHeap _queue;
};

void cb_func(client_data *user_data);

#endif