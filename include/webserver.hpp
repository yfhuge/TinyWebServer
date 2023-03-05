#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>
#include <assert.h>
#include "util.hpp"
#include "http_conn.hpp"
#include "threadpool.hpp"
#include <errno.h>

class WebServer
{
public:
    static const int MAX_FD = 65536;    // 最大文件描述符
    static const int MAX_EVENT_NUMBER = 10000;  // 最大事件数
    static const int TIMESLOT = 5;      // 最小超时时间
public:
    WebServer();
    ~WebServer();
    void init(int promode, int port, int thread_num, int trig_mode, int sql_num, std::string user, std::string password, std::string dbname, int close_log);
    void thread_pool_init();
    void sql_pool_init();
    void log_init();
    void EpollListen();
    void EventLoop();
    bool dealclientconn();
    void dealread(int sockfd);
    void dealwrite(int sockfd);
    bool dealsignal(bool &timeout, bool &stop_server);
    void trig_mode_init();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(int connfd);
    void deal_timer(int sockfd);
private:
    int _port;
    int _epollfd;
    int _listenfd;
    int _pipefd[2];
    int _thread_num;
    int _sql_num;
    int _promode;
    int _trig_mode;
    int _close_log;
    int _listen_trig_mode;
    int _conn_trig_mode;
    epoll_event _events[MAX_EVENT_NUMBER];
    Util _util;
    http_conn *_users;
    ThreadPool<http_conn> *_pool;
    conn_pool *_sql_pool;
    std::string _sql_user;
    std::string _sql_password;
    std::string _sql_dbname;
};

#endif