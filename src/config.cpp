#include "config.hpp"

Conf::Conf()
{
    port = 10000;
    promode = 0;
    thread_num = 8;
    trig_mode = 0;
    sql_num = 8;
    close_log = 0;
}

void Conf::parse(int argc, char** argv)
{
    
}