#include "log.hpp"

Log::Log()
{
    _count = 0;
}

Log::~Log()
{
    if (_fp != NULL)
    {
        fclose(_fp);
    }
}

bool Log::init(const char* file_name, int close_log, int buf_size, int lines)
{
    _close_log = close_log;
    _max_line = lines;
    _log_buf_size = buf_size;
    _buf = new char[_log_buf_size];
    memset(_buf, '\0', sizeof(_buf));

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else 
    {
        strcpy(_log_name, p + 1);
        strncpy(_dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%2d_%s", _dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, _log_name);
    }

    _today = my_tm.tm_mday;
    _fp = fopen(log_full_name, "a");
    if (_fp == NULL) 
    {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    _locker.lock();
    _count ++;

    if (_today != my_tm.tm_mday || _count % _max_line == 0)
    {
        char new_log[256] = {0};
        fflush(_fp);
        fclose(_fp);
        char tail[16] = {0};
        snprintf(tail, 15, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", _dir_name, tail, _log_name);
            _count = 0;
            _today = my_tm.tm_mday;
        }
        else 
        {
            snprintf(new_log, 255, "%s%s%s.%lld", _dir_name, tail, _log_name, _count / _max_line);
        }
        _fp = fopen(new_log, "a");
    }
    _locker.unlock();

    va_list vlst;
    va_start(vlst, format);

    std::string log_str;
    _locker.lock();
    int n = snprintf(_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", 
                        my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                         my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(_buf + n, _log_buf_size - n - 1, format, vlst);
    _buf[n + m] = '\n';
    _buf[n + m + 1] = '\0';
    log_str = _buf;
    fputs(log_str.c_str(), _fp);
    _locker.unlock();

    va_end(vlst);
}

void Log::flush(void)
{
    _locker.lock();
    fflush(_fp);
    _locker.unlock();
}