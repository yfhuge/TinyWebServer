#include "threadpool.h"
#include "http_conn.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstdio>
#include <errno.h>
#include <signal.h>

#define MAX_EVENTS_NUMBER 10000     // 监听的最大的事件个数
#define MAX_FD 65536    // 最大的文件描述符个数

extern void addfd(int epollfd, int fd, bool one_shot);
extern void delfd(int epollfd, int fd);

void addsig(int sig, void (handler)(int )) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

int main(int argc, char* argv[])
{
    if(argc <= 1) {
        printf("please input %s port_name\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    addsig(SIGPIPE, SIG_IGN);

    threadPool<http_conn> * pool = nullptr;
    try {
        pool = new threadPool<http_conn>;
    } catch( ... ) {
        return 1;
    }
    
    http_conn* users = new http_conn[MAX_FD];

    // 创建监听套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if( listenfd == -1 ) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    // 设置端口复用
    int reuser = 1;
    int ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuser, sizeof( reuser ));
    if( ret == -1 ) {
        perror("setsockopt");
        return 1;
    }
    // 绑定信息
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof( address ));
    if( ret == -1 ) {
        perror("bind");
        return 1;
    }

    // 设置监听
    ret = listen(listenfd, 5);
    if( ret == -1 ) {
        perror("listen");
        return 1;
    }

    // 创建epoll对象和事件数组
    epoll_event events[MAX_EVENTS_NUMBER];
    int epollfd = epoll_create(5);
    if( epollfd == -1 ) {
        perror("epoll_create");
        return 1;
    }
    http_conn::_epollfd = epollfd;
    addfd(epollfd, listenfd, false);

    while( true ) {
        int number = epoll_wait(epollfd, events, MAX_EVENTS_NUMBER, -1);

        if( (number) < 0 && (errno != EINTR) ) {
            perror("epoll_wait");
            break;
        }

        for(int i = 0; i < number; ++ i) {
            int sockfd = events[i].data.fd;

            if( sockfd == listenfd ) {

                struct sockaddr_in client_address;
                socklen_t client_addlen = sizeof(client_address); 
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addlen);
                
                if( connfd < 0 ) {
                    perror("accept");
                    printf("errno is %d\n", errno);
                    continue;
                }

                if( http_conn::_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }

                users[connfd].init(connfd, client_address);

            } else if( events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP) ) {

                users[sockfd].close_conn();

            } else if( events[i].events & EPOLLIN ) { 

                if( users[sockfd].read() ) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }

            } else if( events[i].events & EPOLLOUT ) {

                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }
                
            }
        }
    }
    close(listenfd);
    close(epollfd);
    delete[] users;
    delete pool;

    return 0;
}