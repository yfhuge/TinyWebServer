#include "util.hpp"

int *Util::_pipefd = 0;
int Util::_epollfd = -1;

SmallHeap::SmallHeap()
{
    _heap.push_back(client_data());
}

SmallHeap::~SmallHeap()
{
    clear();
}

void SmallHeap::swap(int i, int j)
{
    std::swap(_heap[i], _heap[j]);
    _ref[_heap[i]._sockfd] = i;
    _ref[_heap[j]._sockfd] = j;
}

void SmallHeap::up(int idx)
{
    while (idx != 1)
    {
        if (_heap[idx] < _heap[idx / 2]) swap(idx, idx / 2);
        idx >>= 1;
    }
}

void SmallHeap::down(int idx)
{
    int tmp = idx;
    if (idx * 2 < _heap.size() && _heap[idx * 2] < _heap[tmp]) tmp = idx * 2;
    if (idx * 2 + 1 < _heap.size() && _heap[idx * 2 + 1] < _heap[tmp]) tmp = idx * 2 + 1;
    if (idx != tmp)
    {
        swap(idx, tmp);
        down(tmp);
    }
}

void SmallHeap::del(int idx)
{
    int n = _heap.size() - 1;
    if (idx < n)
    {
        swap(idx, n);
        up(idx);
        down(idx);
    }
    _ref.erase(_heap.back()._sockfd);
    _heap.pop_back();
}

void SmallHeap::add(int sockfd, struct sockaddr_in& addr, time_t t)
{
    if (_ref.count(sockfd))
    {
        // 新加的定时器在之前有，只需要调整即可
        int idx = _ref[sockfd];
        _heap[idx]._expire = t;
        _heap[idx]._address = addr;
        _heap[idx].cb_func = cb_func;
        up(idx);
        down(idx);
    }
    else 
    {
        // 这个在小根堆中没有，加入到堆中
        int idx = _heap.size();
        _ref[sockfd] = idx;
        _heap.push_back({sockfd, addr, t, cb_func});
        up(idx);
    }
}

void SmallHeap::adjust(int sockfd, time_t t)
{
    int idx = _ref[sockfd];
    _heap[idx]._expire = t;
    down(idx);
}

void SmallHeap::clear()
{
    _heap.clear();
    _ref.clear();
}

void SmallHeap::tick()
{
    if (_heap.size() <= 1)
    {
        return;
    }

    while (_heap.size() > 1)
    {
        time_t cur = time(NULL);
        if (_heap[1]._expire > cur)
        {
            break;
        }
        delWork(_heap[1]._sockfd);
    }
}

bool SmallHeap::find(int sockfd)
{
    return _ref.count(sockfd);
}

bool SmallHeap::delWork(int sockfd)
{
    if (_ref.count(sockfd) == 0)
    {
        return false;
    }
    int idx = _ref[sockfd];
    _heap[idx].cb_func(sockfd);
    LOG_INFO("close fd:%d", sockfd);
    del(idx);
    return true;
}

void Util::init(int timeout, int close_log)
{
    _TIMEOUT = timeout;
    _queue._close_log = close_log;
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
void cb_func(int sockfd)
{
    assert(sockfd != -1);
    epoll_ctl(Util::_epollfd, EPOLL_CTL_DEL, sockfd, NULL);
    close(sockfd);
    http_conn::_user_count --;
}