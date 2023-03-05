#include "config.hpp"
#include <stdlib.h>

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
    for (int i = 1; i < argc; ++ i)
    {
        if (i == 1)
        {
            port = atoi(argv[i]);
        }
        else if (i == 2)
        {
            promode = atoi(argv[i]);
        }
        else if (i == 3)
        {
            thread_num = atoi(argv[i]);
        }
        else if (i == 4)
        {
            trig_mode = atoi(argv[i]);
        }
        else if (i == 5)
        {
            sql_num = atoi(argv[i]);
        }
        else if (i == 6)
        {
            close_log = atoi(argv[i]);
        }
    }
}