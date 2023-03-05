#include "config.hpp"
#include "webserver.hpp"
#include <string>

int main(int argc, char** argv)
{
    std::string user = "root";
    std::string password = "123456";
    std::string dbname = "yourdb";
    Conf conf;
    WebServer server;
    server.init(conf.promode, conf.port, conf.thread_num, conf.trig_mode, conf.sql_num, user, password, dbname, conf.close_log);
    server.EpollListen();
    server.EventLoop();
    return 0;
}