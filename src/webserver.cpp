#include "webserver.hpp"

WebServer::WebServer()
{
    _users = new http_conn[MAX_FD];

}

WebServer::~WebServer()
{
    close(_epollfd);
    close(_listenfd);
    close(_pipefd[1]);
    close(_pipefd[0]);
    delete[] _users;
    delete _pool;
}

void WebServer::init(int promode, int port, int thread_num, int trig_mode, int sql_num, std::string user, std::string password, std::string dbname, int close_log)
{
    _promode = promode;
    _port = port;
    _thread_num = thread_num;
    _trig_mode = trig_mode;
    _sql_num = sql_num;
    _sql_user = user;
    _sql_password = password;
    _sql_dbname = dbname;
    _close_log = close_log;

    log_init();
    sql_pool_init();
    thread_pool_init();
    trig_mode_init();
}

void WebServer::trig_mode_init()
{
    if (_trig_mode == 0)
    {
        _listen_trig_mode = 0;
        _conn_trig_mode = 0;
    }
    else if (_trig_mode == 1)
    {
        _listen_trig_mode = 0;
        _conn_trig_mode = 1;
    }
    else if (_trig_mode == 2)
    {
        _listen_trig_mode = 1;
        _conn_trig_mode = 0;
    }
    else if (_trig_mode == 3)
    {
        _listen_trig_mode = 1;
        _conn_trig_mode = 1;
    }
}

void WebServer::log_init()
{
    if (_close_log == 0)
    {
        Log::getinstance()->init("../logging/ServerLog", _close_log, 20000, 8000000);
    }
}

void WebServer::thread_pool_init()
{
    _pool = new ThreadPool<http_conn>(_promode, _sql_pool, _thread_num);
}

void WebServer::sql_pool_init()
{
    _sql_pool = conn_pool::getInstance();
    _sql_pool->init(_sql_num, "localhost", _sql_user, _sql_password, _sql_dbname, 3306, _close_log);

    _users->sql_init_result(_sql_pool);
}

void WebServer::EpollListen()
{
    _listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(_listenfd != -1);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int flag = 1;
    setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int ret = bind(_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(_listenfd, 5);
    assert(ret != -1);

    _util.init(TIMESLOT, _close_log);

    _epollfd = epoll_create(5);
    assert(_epollfd != -1);
    _util.addfd(_epollfd, _listenfd, false, _listen_trig_mode);
    http_conn::_epollfd = _epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, _pipefd);
    assert(ret != -1);
    _util.setnonblocking(_pipefd[1]);
    _util.addfd(_epollfd, _pipefd[0], false, 0);

    _util.addsig(SIGPIPE, SIG_IGN);
    _util.addsig(SIGALRM, _util.sig_handler, false);
    _util.addsig(SIGTERM, _util.sig_handler, false);

    alarm(TIMESLOT);

    Util::_pipefd = _pipefd;
    Util::_epollfd = _epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    _users[connfd].init(connfd, client_address, _conn_trig_mode, _close_log);

    time_t cur = time(NULL);
    _util._queue.add(connfd, client_address, cur + 3 * TIMESLOT);
}

void WebServer::adjust_timer(int connfd)
{
    time_t cur = time(NULL);
    _util._queue.adjust(connfd, cur + 3 * TIMESLOT);
    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(int sockfd)
{
    _util._queue.delWork(sockfd);
}

bool WebServer::dealclientconn()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    if (_listen_trig_mode == 0)
    {
        int connfd = accept(_listenfd, (struct sockaddr *)&client_addr, &client_len);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is %d", "accept error", errno);
            return false;
        }
        if (http_conn::_user_count >= MAX_FD)
        {
            _util.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_addr);
    }
    else 
    {
        while (1)
        {
            int connfd = accept(_listenfd, (struct sockaddr *)&client_addr, &client_len);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is %d", "accept error", errno);
                return false;
            }
            if (http_conn::_user_count >= MAX_FD)
            {
                _util.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                return false;
            }
            timer(connfd, client_addr);
        }
        return false;
    }

    return true;
}

void WebServer::dealread(int sockfd)
{
    if (_promode == 1)
    {
        // Reactor
        if (_util._queue.find(sockfd))
        {
            adjust_timer(sockfd);
        }

        _pool->append(_users + sockfd, 0);

        while (1)
        {
            if (_users[sockfd]._improv == 1)
            {
                if (_users[sockfd]._timer_flag == 1)
                {
                    deal_timer(sockfd);
                    _users[sockfd]._timer_flag = 0;
                }
                _users[sockfd]._improv = 0;
                break;
            }
        }
    }
    else
    {
        // Proactor
        if (_users[sockfd].read())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(_users[sockfd].get_address()->sin_addr));
            
            
            _pool->append(_users + sockfd);
            if (_util._queue.find(sockfd))
            {
                adjust_timer(sockfd);
            }
        }
        else
        {
            deal_timer(sockfd);
        }
    }
}

void WebServer::dealwrite(int sockfd)
{
    if (_promode == 1)
    {
        if (_util._queue.find(sockfd))
        {
            adjust_timer(sockfd);
        }
        // Reactor
        _pool->append(_users + sockfd, 1);

        while (1)
        {
            if (_users[sockfd]._improv == 1)
            {
                if (_users[sockfd]._timer_flag == 1)
                {
                    deal_timer(sockfd);
                    _users[sockfd]._timer_flag = 0;
                }
                _users[sockfd]._improv = 0;
                break;
            }
        }
    }
    else
    {
        // Proactor
        if (_users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(_users[sockfd].get_address()->sin_addr));
            
            /*
            这里有点疑问
            
            */
            // _pool->append(_users + sockfd);
            
            if (_util._queue.find(sockfd) != -1)
            {
                adjust_timer(sockfd);
            }
        }
        else
        {
            deal_timer(sockfd);
            // _users[sockfd].close_conn();
        }
    }
}

bool WebServer::dealsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else 
    {
        for (int i = 0; i < ret; ++ i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::EventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int num = epoll_wait(_epollfd, _events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < num; ++i)
        {
            int sockfd = _events[i].data.fd;
            if (sockfd == _listenfd)
            {
                // 有新连接
                dealclientconn();
            }
            else if (_events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
            {
                // 服务器关闭连接
                deal_timer(sockfd);
            }
            else if (sockfd == _pipefd[0] && (_events[i].events & EPOLLIN))
            {
                // 处理信号
                bool flag = dealsignal(timeout, stop_server);
                if (!flag)
                {
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            else if (_events[i].events & EPOLLIN)
            {
                dealread(sockfd);
            }
            else if (_events[i].events & EPOLLOUT)
            {
                dealwrite(sockfd);
            }
        }

        if (timeout)
        {
            _util.time_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}