#include "http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


int http_conn::_epollfd = -1;
int http_conn::_count = 0;

//  设置文件描述符非阻塞
void setnonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if( one_shot ) {
        // 防止同一次通信被不同的线程处理
        event.events |= EPOLLONESHOT | EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 从epoll中删除监听的文件描述符
void delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

// 初始化新接收的连接
void http_conn::init(int fd, const sockaddr_in& address) {
    _sockfd = fd;
    _address = address;
    // memcpy(&_address, &address, sizeof(address));
    // 端口复用
    int reuse = 1;
    setsockopt( _sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd(_epollfd, fd, true);
    _count ++;
    init();
}

// 初始化除连接外的信息
void http_conn::init() {
    bytes_have_send = 0;
    bytes_to_send = 0;
    _read_idx = 0;
    memset(_readBuf, 0, sizeof _readBuf);
    _read_idx = 0;
    _checked_idx = 0;
    _write_idx = 0;
    _check_start = CHECK_STATE_REQUESTLINE;
    _start_line = 0;
    _method = GET;
    _url = 0;
    _version = 0;
    _host = 0;
    _content_length = 0;
    _linger = false;
    // 获取当前的目录
    getcwd(_doc_root, sizeof _doc_root);
    strcat(_doc_root, "/resources");
  
    memset(_real_file, 0, sizeof _real_file);
    memset(_writeBuf, 0, sizeof _writeBuf);
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if ( _file_address ) {
        munmap(_file_address, _file_state.st_size);
        _file_address = 0;
    }
}

// 非阻塞读
bool http_conn::read() {
    
    if( _read_idx >= MAX_READ_SIZE ) {
        printf("超出范围了\n");
        return false;
    }
    

    int byte = 0;
    while( true ) {
        
        byte = recv(_sockfd, _readBuf + _read_idx, MAX_READ_SIZE - _read_idx, 0);

        if( byte == -1 ) {
            // 读取完了
            if( errno == EAGAIN | errno == EWOULDBLOCK ) {
                break;
            }
            // 发生错误
            perror("recv");
            return false;

        } else if( byte == 0 ) {
            // 已经关闭连接
            printf("client close\n");
            return false;
        }

        _read_idx += byte;

    }
    // printf("读取到的内容为:\n%s\n", _readBuf);

    return true;
}

// 非阻塞写
bool http_conn::write() {
    int temp = 0;

    if ( bytes_to_send == 0 ) {
        // 将要发送的字节数为0，这一次响应结束
        modfd(_epollfd, _sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(_sockfd, _iv, _iv_count);
        if ( temp <= -1 ) {
            if ( errno == EAGAIN ) {
                modfd(_epollfd, _sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if ( bytes_have_send >= _iv[0].iov_len ) {
            _iv[0].iov_len = 0;
            _iv[1].iov_base = _file_address + (bytes_have_send - _write_idx);
            _iv[1].iov_len = bytes_to_send;
        } else {
            _iv[0].iov_base = _writeBuf + bytes_have_send;
            _iv[0].iov_len = _iv[0].iov_len - temp;
        }

        if ( bytes_to_send <= 0 ) {
            // 没有数据要发送了
            unmap();
            modfd(_epollfd, _sockfd, EPOLLIN);

            if ( _linger ) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }

    return true;
}

// 关闭连接 
void http_conn::close_conn() {
    if( _sockfd != -1 ) {
        delfd(_epollfd, _sockfd);
        _sockfd = -1;
        _count --;
    }
}

// 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::pares_line() {

    char temp;
    for( ; _checked_idx < _read_idx; ++ _checked_idx) {

        temp = _readBuf[_checked_idx];
        if( temp == '\r' ) {

            if( (_checked_idx + 1) == _read_idx ) {
                return LINE_OPEN;
            } else if ( _readBuf[_checked_idx + 1] == '\n' ) {
                _readBuf[_checked_idx ++] = '\0';
                _readBuf[_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;

        } else if( temp == '\n' ) {
            

            if( ( _checked_idx > 1 ) && ( _readBuf[_checked_idx - 1] == '\r' ) ) {
                _readBuf[_checked_idx - 1] = '\0';
                _readBuf[_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
            
        }
    }

    return LINE_OK;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {

    strcpy(_real_file, _doc_root);
    int len = strlen( _doc_root );
    // strcat(_real_file, _url);
    strncpy( _real_file + len, _url, MAX_NAME_SIZE - len - 1 );

    
    // 获取_real_file文件的相关信息
    if ( stat(_real_file, &_file_state) != 0 ) {
        return NO_REQUEST;
    }

    // 判断访问权限
    if ( !( _file_state.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR(_file_state.st_mode) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打卡文件
    int fd = open(_real_file, O_RDONLY);

    // 创建内存映射
    _file_address = (char* )mmap(0, _file_state.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() {

    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;
    while( ( (_check_start == CHECK_STATE_CONTENT) && (line_status == LINE_OK) )
         || (line_status = pares_line()) == LINE_OK ) {
        
        // 获取一行数据
        text = getline();
        _start_line = _checked_idx;
        printf("got 1 http line: %s\n", text);
        
        switch( _check_start ) {

            case CHECK_STATE_REQUESTLINE: {

                ret = pares_request_line(text);
                if( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;

            }
            case CHECK_STATE_HEADER: {

                ret = pares_header(text);
                if( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;

            }
            case CHECK_STATE_CONTENT: {

                ret = pares_content(text);
                if( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;

            }
            default: {
                return INTERNAL_ERROR;
            }
        }

    }

    return NO_REQUEST;
}

// 解析请求行，获取请求方法，目标URL，以及HTTP版本号
http_conn::HTTP_CODE http_conn::pares_request_line(char * text) {

    // strpbrk   判断第二个参数中的字符哪个在text中最先出现
    _url = strpbrk(text, " \t");
    if( !_url ) {
        return BAD_REQUEST;
    }

    *_url ++ = '\0';
    char * method = text;
    // strcasecmp   忽略大小写进行比较
    if( strcasecmp(method, "GET") == 0 ) {
        _method = GET;
    } else if( strcasecmp(method, "HEAD") == 0 ) {
        _method = HEAD;
    } else  {
        return BAD_REQUEST;
    }

    _version = strpbrk(_url, " \t");
    if( !_version ) {
        return BAD_REQUEST;
    }
    *_version ++ = '\0';
    if( strcasecmp(_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }


    if( strncasecmp(_url, "http://", 7) == 0 ) {
        _url += 7;
        // char *strchr(const char *str, int c) 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        _url = strchr(_url, '/');
    }

    if( !_url || _url[0] != '/' ) {
        return BAD_REQUEST;
    }

    // 检测状态变成检测请求头
    _check_start = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

// 解析请求头
http_conn::HTTP_CODE http_conn::pares_header(char * text) {

    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求体中有消息，则还需要解析请求体中的内容
        // 状态机转移到CHECK_STATE_HEADER状态
        if( _content_length != 0 ) {
            _check_start = CHECK_STATE_HEADER;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if ( strncasecmp(text, "Host:", 5) == 0 ) {
        // 处理Host头部字段
        text += 5;
        // size_t strspn(const char *str1, const char *str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
        text += strspn(text, "\t");
        _host = text;
    } else if ( strncasecmp(text, "Connection:", 11) == 0 ) {
        // 处理Connection头部字段
        text += 11;
        text += strspn(text, "\t");
        if( strcasecmp(text, "keep-alive") ) {
            _linger = true;
        }
    }  else if ( strncasecmp(text, "Content-Length:", 15) == 0 ) {
        // 处理Connection-Length头部字段
        text += 15;
        text += strspn(text, "\t");
        _content_length = atoi(text);
    }

    return NO_REQUEST;
}

// 解析请求体，我们没有真正的解析HTTP请求的消息体，只是判断它是否被完整读入了
http_conn::HTTP_CODE http_conn::pares_content(char * text) {

    if( _read_idx >= ( _content_length + _checked_idx ) ) {
        text[_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(http_conn::HTTP_CODE ret) {

    switch( ret ) {
        // 表示客户请求语法错误
        case BAD_REQUEST:
            add_response_line(400, error_400_title);
            add_response_head(strlen(error_400_form));
            if ( add_response_content(error_400_form) ) {
                return false;
            }
            break;

        // 表示服务器没有资源
        case NO_RESOURCE:
            add_response_line(404, error_404_title);
            add_response_head(strlen(error_404_form));
            if ( add_response_content(error_404_form) ) {
                return false;
            }
            break;

        // 表示客户对资源没有足够的访问权限
        case FORBIDDEN_REQUEST:
            add_response_line(403, error_403_title);
            add_response_head(strlen(error_403_form));
            if ( add_response_content(error_403_form) ) {
                return false;
            }
            break;

        // 文件请求，获取文件成功
        case FILE_REQUEST:
            add_response_line(200, ok_200_title);
            add_response_head(_file_state.st_size);
            _iv[0].iov_base = _writeBuf;
            _iv[0].iov_len = _write_idx;
            _iv[1].iov_base = _file_address;
            _iv[1].iov_len = _file_state.st_size;

            _iv_count = 2;
            bytes_to_send = _write_idx + _file_state.st_size;

            return true;

        // 表示服务器内部错误
        case INTERNAL_ERROR:
            add_response_line(500, error_500_title);
            add_response_head(strlen(error_500_form));
            if ( add_response_content(error_500_form) ) {
                return false;
            }
            break;

        default:
            return false;

    }

    _iv[0].iov_base = _writeBuf;
    _iv[0].iov_len = _write_idx;
    _iv_count = 1;
    bytes_to_send = _write_idx;

    return true;
}

// 向写缓冲区中发送数据
bool http_conn::add_response(const char* format, ...) {

    if ( _write_idx >= MAX_WRITE_SIZE ) {
        return false;
    }

    va_list ap;
    va_start(ap, format);
    /*
    函数原型：int vsnprintf(char *str, size_t size, const char *format, va_list ap);
    函数说明：将可变参数格式化输出到一个字符数组
    参数：str输出到的数组，size指定大小，防止越界，format格式化参数，ap可变参数列表函数用法
    */
    int len = vsnprintf(_writeBuf + _write_idx, MAX_WRITE_SIZE - _write_idx - 1, format, ap);
    if ( len + _write_idx >= MAX_WRITE_SIZE ) {
        return false;
    }

    _write_idx += len;
    va_end(ap);
    return true;
}

// 响应行   协议版本 状态码 状态码描述
bool http_conn::add_response_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", _version, status, title);
}

// 响应头
bool http_conn::add_response_head(int content_length) {
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

// 响应体
bool http_conn::add_response_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", ( _linger == true)? "keep_alive" : "close" );
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content_length(int contentlength) {
    return add_response("Content-Length: %d\r\n", contentlength);
}


// 处理客户端请求
void http_conn::process() {

    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if( read_ret == NO_REQUEST ) {
        modfd(_epollfd, _sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    if ( !process_write( read_ret ) ) {
        close_conn();
    }
    modfd(_epollfd, _sockfd, EPOLLOUT);
}