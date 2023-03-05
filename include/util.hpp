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
#include <unordered_map>
#include "log.hpp"

class util_timer;

struct client_data
{
    int _sockfd;
    sockaddr_in _address;
    time_t _expire;
    void (*cb_func)(int);
    bool operator<(const client_data& W)const 
    {
        return _expire < W._expire;
    }
};

class SmallHeap
{
public:
    SmallHeap();
    ~SmallHeap();
    void up(int idx);
    void down(int idx);
    void add(int sockfd, struct sockaddr_in& add, time_t t);
    void adjust(int sockfd, time_t t);
    void clear();
    void tick();
    bool delWork(int sockfd);
    bool find(int sockfd);
    int _close_log;

private:
    void del(int idx);
    void swap(int i, int j);
    std::vector<client_data> _heap;
    std::unordered_map<int, int> _ref;
};

class Util
{
public:
    Util() {}
    ~Util() {}
    void init(int timeout, int close_log);
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

void cb_func(int sockfd);

#endif