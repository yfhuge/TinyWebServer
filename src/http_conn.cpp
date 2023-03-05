#include "http_conn.hpp"

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int http_conn::_epollfd = -1;
int http_conn::_user_count = 0;

std::unordered_map<std::string, std::string> users;
locker _locker;

void setnonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

void addfd(int epollfd, int fd, bool oneshot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (trig_mode == 1)
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

void modfd(int epollfd, int fd, int ev, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT;
    if (trig_mode)
    {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void delfd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

void http_conn::sql_init_result(conn_pool *pool)
{
    _conn = NULL;
    conn_sql _sql_con(&_conn, pool);
    if (mysql_query(_conn, "select * from user;"))
    {
        LOG_ERROR("select error:%s", mysql_error(_conn));
    }

    MYSQL_RES *res = mysql_store_result(_conn);
    if (res != NULL)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != NULL)
        {
            users.insert({row[0], row[1]});
        }
        mysql_free_result(res);
    }
}

void http_conn::init(int fd, const sockaddr_in &addr, int trig_mode, int close_log)
{
    _sockfd = fd;
    _addr = addr;
    _trig_mode = trig_mode;
    _close_log = close_log;
    addfd(_epollfd, _sockfd, true, trig_mode);
    _user_count++;

    init();
}

void http_conn::init()
{
    _conn = NULL;
    _bytes_have_send = 0;
    _bytes_to_send = 0;
    _check_state = CHECK_STATE_REQUESTLINE;
    _url = 0;
    _version = 0;
    _method = GET;
    _host = 0;
    _read_idx = 0;
    _check_idx = 0;
    _start_line = 0;
    _write_idx = 0;
    _content_length = 0;
    _linger = false;
    _state = 0;
    _cgi = 0;
    _improv = 0;
    _timer_flag = 0;

    memset(_readBuf, '\0', MAX_READ_SIZE);
    memset(_writeBuf, '\0', MAX_WRITE_SIZE);
    memset(_real_file, '\0', MAX_NAME_SIZE);
    getcwd(_doc_root, MAX_NAME_SIZE);
    strcat(_doc_root, "/../resource");
}

void http_conn::close_conn()
{
    if (_sockfd != -1)
    {
        _user_count--;
        _sockfd = -1;
        delfd(_epollfd, _sockfd);
    }
}

bool http_conn::read()
{
    if (_read_idx >= MAX_READ_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    if (_trig_mode == 0)
    {
        // LT读取数据
        bytes_read = recv(_sockfd, _readBuf + _read_idx, MAX_READ_SIZE - _read_idx - 1, 0);

        if (bytes_read <= 0)
        {
            return false;
        }
        _read_idx += bytes_read;

        return true;
    }
    else
    {
        // ET读数据
        while (1)
        {
            bytes_read = recv(_sockfd, _readBuf + _read_idx, MAX_READ_SIZE - _read_idx, 0);
            if (bytes_read < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            _read_idx += bytes_read;
        }
        return true;
        
    }
}

bool http_conn::write()
{
    int tmp = 0;
    if (_bytes_to_send == 0)
    {
        modfd(_epollfd, _sockfd, EPOLLIN, _trig_mode);
        init();
        return true;
    }

    while (1)
    {
        tmp = writev(_sockfd, _iv, _iv_count);

        if (tmp < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                modfd(_epollfd, _sockfd, EPOLLOUT, _trig_mode);
                return true;
            }
            unmap();
            return false;
        }

        _bytes_have_send += tmp;
        _bytes_to_send -= tmp;
        if (_bytes_have_send >= _iv[0].iov_len)
        {
            _iv[0].iov_len = 0;
            _iv[1].iov_base = _file_address + (_bytes_have_send - _write_idx);
            _iv[0].iov_len = _bytes_to_send;
        }
        else
        {
            _iv[0].iov_base = _writeBuf + _bytes_have_send;
            _iv[0].iov_len = _iv[0].iov_len - _bytes_have_send;
        }

        if (_bytes_to_send <= 0)
        {
            unmap();
            modfd(_epollfd, _sockfd, EPOLLIN, _trig_mode);

            if (_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }

    return true;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();
        _start_line = _check_idx;
        LOG_INFO("%s", text);
        switch (_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_request_headers(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_request_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    for (; _check_idx < _read_idx; ++_check_idx)
    {
        tmp = _readBuf[_check_idx];
        if (tmp == '\r')
        {
            if ((_check_idx + 1 == _read_idx))
            {
                return LINE_OPEN;
            }
            else if (_readBuf[_check_idx + 1] == '\n')
            {
                _readBuf[_check_idx++] = '\0';
                _readBuf[_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (_check_idx > 1 && _readBuf[_check_idx - 1] == '\r')
            {
                _readBuf[_check_idx - 1] = '\0';
                _readBuf[_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    _url = strpbrk(text, " \t");
    if (!_url)
    {
        return BAD_REQUEST;
    }

    *_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        _method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        _method = POST;
        _cgi = 1;
    }
    else 
    {
        return BAD_REQUEST;
    }

    _url += strspn(_url, " \t");
    _version = strpbrk(_url, " \t");
    if (!_version)
    {
        return BAD_REQUEST;
    }

    *_version++ = '\0';
    if (strcasecmp(_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(_url, "http://", 7) == 0)
    {
        _url += 7;
        _url = strchr(_url, '/');
    }

    if (!_url || _url[0] != '/')
    {
        return BAD_REQUEST;
    }
    if (strlen(_url) == 1)
    {
        strcat(_url, "judge.html");
    }
    _check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (_content_length != 0)
        {
            _check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        _host = text;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            _linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        _content_length = atol(text);
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_content(char *text)
{
    if (_read_idx >= _check_idx + _content_length)
    {
        text[_content_length] = '\0';
        _msg = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(_real_file, _doc_root);
    int len = strlen(_real_file);

    const char *p = strrchr(_url, '/');

    if (_cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char name[100], password[100], repassword[100];
        int i;
        for (i = 9; _msg[i] != '&'; ++ i)
        {
            name[i - 9] = _msg[i];
        }
        name[i - 9] = '\0';

        int j = 0;
        for (i = i + 10; _msg[i] != '&' && _msg[i] != '\0'; ++ i, ++ j)
        {
            password[j] = _msg[i];
        }
        password[j] = '\0';



        if (*(p + 1) == '3')
        {
            int k = 0;
            for (i = i + 12; _msg[i] != '\0'; ++ i, ++ k)
            {
                repassword[k] = _msg[i];
            }
            repassword[k] = '\0';

            if (std::string(password) != std::string(repassword))
            {
                strcpy(_url, "/registerError1.html");
            }
            else 
            {
                char *sql = (char*)malloc(sizeof(char) * 200);
                sprintf(sql, "insert into user values('%s', '%s');", name, password);

                if (users.find(name) == users.end())
                {
                    _locker.lock();
                    int res = mysql_query(_conn, sql);
                    users.insert({name, password});
                    _locker.unlock();

                    if (!res)
                    {
                        strcpy(_url, "/login.html");
                    }
                    else 
                    {
                        strcpy(_url, "/registerError2.html");
                    }
                }
                else 
                {
                    strcpy(_url, "/registerError2.html");
                }
            }
        }
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
            {
                strcpy(_url, "/welcome.html");
            }
            else 
            {
                if (users.find(name) == users.end())
                {
                    strcpy(_url, "/loginError1.html");
                }
                else 
                {
                    strcpy(_url, "/loginError2.html");
                }
            }
        }
    }

    if (*(p + 1) == '0')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/register.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/login.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else 
    {
        strncpy(_real_file + len, _url, MAX_NAME_SIZE - 1 - len);
    }


    if (stat(_real_file, &_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    if (!(_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    int fd = open(_real_file, O_RDONLY);
    _file_address = (char *)mmap(0, _file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (_file_address)
    {
        munmap(_file_address, _file_stat.st_size);
        _file_address = 0;
    }
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (_file_stat.st_size != 0)
        {
            add_headers(_file_stat.st_size);
            _iv[0].iov_base = _writeBuf;
            _iv[0].iov_len = _write_idx;
            _iv[1].iov_base = _file_address;
            _iv[1].iov_len = _file_stat.st_size;
            _iv_count = 2;
            _bytes_to_send = _write_idx + _file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
        }
    }
    default:
        return false;
    }
    _iv[0].iov_base = _writeBuf;
    _iv[0].iov_len = _write_idx;
    _iv_count = 1;
    _bytes_to_send = _write_idx;
    return true;
}

bool http_conn::add_response(const char *format, ...)
{
    if (_write_idx >= MAX_WRITE_SIZE)
    {
        return false;
    }
    va_list vast;
    va_start(vast, format);
    int len = vsnprintf(_writeBuf + _write_idx, MAX_WRITE_SIZE - 1 - _write_idx, format, vast);
    if (len >= MAX_WRITE_SIZE - 1 - _write_idx)
    {
        va_end(vast);
        return false;
    }
    _write_idx += len;
    va_end(vast);

    LOG_INFO("request:%s", _writeBuf);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int length)
{
    return add_content_length(length) && add_linger() && add_blank_line();
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (_linger) ? "keep-alive" : "close");
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content_length(int lenght)
{
    return add_response("Content-Length:%d\r\n", lenght);
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(_epollfd, _sockfd, EPOLLIN, _trig_mode);
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(_epollfd, _sockfd, EPOLLOUT, _trig_mode);
}