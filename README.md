# TinyWebServer

基于LinuxC++实现的C++轻量级服务器

## 功能
* 使用I/O多路复用Epoll和线程池实现(Reactor和模拟Proactor)高并发模型;
* 使用状态机解析HTTP请求报文，支持解析GET和POST请求;
* 基于小根堆实现的定时器，关闭超时的非法连接;
* 使用单例模式实现了日志系统，记录服务器的运行状态;
* 使用RAII机制实现了数据库线程池，减少数据库连接建立和关闭的开销，同时实现了用户注册登录功能;

## 环境要求
* Linux
* mysql

## 项目启动
需要建立一个数据库和表
```
// 建立一个数据库
create database yourdb;

// 建立user表
use yourdb; // 进入到yourdb数据库

create table if not exists(
username char(20) NULL primary key,
password char(20) NULL
);
```

有两种方法启动
1. 直接运行run文件，如果run不是可执行文件，需要给run文件增加可执行的权限`chmod +x run`, 然后运行run文件即可
2. 进入build文件夹下，然后依次执行
> rm * -rf                       
cmake .. && make


## 目前的问题和待实现的功能
1. 当启动服务器连接一个客户端之后，将客户端关闭之后服务器会异常退出
2. 目前只测试了LT + Proactor
3. 后续打算实现一个在线的文件管理的功能
4. 压力测试还没有进行
