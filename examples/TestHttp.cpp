#include <iostream>
#include <string>
#include <mutex>
#include <condition_variable>

#include "SocketLibFunction.h"
#include "HttpServer.h"
#include "HttpFormat.h"
#include "WebSocketFormat.h"

HTTPParser etcdHelp(const std::string& ip, int port, HttpFormat::HTTP_TYPE_PROTOCOL protocol, const std::string& key, const std::string& value, int timeout)
{
    HTTPParser result(HTTP_BOTH);

    std::mutex mtx;
    std::condition_variable cv;

    HttpServer server;
    Timer::WeakPtr timer;
    server.startWorkThread(1);  /*必须为1个线程*/

    sock fd = ox_socket_connect(ip.c_str(), port);
    if (fd != SOCKET_ERROR)
    {
        server.addConnection(fd, [key, value, &mtx, &cv, &server, &timer, timeout, protocol](HttpSession::PTR session){
            /*注册超时定时器*/
            timer = server.getServer()->getService()->getRandomEventLoop()->getTimerMgr().AddTimer(timeout, [session](){
                session->getSession()->postShutdown();
            });

            HttpFormat request;
            request.addHeadValue("Accept", "*/*");
            request.setProtocol(protocol);
            std::string keyUrl = "/v2/keys/";
            keyUrl.append(key);
            request.setRequestUrl(keyUrl.c_str());
            if (protocol == HttpFormat::HTP_PUT)
            {
                request.addParameter("value", value.c_str());
                request.setContentType("application/x-www-form-urlencoded");
            }
            std::string requestStr = request.getResult();
            session->getSession()->send(requestStr.c_str(), requestStr.size());

        }, [&cv, &result, &timer](const HTTPParser& httpParser, HttpSession::PTR session, const char* websocketPacket, size_t websocketPacketLen){
            result = httpParser;
            /*关闭连接,并删除超时定时器*/
            session->getSession()->postShutdown();
            if (timer.lock() != nullptr)
            {
                timer.lock()->Cancel();
            }
        }, [&cv, &timer](HttpSession::PTR session){
            /*收到断开通知,通知等待线程*/
            if (timer.lock() != nullptr)
            {
                timer.lock()->Cancel();
            }
            cv.notify_one();
        });

        auto ul = std::unique_lock<std::mutex>(mtx);
        cv.wait(ul);
    }

    return result;
}

HTTPParser etcdSet(const std::string& ip, int port, const std::string& key, const std::string& value, int timeout)
{
    return etcdHelp(ip, port, HttpFormat::HTP_PUT, key, value, timeout);
}

HTTPParser etcdGet(const std::string& ip, int port, const std::string& key, int timeout)
{
    return etcdHelp(ip, port, HttpFormat::HTP_GET, key, "", timeout);
}

int main(int argc, char **argv)
{
    HttpServer server;

    server.startListen(8080);
    server.startWorkThread(ox_getcpunum());
    server.setEnterCallback([](HttpSession::PTR session){
        session->setRequestCallback([](const HTTPParser& httpParser, HttpSession::PTR session, const char* websocketPacket, size_t websocketPacketLen){
            if (websocketPacket != nullptr)
            {
                std::string sendPayload = "hello";
                std::string sendFrame;
                WebSocketFormat::wsFrameBuild(sendPayload, sendFrame);

                session->getSession()->send(sendFrame.c_str(), sendFrame.size());
            }
            else
            {
                //普通http协议
                HttpFormat httpFormat;
                httpFormat.setProtocol(HttpFormat::HTP_RESPONSE);
                httpFormat.addParameter("<html>hello</html>");
                std::string result = httpFormat.getResult();
                session->getSession()->send(result.c_str(), result.size(), std::make_shared<std::function<void(void)>>([session](){
                    session->getSession()->postShutdown();
                }));
            }
        });
    });

    std::cin.get();

    if (false)
    {
        HTTPParser result = etcdSet("127.0.0.1", 2379, "server/1", "ip:127.0.0.1, port:8888", 5000);
        result = etcdSet("127.0.0.1", 2379, "server/2", "ip:127.0.0.1, port:9999", 5000);
        result = etcdGet("127.0.0.1", 2379, "server", 5000);
    }

    sock fd = ox_socket_connect("192.168.12.128", 8080);
    server.addConnection(fd, [](HttpSession::PTR session){
        HttpFormat request;
        if (false)
        {
            request.addHeadValue("Accept", "*/*");
            request.setProtocol(HttpFormat::HTP_PUT);
            request.setRequestUrl("/v2/keys/asea/aagee");
            request.addParameter("value", "123456");
            request.setContentType("application/x-www-form-urlencoded");
        }
        else
        {
            request.setProtocol(HttpFormat::HTP_GET);
            request.setRequestUrl("/v2/keys/asea/aagee");
            request.addParameter("value", "123456");
        }
        std::string requestStr = request.getResult();
        session->getSession()->send(requestStr.c_str(), requestStr.size());

    }, [](const HTTPParser& httpParser, HttpSession::PTR session, const char* websocketPacket, size_t websocketPacketLen){
        std::cout << httpParser.getBody() << std::endl;
        return;
        /*处理response*/
    }, [](HttpSession::PTR){
    });

    std::cin.get();
}
