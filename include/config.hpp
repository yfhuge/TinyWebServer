#ifndef CONFIG_H
#define CONFIG_H

class Conf
{
public:
    Conf();
    void parse(int argc, char** argv);
public:
    int port;  // 端口号
    int promode;   // 事件模式
    int thread_num;    // 线程池的线程数量
    int trig_mode;  // 触发模式
    int sql_num;    // 数据库线程池的线程数量
    int close_log;  // 关闭日志
};

#endif