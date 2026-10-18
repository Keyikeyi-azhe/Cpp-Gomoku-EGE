// 【建议阅读 08 / TCP与网络线程】network_transport.h
// NetSocket::queue/pump负责字节分帧；NetClient负责连接、线程及完整消息队列。
// Winsock相关头文件必须早于一般windows.h，避免引入旧Winsock声明而冲突。
// 下一站：server_main.cpp；再读online_ui.h把网络状态与界面串起来。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_NETWORK_TRANSPORT_H
#define GOMOKU_NETWORK_TRANSPORT_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <deque>
#include <vector>
#include <cstring>
#include <sstream>

// 【类型 NetSocket】单条TCP连接的字节缓冲封装。queue添加长度帧，pump尝试收发并拆出完整正文；不判断
// 五子棋规则。没有自定义析构函数，使用者按当前代码路径显式close。
struct NetSocket {
    SOCKET fd;std::string input,output;ULONGLONG received,sent;bool failed;  // fd套接字；input待拆接收字节、output待发送字节；received/sent最近收发毫秒；failed为缓冲错误。
    // 【函数 NetSocket::NetSocket】构造尚未连接的流缓冲对象。
    // 执行顺序：fd=INVALID_SOCKET；received=当前毫秒；sent=0；failed=false；字符串自动为空。
    // 类型定义：network_transport.h:22 (NetSocket)。
    NetSocket():fd(INVALID_SOCKET),received(GetTickCount64()),sent(0),failed(false) {}
    // 【函数 NetSocket::close】关闭套接字并清空收发字节。
    // 执行顺序：若 fd 有效调用 closesocket；设无效；清 input/output。
    // 参数/局部变量：它不自动重置 failed/received，下一次 attach 会重置必要状态。
    // 从上层理解：network_transport.h:125 (NetClient::run)；
    // network_transport.h:41 (NetSocket::attach)；server_main.cpp:22 (main)。
    void close() {if(fd!=INVALID_SOCKET) closesocket(fd);fd=INVALID_SOCKET;input.clear();output.clear();}
    // 【函数 NetSocket::attach】接管 SOCKET s 并设为非阻塞。
    // 执行顺序：先关闭旧连接；清 failed；保存 s；ioctlsocket(FIONBIO)；重设 received。
    // 参数/局部变量：s 是接管的句柄；mode=1 代表非阻塞。
    // 边界与易错点：后续由此对象负责 close。当前代码没有检查 ioctlsocket 的返回值。
    // 从上层理解：network_transport.h:125 (NetClient::run)；
    // server_main.cpp:22 (main)。
    // 首次调用定位：network_transport.h:33 (NetSocket::close)。
    void attach(SOCKET s) {close();failed=false;fd=s;u_long mode=1;ioctlsocket(fd,FIONBIO,&mode);received=GetTickCount64();}
    // 【函数 NetSocket::queue】把一条正文 message 封装到待发送字节缓冲，返回是否成功。
    // 执行顺序：拒绝空正文、>8192字节或总缓冲>65536；用 htonl 得4字节长度；追加长度头与正文。
    // 参数/局部变量：n 为网络序正文长度。
    // 边界与易错点：queue 只入缓冲，不表示已经送达。Windows 中 unsigned long 为4字节，这段实现依赖当前
    // 平台。
    // 从上层理解：network_transport.h:125 (NetClient::run)；
    // server_main.cpp:22 (main)。
    bool queue(const std::string& message) {
        // 语句说明：正文1..8192字节，累计待发不超过65536；超限将连接标记failed。
        if(message.empty() || message.size()>8192 || output.size()+message.size()+4>65536) {failed=true;return false;}
        // 语句说明：Windows的unsigned long为4字节；htonl把正文长度变为网络字节序。
        unsigned long n=htonl(static_cast<unsigned long>(message.size()));
        output.append(reinterpret_cast<const char*>(&n),4);output+=message;return true;  // 长度头和正文一起进发送缓冲区
    }
    // 【阅读提示】TCP是字节流：半个头、半条正文保留到下次；同批多帧则while连续拆。
    // 【函数 NetSocket::pump】尝试发送并接收；把完整正文追加到 messages；false 表示连接不可继续。
    // 执行顺序：先发送 output 的一部分并移除实发字节；最多16次 recv；把新字节追加 input；while 拆完整
    // 帧；遇零长度、超限、断开或超时失败；暂时无数据 WSAEWOULDBLOCK 不算故障。
    // 参数/局部变量：messages 为输出数组引用；n 分别为 send/recv 实际字节数；bytes[4096] 为接收块；
    // iteration 限制本轮读取次数；length 为从包头取出的正文长度。
    // 边界与易错点：send 可能只发一部分。recv 返回0是有序关闭；<0才检查错误码。messages 超256也失败；
    // 这些界限防止缓冲无限增长。
    // 从上层理解：network_transport.h:125 (NetClient::run)；
    // server_main.cpp:22 (main)。
    bool pump(std::vector<std::string>& messages) {
        if(fd==INVALID_SOCKET || failed) return false;  // 连接已经不可用，直接返回假
        if(!output.empty()) {
            int n=send(fd,output.data(),static_cast<int>(output.size()),0);
            if(n>0) {output.erase(0,n);sent=GetTickCount64();}  // n可能小于待发总长；仅删除已发送前缀，余下字节留待下轮。
            else if(n==0 || WSAGetLastError()!=WSAEWOULDBLOCK) return false;
        }
        char bytes[4096];  // 一次最多收 4096 字节
        for(int iteration=0;iteration<16;++iteration) {
            int n=recv(fd,bytes,sizeof(bytes),0);
            if(n==0) return false;  // 对方关闭了连接
            if(n<0) {if(WSAGetLastError()!=WSAEWOULDBLOCK) return false;break;}
            input.append(bytes,n);received=GetTickCount64();  // 把收到的字节追加到缓冲区，并更新「最后收到数据的时刻」
            if(input.size()>65536) return false;  // 接收缓冲超过上限则中止连接；这是资源保护，不据此证明对方主观恶意。
            while(input.size()>=4) {
                // 语句说明：先memcpy避免未对齐读取，再ntohl还原本机整数长度。
                unsigned long length;std::memcpy(&length,input.data(),4);length=ntohl(length);
                if(!length || length>8192) return false;
                // 语句说明：已有合法长度头但正文尚未到齐；保留input等待下次recv。
                if(input.size()<length+4) break;
                // 语句说明：产出一条完整正文并删去头+正文；while可继续拆同一批收到的后续帧。
                messages.push_back(input.substr(4,length));input.erase(0,length+4);
                if(messages.size()>256) return false;
            }
        }  // 本轮接收结束
        // 语句说明：自上次收到任何数据起不到15秒才视为仍存活；这不是等待完整业务回复的超时。
        return GetTickCount64()-received<15000;
    }
};

