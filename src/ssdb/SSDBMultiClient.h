#ifndef _SSDB_MULTI_CLIENT_H
#define _SSDB_MULTI_CLIENT_H

#include <string>
#include <functional>
#include <unordered_map>
#include <stdint.h>
#include <vector>
#include <thread>
#include <memory>

#include "eventloop.h"
#include "datasocket.h"
#include "msgqueue.h"
#include "SSDBProtocol.h"

class DataSocket;
class SSDBProtocolRequest;
class RedisProtocolRequest;
struct parse_tree;

/*  可链接多个ssdb 服务器的客户端   */

/*  TODO::目前对于ssdb请求是随机选择服务器，后续或许也需要提供重载让使用者去管理链接，以及客户端自定义sharding    */

/*  TODO::目前主要支持ssdb；而redis只支持get/set。且redis和ssdb的转换也没有测试   */

class SSDBMultiClient
{
public:

    typedef SSDBProtocolRequest MyRequestProcotol;

    typedef std::shared_ptr<SSDBMultiClient> PTR;

    typedef std::function<void(const std::string&, const Status&)>  ONE_STRING_CALLBACK;
    typedef std::function<void(const Status&)>                      NONE_VALUE_CALLBACK;
    typedef std::function<void(int64_t, const Status&)>             ONE_INT64_CALLBACK;
    typedef std::function<void(const std::vector<std::string>&, const Status&)> STRING_LIST_CALLBACK;

public:
    SSDBMultiClient();
    ~SSDBMultiClient();

    EventLoop&                                                      getEventLoop();

    void                                                            startNetThread(std::function<void(void)> frameCallback = nullptr);
    /*开启一个异步链接后端服务器, pingTime为ping时间，-1表示不执行ping操作, isAutoConnection为是否自动重连*/
    void                                                            asyncConnection(std::string ip, int port, int pingTime, bool isAutoConnection);
    /*添加一个(已链接的)后端服务器(线程安全)*/
    void                                                            addConnection(sock fd, std::string ip, int port, int pingTime, bool isAutoConnection);

    void                                                            redisSet(const std::string& key, const std::string& value, const NONE_VALUE_CALLBACK& callback);
    void                                                            redisGet(const std::string& key, const ONE_STRING_CALLBACK& callback);

    void                                                            multiSet(const std::unordered_map<std::string, std::string> &kvs, const NONE_VALUE_CALLBACK& callback);
    void                                                            multiGet(const std::vector<std::string> &keys, const STRING_LIST_CALLBACK& callback);
    void                                                            multiDel(const std::vector<std::string> &keys, const NONE_VALUE_CALLBACK& callback);

    void                                                            getset(const std::string& key, const std::string& value, const ONE_STRING_CALLBACK& callback);

    void                                                            set(const std::string& key, const std::string& value, const NONE_VALUE_CALLBACK& callback);
    void                                                            get(const std::string& k, const ONE_STRING_CALLBACK& callback);

    void                                                            hget(const std::string& hname, const std::string& k, const ONE_STRING_CALLBACK& callback);
    void                                                            hset(const std::string& hname, const std::string& k, const std::string& v, const NONE_VALUE_CALLBACK& callback);

    void                                                            multiHget(const std::string& hname, const std::vector<std::string> &keys, const STRING_LIST_CALLBACK& callback);
    void                                                            multiHset(const std::string& hname, const std::unordered_map<std::string, std::string> &kvs, const NONE_VALUE_CALLBACK& callback);

    void                                                            zset(const std::string& name, const std::string& key, int64_t score,
                                                                        const NONE_VALUE_CALLBACK& callback);

    void                                                            zget(const std::string& name, const std::string& key, const ONE_INT64_CALLBACK& callback);

    void                                                            zsize(const std::string& name, const ONE_INT64_CALLBACK& callback);

    void                                                            zkeys(const std::string& name, const std::string& key_start, int64_t score_start, int64_t score_end,
                                                                        uint64_t limit, const STRING_LIST_CALLBACK& callback);

    void                                                            zscan(const std::string& name, const std::string& key_start, int64_t score_start, int64_t score_end,
                                                                        uint64_t limit, const STRING_LIST_CALLBACK& callback);

    void                                                            zclear(const std::string& name, const NONE_VALUE_CALLBACK& callback);

    void                                                            qpush(const std::string& name, const std::string& item, const NONE_VALUE_CALLBACK&);
    void                                                            qpop(const std::string& name, const ONE_STRING_CALLBACK&);
    void                                                            qslice(const std::string& name, int64_t begin, int64_t end, const STRING_LIST_CALLBACK& callback);
    void                                                            qclear(const std::string& name, const NONE_VALUE_CALLBACK& callback);

    void                                                            forceSyncRequest();
    void                                                            pull();
    void                                                            stopService();

private:
    /*投递没有返回值的db请求*/
    void                                                            pushNoneValueRequest(const char* request, size_t len, const NONE_VALUE_CALLBACK& callback);
    /*投递返回值为string的db请求*/
    void                                                            pushStringValueRequest(const char* request, size_t len, const ONE_STRING_CALLBACK& callback);
    /*投递返回值为string list的db请求*/
    void                                                            pushStringListRequest(const char* request, size_t len, const STRING_LIST_CALLBACK& callback);
    /*投递返回值为int64_t的db请求*/
    void                                                            pushIntValueRequest(const char* request, size_t len, const ONE_INT64_CALLBACK& callback);

    parse_tree*                                                     processResponse(const std::string& response);
    void                                                            forgeError(const std::string& error, std::function<void(const std::string&)>& callback);

    void                                                            sendPing(DataSocket::PTR ds);
private:
    std::thread*                                                    mNetThread;
    int64_t                                                         mCallbackNextID;

    std::unordered_map<int64_t, ONE_STRING_CALLBACK>                mOneStringValueCallback;
    std::unordered_map<int64_t, NONE_VALUE_CALLBACK>                mNoValueCallback;
    std::unordered_map<int64_t, ONE_INT64_CALLBACK>                 mOntInt64Callback;
    std::unordered_map<int64_t, STRING_LIST_CALLBACK>               mStringListCallback;

    bool                                                            mRunIOLoop;
    EventLoop                                                       mNetService;
    std::vector<DataSocket::PTR>                                    mBackends;

    /*投递到网络线程的db请求*/
    struct RequestMsg
    {
        RequestMsg()
        {}

        RequestMsg(const std::function<void(const std::string&)>& acallback, const std::string& acontent) : callback(acallback), content(acontent)
        {
        }

        RequestMsg(std::function<void(const std::string&)>&& acallback, std::string&& acontent) : callback(std::move(acallback)), content(std::move(acontent))
        {
        }

        RequestMsg(RequestMsg &&a) : callback(std::move(a.callback)), content(std::move(a.content))
        {
        }

        RequestMsg & operator = (RequestMsg &&a)
        {
            if (this != &a)
            {
                callback = std::move(a.callback);
                content = std::move(a.content);
            }

            return *this;
        }
        std::function<void(const std::string&)> callback;    /*用户线程的异步回调*/
        std::string  content;                                /*请求的协议内容*/
    };

    MsgQueue<RequestMsg>                                            mRequestList;
    MsgQueue<std::function<void(void)>>                             mLogicFunctorMQ;

    MyRequestProcotol*                                              mRequestProtocol;
    SSDBProtocolResponse*                                           mResponseProtocol;
};

#endif