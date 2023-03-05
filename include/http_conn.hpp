#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "locker.hpp"
#include "sql_conn_pool.hpp"
#include <unordered_map>
#include "log.hpp"

class http_conn
{
public:
    static const int MAX_NAME_SIZE = 200;
    static const int MAX_READ_SIZE = 1000;
    static const int MAX_WRITE_SIZE = 2000;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() { }
    ~http_conn() { }
    void init(int fd, const sockaddr_in &addr, int trig_mode, int close_log);
    void close_conn();
    bool read();
    bool write();
    void process();
    void sql_init_result(conn_pool *pool);
    sockaddr_in *get_address() { return &_addr; }

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    char *get_line() { return _readBuf + _start_line; }
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_request_headers(char* text);
    HTTP_CODE parse_request_content(char* text);
    HTTP_CODE do_request();
    void unmap();
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content(const char* content);
    bool add_linger();
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_blank_line();
public:
    
    static int _epollfd;
    static int _user_count;
    int _state;
    int _improv;
    int _timer_flag;
    MYSQL* _conn;
private:
    int _sockfd;    
    struct sockaddr_in _addr;

    char _readBuf[MAX_READ_SIZE];
    int _read_idx;
    int _check_idx;
    int _start_line;
    char _writeBuf[MAX_WRITE_SIZE];
    int _write_idx;
    char _doc_root[MAX_NAME_SIZE];
    METHOD _method;
    char* _url;
    char* _version;
    CHECK_STATE _check_state;
    int _content_length;
    char* _host;
    bool _linger;
    char* _file_address;
    struct stat _file_stat;
    char _real_file[MAX_NAME_SIZE];
    struct iovec _iv[2];
    int _iv_count;
    int _trig_mode;
    int _close_log;
    int _cgi;
    std::string _msg;

    int _bytes_to_send;
    int _bytes_have_send;
};


#endif