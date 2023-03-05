#ifndef SQLCONNECTIONPOOL_H
#define SQLCONNECTIONPOOL_H

#include <mysql/mysql.h>
#include <string.h>
#include <string>
#include <list>
#include "locker.hpp"
#include "log.hpp"

class conn_pool
{
public:
    static conn_pool* getInstance()
    {
        static conn_pool instance;
        return &instance;
    }
    void init(int sql_num, std::string host, std::string user, std::string password, std::string dbname, int port, int close_log);
    MYSQL* getConn();
    bool releaseConn(MYSQL*);
    void destroyPool();
    int getFreeNum();
private:
    conn_pool();
    ~conn_pool();

    int _sql_num;
    int _free_num;
    int _cur_num;
    locker _locker;
    sem _sem;
    std::list<MYSQL *> _queue;

public:
    std::string _host;
    std::string _user;
    std::string _password;
    std::string _dbname;
    int _port;
    int _close_log;
};

class conn_sql
{
public:
    conn_sql(MYSQL**, conn_pool *);
    ~conn_sql();

private:
    MYSQL* _conn;
    conn_pool *_pool;
};

#endif