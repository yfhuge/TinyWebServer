#include "sql_conn_pool.hpp"

conn_pool::conn_pool()
{
    _free_num = 0;
    _cur_num = 0;
}

void conn_pool::init(int sql_num, std::string host, std::string user, std::string password, std::string dbname, int port, int close_log)
{
    _host = host;
    _user = user;
    _password = password;
    _dbname = dbname;
    _port = port;
    _sql_num = sql_num;
    _close_log = close_log;

    for (int i = 0; i < sql_num; ++ i)
    {
        MYSQL* conn = NULL;
        conn = mysql_init(conn);

        if (conn == NULL)
        {
            LOG_ERROR("MySQL ERROR");
            exit(-1);
        }
        conn = mysql_real_connect(conn, _host.c_str(), _user.c_str(), _password.c_str(), _dbname.c_str(), _port, NULL, 0);
        if (conn == NULL)
        {
            LOG_ERROR("MySQL ERROR");
            exit(-1);
        }
        _queue.push_back(conn);
        _free_num ++;
    }
    _sem = sem(sql_num);
}

MYSQL* conn_pool::getConn()
{
    MYSQL* conn = NULL;
    if (_queue.size() == 0)
    {
        return conn;
    }

    _sem.wait();
    _locker.lock();
    conn = _queue.front();
    _queue.pop_front();
    ++ _cur_num;    
    ++ _free_num;
    _locker.unlock();
    return conn;
}

bool conn_pool::releaseConn(MYSQL* conn)
{
    if (conn == NULL)
    {
        return false;
    }

    _locker.lock();
    _queue.push_back(conn);
    ++ _free_num;
    -- _cur_num;
    _locker.unlock();
    _sem.post();
    return true;
}

void conn_pool::destroyPool()
{
    _locker.lock();
    if (_queue.size())
    {
        std::list<MYSQL*>::iterator it = _queue.begin();
        for (; it != _queue.end(); ++ it)
        {
            MYSQL* conn = *it;
            mysql_close(conn);
        }
        _free_num = 0;
        _cur_num = 0;
        _queue.clear();
    }
    _locker.unlock();
}

int conn_pool::getFreeNum()
{
    return this->_free_num;
}

conn_pool::~conn_pool()
{
    destroyPool();
}


conn_sql::conn_sql(MYSQL** conn, conn_pool* pool)
{
    *conn = pool->getConn();
    _conn = *conn;
    _pool = pool;
}

conn_sql::~conn_sql()
{
    _pool->releaseConn(_conn);
}