// 【类型 NetClient】客户端网络工作线程。private部分负责收发循环，public部分供UI提交/取得正文；临界
// 区保护incoming/outgoing，不让后台线程直接操作界面。start成功仅表示线程创建，不等于已连上房间。
class NetClient {
    CRITICAL_SECTION mutex;HANDLE thread;volatile LONG stopFlag;  // mutex 保护消息队列；thread 是后台线程句柄；stopFlag 是停止标志
    std::deque<std::string> incoming,outgoing;  // incoming 是「收到的、等界面取走」；outgoing 是「界面交来的、等发送」
    std::string address,hello;int port;  // 连接目标：地址、端口，以及连接后要发的问候语
    // 【函数 NetClient::push】网络线程把字符串 s 放入 incoming。
    // 执行顺序：进锁；未满256则追加；否则置 stopFlag；离锁。
    // 参数/局部变量：s 是交给UI的完整消息或内部通知。
    // 从上层理解：network_transport.h:125 (NetClient::run)。
    void push(const std::string& s) {EnterCriticalSection(&mutex);if(incoming.size()<256) incoming.push_back(s);else InterlockedExchange(&stopFlag,1);LeaveCriticalSection(&mutex);}
    // 【函数 NetClient::entry】Win32线程静态入口。
    // 执行顺序：将 LPVOID p 转回 NetClient*，调用成员 run，再返回0。
    // 参数/局部变量：p 是 CreateThread 传来的 this。
    // 从上层理解：network_transport.h:196 (NetClient::start)。
    // 类型定义：network_transport.h:98 (NetClient)。
    // 首次调用定位：network_transport.h:125 (NetClient::run)。
    static DWORD WINAPI entry(LPVOID p) {static_cast<NetClient*>(p)->run();return 0;}
    // 【函数 NetClient::run】后台线程的连接与收发主循环。
    // 执行顺序：初始化Winsock；创建非阻塞TCP；connect并用select等最多约8秒；连上后queue hello；每轮取
    // 走outgoing、发心跳、pump、处理PING/PONG与延迟，其他消息push给UI；失败push LOST；退出清理。
    // 参数/局部变量：data 为Winsock信息；sock 为流封装；fd 为原句柄；target 为IPv4地址结构；rc/error
    // 为connect结果和错误码；connected 是否完成；start 为等待开始毫秒；writeSet/errorSet 为select集合；
    // wait 是10毫秒超时；n 为select结果；e/len 为SO_ERROR值及长度；ping 为最近本机心跳时间；pending 为
    // 本轮出站消息副本；ok/alive 为入缓冲及连接结果；request 为PING输出流；messages 为收到的正文；i 为
    // 索引；stamp/value 为PONG时间解析流与值；latency 为内部LATENCY通知流。
    // 边界与易错点：ping 时间由本机生成并由服务器原样回传，因此测的是往返时间，不需要两台机器时钟一致。
    // 线程每轮 Sleep(10)，没有进行图形绘制。
    // 从上层理解：network_transport.h:113 (NetClient::entry)。
    void run() {
        // 首次调用定位：network_transport.h:106 (NetClient::push)。
        WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)) {push("ERROR SOCKET");return;}
        // 类型定义：network_transport.h:22 (NetSocket)。
        NetSocket sock;SOCKET fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);  // 创建一个 TCP 套接字
        if(fd==INVALID_SOCKET) {push("ERROR SOCKET");WSACleanup();return;}  // 创建失败就通知界面并释放 Winsock
        // 首次调用定位：network_transport.h:41 (NetSocket::attach)。
        sock.attach(fd);sockaddr_in target;ZeroMemory(&target,sizeof(target));target.sin_family=AF_INET;target.sin_port=htons(static_cast<u_short>(port));
        target.sin_addr.s_addr=inet_addr(address.c_str());  // 把点分十进制形式的 IP 字符串转成网络地址
        int rc=connect(fd,reinterpret_cast<sockaddr*>(&target),sizeof(target));
        bool connected=rc==0;int error=WSAGetLastError();
        // 首次调用定位：network_transport.h:33 (NetSocket::close)。
        if(rc && error!=WSAEWOULDBLOCK && error!=WSAEINPROGRESS) {push("ERROR CONNECT");sock.close();WSACleanup();return;}
        ULONGLONG start=GetTickCount64();  // 记下开始等连接的时刻，用来判断是否超时
        // 【阅读提示】非阻塞connect的暂未完成不是失败；select等可写，再用SO_ERROR确认成功。
        while(!connected && !InterlockedCompareExchange(&stopFlag,0,0) && GetTickCount64()-start<8000) {
            fd_set writeSet,errorSet;FD_ZERO(&writeSet);FD_ZERO(&errorSet);FD_SET(fd,&writeSet);FD_SET(fd,&errorSet);
            timeval wait={0,10000};int n=select(0,NULL,&writeSet,&errorSet,&wait);
            if(n>0) {int e=0,len=sizeof(e);getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&e),&len);if(e || FD_ISSET(fd,&errorSet)) break;connected=true;}
            if(n<0) break;  // select 返回负数表示出错，同样跳出
        }
        if(!connected) {if(!stopFlag) push("ERROR CONNECT");sock.close();WSACleanup();return;}
        // 首次调用定位：network_transport.h:49 (NetSocket::queue)。
        sock.queue(hello);ULONGLONG ping=0;
        while(!InterlockedCompareExchange(&stopFlag,0,0)) {
            // 语句说明：锁内只交换消息队列；pending由本线程独享，后续封包无需占用UI的队列锁。
            EnterCriticalSection(&mutex);std::deque<std::string> pending;pending.swap(outgoing);LeaveCriticalSection(&mutex);
            bool ok=true;for(size_t i=0;i<pending.size();++i) if(!sock.queue(pending[i])) ok=false;
            if(GetTickCount64()-ping>3000) {ping=GetTickCount64();std::ostringstream request;request<<"PING "<<ping;sock.queue(request.str());}
            // 首次调用定位：network_transport.h:66 (NetSocket::pump)。
            std::vector<std::string> messages;bool alive=ok && sock.pump(messages);
            for(size_t i=0;i<messages.size();++i) {  // 逐条处理收到的消息
                if(messages[i]=="PING") {sock.queue("PONG");continue;}  // 对方的 PING 要立刻回 PONG，保持这条链路活着
                if(messages[i].compare(0,5,"PONG ")==0) {
                    std::istringstream stamp(messages[i].substr(5));ULONGLONG value=0;  // 把时间戳那部分单独取出来解析
                    if(stamp>>value && value==ping && value<=GetTickCount64()) {
                        // 语句说明：回传的是本机先前发出的毫秒值，差值为RTT，不依赖服务端时钟同步。
                        std::ostringstream latency;latency<<"LATENCY "<<(GetTickCount64()-value);push(latency.str());
                    }  
                } else if(messages[i]!="PONG") push(messages[i]);
            }  // 一轮收发结束
            if(!alive) {push("LOST");break;}
            Sleep(10);
        }  // 循环结束，关闭连接并释放 Winsock
        sock.close();WSACleanup();
    }
