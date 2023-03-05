#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <cstring>
#include "locker.hpp"
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string>

class Log
{
public:
    static Log *getinstance()
    {
        static Log instance;
        return &instance;
    }
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, int lines = 5000000);
    void write_log(int level, const char *format, ...);
    void flush(void);

private:
    Log();
    virtual ~Log();

    char _dir_name[100];    // 路径名
    char _log_name[100];    // log文件名
    int _max_line;  // 日志最大行数
    int _log_buf_size;  // 日志缓冲区大小
    long long _count;   // 日志行数记录
    int _today;   // 日志是按天分类，记录当前时间的天数
    FILE *_fp;  // 打开log文件的指针
    char *_buf; // 缓冲区
    locker _locker; // 互斥锁
    int _close_log; // 关闭日志
};

#define LOG_DEBUG(format, ...) if (_close_log == 0) { Log::getinstance()->write_log(0, format, ##__VA_ARGS__); Log::getinstance()->flush(); }
#define LOG_INFO(format, ...) if (_close_log == 0) { Log::getinstance()->write_log(1, format, ##__VA_ARGS__); Log::getinstance()->flush(); }
#define LOG_WARN(format, ...) if (_close_log == 0) { Log::getinstance()->write_log(2, format, ##__VA_ARGS__); Log::getinstance()->flush(); }
#define LOG_ERROR(format, ...) if (_close_log == 0) { Log::getinstance()->write_log(3, format, ##__VA_ARGS__); Log::getinstance()->flush(); }

#endif