#include "util.hpp"

int *Util::_pipefd = 0;
int Util::_epollfd = -1;

SmallHeap::SmallHeap()
{
    _size = 0;
    _heap.push_back(util_timer());
}

void SmallHeap::up(int idx)
{
    while (idx != 1)
    {
        if (_heap[idx] < _heap[idx / 2]) std::swap(_heap[idx], _heap[idx / 2]);
        idx >>= 1;
    }
}

void SmallHeap::down(int idx)
{
    int tmp = idx;
    if (idx * 2 <= _size && _heap[idx * 2] < _heap[tmp]) tmp = idx * 2;
    if (idx * 2 + 1 <= _size && _heap[idx * 2 + 1] < _heap[tmp]) tmp = idx * 2 + 1;
    if (idx != tmp)
    {
        std::swap(_heap[idx], _heap[tmp]);
        down(tmp);
    }
}

int SmallHeap::find(util_timer &str)
{
    for (int i = 1; i <= _size; ++ i)
    {
        if (_heap[i] == str)
        {
            return i;
        }
    }
    return -1;
}

void SmallHeap::add(util_timer &str)
{
    _heap.push_back(str);
    ++ _size;
    up(_size);
}

void SmallHeap::adjust(util_timer &str)
{
    int idx = find(str);
    down(idx);
}

void SmallHeap::del(util_timer &str)
{
    int idx = find(str);
    std::swap(_heap[idx], _heap[_size --]);
    down(idx);
    _heap[_size + 1].cb_func(_heap[_size + 1]._user_data);
}

void SmallHeap::tick()
{
    time_t cur = time(NULL);
    while (_size)
    {
        if (_heap[1]._expire > cur)
        {
            break;
        }
        std::swap(_heap[1], _heap[_size --]);
        down(1);
        _heap[_size + 1].cb_func(_heap[_size + 1]._user_data);

    }
}

Util::~Util()
{
    
}

void Util::init(int timeout)
{
    _TIMEOUT = timeout;
}

void Util::setnonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

void Util::addfd(int epollfd, int fd, bool oneshot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (trig_mode)
    {
        event.events |= EPOLLET;
    }
    if (oneshot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Util::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Util::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Util::time_handler()
{
    _queue.tick();
    alarm(_TIMEOUT);
}

void Util::show_error(int connfd, const char* info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

class util_timer;
void cb_func(client_data *user_data)
{
    epoll_ctl(Util::_epollfd, EPOLL_CTL_DEL, user_data->_sockfd, NULL);
    assert(user_data);
    close(user_data->_sockfd);
    http_conn::_user_count --;
}