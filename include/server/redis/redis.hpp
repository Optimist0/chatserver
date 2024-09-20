#ifndef REDIS_H
#define REDIS_H

#include<hiredis/hiredis.h>
#include<thread>
#include<functional>
using namespace std;

class Redis {
public:
    Redis();
    ~Redis();
    //连接redis服务器
    bool connect();
    //向redis指定通道channel发布消息
    bool publish(int channel, string message);
    //向redis指定通道subscribe订阅消息
    bool subscribe(int channel);
    //向redis指定通道unsubscribe取消订阅消息
    bool unsunscribe(int channel);
    //再独立线程中接受定和约通道中的信息
    void observer_channel_message();
    //初始化向业务成上报通道消息的回到对象
    void init_notify_handler(function<void(int, string)>fn);
private:
    //hiredis同步上下文对象  负责publish消息
    redisContext* _publish_context;
    //hiredis同步上下文对象  负责subscribe消息
    redisContext* _subscribe_context;
    //回调操作 受到订阅的消息  给service层上报
    function<void(int, string)> _notify_message_handler;

};

#endif 