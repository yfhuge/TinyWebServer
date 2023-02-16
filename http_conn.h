#ifndef _HTTPCONNECTION_H_
#define _HTTPCONNECTION_H_

#include <arpa/inet.h>
#include <unistd.h>
#include "threadpool.h"
#include <cstring>
#include <cstdio>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

class http_conn {
public:
    // 读缓冲区的大小
    static const int MAX_READ_SIZE = 2048;
    // 写缓冲区的大小
    static const int MAX_WRITE_SIZE = 1024;
    // 文件路径名的大小
    static const int MAX_NAME_SIZE = 200;

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };


public:
    // 初始化新接收的连接
    void init(int fd, const sockaddr_in& address);
    // 非阻塞读
    bool read();
    // 非阻塞写
    bool write();
    // 关闭连接
    void close_conn();
    // 处理客户端请求
    void process();


public:
    http_conn() { }
    ~http_conn() { } 

public:
    // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态
    static int _epollfd; 
    // 统计用户的数量
    static int _count;

private:
    // 初始化除连接外的信息;
    void init(); 
    // 主状态机，解析请求
    HTTP_CODE process_read();
    // 解析请求行
    HTTP_CODE pares_request_line(char * text);
    // 解析请求头
    HTTP_CODE pares_header(char * text);
    // 解析请求体
    HTTP_CODE pares_content(char * text);
    // 从状态机，解析请求一行
    LINE_STATUS pares_line();
    // 获取一行的信息
    char* getline() { return _readBuf + _start_line; }
    HTTP_CODE do_request();
    // 填充HTTP应答
    bool process_write(HTTP_CODE ret);
    // 向写缓冲区中待发送数据
    bool add_response(const char* format, ...);
    // 写入响应行信息
    bool add_response_line(int status, const char* title);
    // 写入响应头信息
    bool add_response_head(int content_length);
    // 写入响应体信息
    bool add_response_content(const char* content);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    // 对内存映射区执行munmap操作
    void unmap();

private:
    // 该HTTP连接的socket和对方的socket地址
    int _sockfd; 
    struct sockaddr_in _address;

    // 读缓冲区
    char _readBuf[MAX_READ_SIZE];

    // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int _read_idx;

    // 当前正在分析的字符在读缓冲区中的位置
    int _checked_idx;

    // 当前正在解析行的起始位置
    int _start_line;

    // 主状态机当前所处的状态
    CHECK_STATE _check_start;

    // 请求方法
    METHOD _method;

    // 客户请求的目标文件的文件名
    char* _url;

    // HTTP协议版本号，我们仅支持HTTP1.1
    char* _version;

    // 主机名
    char* _host;

    // HTTP请求的消息总长度
    int _content_length;

    // HTTP请求是否要求保持连接
    bool _linger;

    // 网站的根目录
    char _doc_root[MAX_NAME_SIZE];

    // 客户请求的目标文件的完整路径
    char _real_file[MAX_NAME_SIZE];

    // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct stat _file_state;

    // 客户请求的目标文件被mmap到内存中的起始位置
    char* _file_address;

    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec _iv[2];
    int _iv_count;

    // 将要发送的数据字节数
    int bytes_to_send;

    // 已经发送的字节数
    int bytes_have_send;

    // 写缓冲区
    char _writeBuf[MAX_WRITE_SIZE];

    // 写缓冲区中待发送的字节数
    int _write_idx;


};

#endif