public:
    // 【函数 NetClient::NetClient】构造客户端通信对象。
    // 执行顺序：thread=NULL，stopFlag=0，port=0；InitializeCriticalSection 建锁。
    // 类型定义：network_transport.h:98 (NetClient)。
    NetClient():thread(NULL),stopFlag(0),port(0) {InitializeCriticalSection(&mutex);}
    // 【函数 NetClient::~NetClient】对象销毁时释放线程和锁。
    // 执行顺序：先 stop 等线程退出，再 DeleteCriticalSection。
    // 参数/局部变量：析构函数名以 ~ 开头，随对象生命周期自动调用。
    // 类型定义：network_transport.h:98 (NetClient)。
    // 首次调用定位：network_transport.h:186 (NetClient::stop)。
    ~NetClient() {stop();DeleteCriticalSection(&mutex);}
    // 【函数 NetClient::stop】停止并回收后台线程，再清队列。
    // 执行顺序：原子置 stopFlag；有线程则等待结束、关句柄、清NULL；在锁保护下清 incoming/outgoing。
    // 从上层理解：network_transport.h:196 (NetClient::start)；
    // network_transport.h:181 (NetClient::~NetClient)；online_ui.h:67 (onlineStop)。
    void stop() {
        InterlockedExchange(&stopFlag,1);  // 置停止标志，通知后台线程退出
        if(thread) {WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=NULL;}  // INFINITE 表示「一直等到线程真正结束」
        EnterCriticalSection(&mutex);incoming.clear();outgoing.clear();LeaveCriticalSection(&mutex);  // 清空两个队列，避免下次启动时读到上次的残留消息
    }
    // 【函数 NetClient::start】用 ip、p、greeting 启动连接任务，返回建线程是否成功。
    // 执行顺序：先 stop 旧任务；保存地址、端口、握手；清停止标志；CreateThread(entry,this)。
    // 参数/局部变量：ip 为点分十进制IPv4；p 是端口，不是座位；greeting 为HELLO正文。
    // 边界与易错点：true 只表示线程建成，不表示已连接成功；连接成功需UI收到 WELCOME。
    // 从上层理解：online_ui.h:465 (updateOnline)。
    bool start(const std::string& ip,int p,const std::string& greeting) {
        // 首次调用定位：network_transport.h:186 (NetClient::stop)。
        stop();address=ip;port=p;hello=greeting;InterlockedExchange(&stopFlag,0);
        // 首次调用定位：network_transport.h:113 (NetClient::entry)。
        thread=CreateThread(NULL,0,entry,this,0,NULL);return thread!=NULL;  // true仅表示线程创建成功；连接/握手的成功或失败通过incoming消息报告。
    }
    // 【函数 NetClient::sendMessage】UI提交一条出站 message。
    // 执行顺序：在临界区内，outgoing 未满256就追加。
    // 参数/局部变量：当前实现队列满时静默丢弃，没有返回失败；不要理解成每次调用都确保消息入队。
    // 从上层理解：online_ui.h:194 (onlineSaveStatus)；online_ui.h:146 (onlineSend)；
    // online_ui.h:465 (updateOnline)。
    // 语句说明：出站队列最多256条；当前实现满时不追加，且没有返回值报告丢弃。
    void sendMessage(const std::string& message) {EnterCriticalSection(&mutex);if(outgoing.size()<256) outgoing.push_back(message);LeaveCriticalSection(&mutex);}
    // 【函数 NetClient::take】UI非阻塞取一条 incoming，经 message 输出。
    // 执行顺序：进锁检查 found；有消息取front并pop_front；离锁，返回 found。
    // 参数/局部变量：found 为队列非空判断。
    // 边界与易错点：此处“不阻塞等待消息”不等于完全不等待：争用临界区时仍可能短暂等待锁。
    // 从上层理解：online_ui.h:465 (updateOnline)。
    bool take(std::string& message) {
        EnterCriticalSection(&mutex);bool found=!incoming.empty();
        if(found) {message=incoming.front();incoming.pop_front();}LeaveCriticalSection(&mutex);return found;  // 队列为空时 found 为假，调用者据此知道暂时没有消息
    }
};
#endif
