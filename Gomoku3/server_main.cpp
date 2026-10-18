// 【建议阅读 09 / 独立服务端入口】server_main.cpp
// main初始化 -> Peer连接槽 -> HELLO握手 -> NetState::apply -> STATE广播 -> 退出确认。
// 本文件与客户端main.cpp各有一个main，分别生成GomokuServer.exe与Gomoku.exe。
// 下一站：online_ui.h；从onlineEnter到updateOnline，再跟一次onlineMove。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
#define NOMINMAX
#define _WIN32_WINNT 0x0601  // 指定目标系统版本；不定义它，Windows 的一些较新接口会取不到
#include "network_transport.h"
#include "network_protocol.h"
#include <cstdlib>
#include <cstdio>

// 【函数 main（服务端）】独立服务进程入口；管理监听、握手、权威状态和广播。
// 执行顺序：验证参数；OpenProcess观察父进程；初始化Winsock并绑定监听；通知就绪事件；循环accept、
// pump、握手或apply、检测断线、确认离开、广播与心跳；最后关闭全部资源。
// 参数/局部变量：全部局部量在下面对应声明处说明；返回2为参数错误、3为Winsock失败、4为绑定/监听失败、
// 0为循环正常结束。
// 边界与易错点：这个 main 与客户端 main 属于两个不同exe。房主客户端退出后服务端观察父句柄退出，避
// 免留下后台房间。
int main(int argc,char** argv) {
    // 语句说明：argc必须为4：程序名也算一项；缺少端口、口令或父进程PID即退出。
    if(argc!=4) return 2;
    // 语句说明：argv[1]端口；argv[2]房主口令；argv[3]父进程PID；argc包含exe本身。
    int port=std::atoi(argv[1]);std::string token=argv[2];DWORD parent=static_cast<DWORD>(std::strtoul(argv[3],NULL,10));
    if(port<1 || port>65535 || token.empty()) return 2;
    HANDLE parentProcess=OpenProcess(SYNCHRONIZE,FALSE,parent);  // 用父进程编号打开它的句柄，只申请「等待它结束」这一项权限
    // 语句说明：data接收Winsock信息；MAKEWORD(2,2)请求Winsock 2.2，非零返回值表示初始化失败。
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)) return 3;
    // 语句说明：listener只用于接受新连接；实际读写交给accept返回的连接socket。
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    // 语句说明：独占监听端口；按此Windows选项避免其他socket共享同一绑定。
    BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
    // 语句说明：address为IPv4监听地址；INADDR_ANY表示所有本机网卡，端口用htons转为网络序。
    sockaddr_in address;ZeroMemory(&address,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=htons(static_cast<u_short>(port));
    // 语句说明：先bind绑定地址，再listen监听；任一步失败则清理资源并返回4。
    if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || listen(listener,8)) {
        closesocket(listener);WSACleanup();if(parentProcess) CloseHandle(parentProcess);return 4;
    }
    // 语句说明：eventName必须与房主创建的就绪事件名相同，使客户端知道何时可以connect。
    std::string eventName="Local\\GomokuReady-"+token;
    // 语句说明：readyEvent是父进程创建的事件句柄；服务端申请将它置为有信号的权限。
    HANDLE readyEvent=OpenEventA(EVENT_MODIFY_STATE,FALSE,eventName.c_str());
    // 语句说明：只有bind/listen成功后才通知就绪；通知后关闭本进程持有的事件句柄。
    if(readyEvent) {SetEvent(readyEvent);CloseHandle(readyEvent);}
    // 语句说明：mode=1使listener非阻塞；accept暂时没人接入时直接返回，不冻结整个循环。
    u_long mode=1;ioctlsocket(listener,FIONBIO,&mode);
    // 【类型 Peer】服务端main内部的连接槽类型。socket为network_transport.h:22 (NetSocket)；
    // player=-1未握手，0/1为座位；reject表示等待错误包发出后关闭；accepted为接入毫秒。peers[8]容纳握手
    // 中的连接，并不代表8人游戏。
    // 【函数 Peer::Peer】初始化尚未握手的连接槽。
    // 执行顺序：player=-1，reject=false，accepted=0；内含 socket 自行构造。
    // 参数/局部变量：它是定义在 main 函数内部的局部结构体，仅在该函数作用域可用。
    // 类型定义：network_transport.h:22 (NetSocket)。
    struct Peer {NetSocket socket;int player;bool reject;ULONGLONG accepted;Peer():player(-1),reject(false),accepted(0) {}} peers[8];
    // 类型定义：network_protocol.h:26 (NetState)。
    NetState state;bool hadHost=false;ULONGLONG created=GetTickCount64(),closedAt=0,ping=0;  // state权威房间；hadHost是否曾有房主；created/closedAt/ping均为毫秒时刻。
    int watchedExit=0,ackSlot=-1;bool seen[2]={false,false};ULONGLONG ackAt=0;  // watchedExit已处理离开流水；ackSlot待回ACK槽；seen双方回执；ackAt等待起点。
    bool run=true;  // run 是整个服务端的运行开关
    while(run) {
        // 语句说明：parentProcess有效且父进程已退出时停止；句柄为空则没有这一退出检测。
        if(parentProcess && WaitForSingleObject(parentProcess,0)==WAIT_OBJECT_0) break;
        // 语句说明：服务启动后约15秒仍没有房主完成握手就退出，避免空房间进程长期残留。
        if(!hadHost && GetTickCount64()-created>15000) break;
        // 语句说明：incoming为本轮新接入socket；INVALID_SOCKET可能只是此刻没有新连接。
        SOCKET incoming=accept(listener,NULL,NULL);
        if(incoming!=INVALID_SOCKET) {
            // 语句说明：slot为第一个空闲连接槽下标，-1表示8个槽全占用；i用于扫描槽数组。
            int slot=-1;for(int i=0;i<8;++i) if(peers[i].socket.fd==INVALID_SOCKET) {slot=i;break;}
            if(slot<0) closesocket(incoming);  // 8 个位置都满了，只能把新连接直接关掉
            // 首次调用定位：network_transport.h:41 (NetSocket::attach)。
            else {peers[slot].socket.attach(incoming);peers[slot].player=-1;peers[slot].reject=false;peers[slot].accepted=GetTickCount64();}
        }
        // 语句说明：changed记录本轮是否改变房间状态；为true时在循环末尾向已握手双方广播。
        bool changed=false;
        for(int i=0;i<8;++i) {
            // 类型定义：server_main.cpp:56 (Peer)。
            Peer& p=peers[i];if(p.socket.fd==INVALID_SOCKET) continue;  // 这个位置是空的，跳过
            // 首次调用定位：network_transport.h:66 (NetSocket::pump)。
            // 语句说明：messages收完整正文；alive是pump返回的连接可用性，不等于某条业务命令是否合法。
            std::vector<std::string> messages;bool alive=p.socket.pump(messages);
            // 语句说明：未完成HELLO的连接最多等待约5秒，避免长期占用连接槽。
            if(p.player<0 && GetTickCount64()-p.accepted>5000) alive=false;
            // 语句说明：j遍历这一连接收到的正文；已经reject后不再执行后续业务消息。
            for(size_t j=0;j<messages.size() && !p.reject;++j) {
                const std::string& msg=messages[j];  // 简写一下，后面反复要用到这条消息
                // 首次调用定位：network_transport.h:49 (NetSocket::queue)。
                if(msg.compare(0,5,"PING ")==0 && msg.size()<40) {p.socket.queue("PONG "+msg.substr(5));continue;}
                if(msg.compare(0,5,"SEEN ")==0 && p.player>=0) {
                    std::istringstream response(msg.substr(5));int serial=0;std::string extra;  // 把流水号那部分单独取出来解析
                    // 语句说明：serial必须与当前exitSerial一致；extra检测多余字段，避免旧回执误确认新离开。
                    if((response>>serial) && !(response>>extra) && serial==state.exitSerial) seen[p.player]=true;
                    continue;  // 处理完毕，看下一条
                }
                if(msg=="PING") {p.socket.queue("PONG");continue;}
                if(msg=="PONG") continue;  // 心跳的回应本身不需要再处理
                // 【阅读提示】HELLO 2 HOST token或HELLO 2 GUEST；身份槽只有两个，握手中的TCP槽可以多于两个。
                if(p.player<0) {
                    // 语句说明：input为HELLO解析流；cmd指令、version协议版本、role身份、key口令、extra尾随字段。
                    std::istringstream input(msg);std::string cmd,version,role,key,extra;
                    input>>cmd>>version>>role;  // 按空格把问候消息切开
                    // 语句说明：host表示请求HOST座位；只有房主格式才读取key口令。
                    bool host=role=="HOST";if(host) input>>key;
                    std::string error;  // error 为空表示通过；非空就是要发给对方的错误原因
                    // 语句说明：检查协议版本2及HELLO格式；未知身份或尾随字段也归入VERSION错误。
                    if(cmd!="HELLO" || version!="2" || (role!="HOST" && role!="GUEST") || (input>>extra)) error="VERSION";
                    // 语句说明：房主口令不匹配则AUTH；朋友端不提交房主口令。
                    else if(host && key!=token) error="AUTH";
                    // 语句说明：不在等待室则拒绝新玩家，当前协议不支持对局中途重连。
                    else if(state.phase!=N_LOBBY) error="STARTED";
                    // 语句说明：host对应0号座位，朋友对应1号；目标座位已有人则FULL。
                    else if(state.present[host?0:1]) error="FULL";
                    // 语句说明：朋友加入前要求房主已经完成握手，避免房主未到时形成悬空房间。
                    else if(!host && !state.present[0]) error="WAIT_HOST";
                    // 语句说明：拒绝时先把ERROR排入待发缓冲，reject使后续逻辑等待发完再关闭。
                    if(!error.empty()) {p.socket.queue("ERROR "+error);p.reject=true;continue;}
                    // 语句说明：player从-1转为正式座位；present置true；hadHost只在房主握手成功时置true。
                    p.player=host?0:1;state.present[p.player]=true;if(host) hadHost=true;
                    // 语句说明：WELCOME告诉客户端本机座位；changed使本轮末尾再同步完整STATE。
                    p.socket.queue(host?"WELCOME 0":"WELCOME 1");changed=true;
                } else if(msg=="LEAVE") {alive=false;break;}
                else {
                    std::string event;  // 事件字符串由 apply 填出来，用来做额外通知
                    // 首次调用定位：network_protocol.h:103 (NetState::apply)。
                    if(state.apply(p.player,msg,event)) {
                        changed=true;  // 指令合法且状态已更新，需要广播
                        // 语句说明：k扫描连接槽，拒绝提示只发给state.owner对应的申请者。
                        if(event=="REFUSED" || event=="UNDO_REFUSED") for(int k=0;k<8;++k)
                            if(peers[k].player==state.owner && peers[k].socket.fd!=INVALID_SOCKET) peers[k].socket.queue("EVENT "+event);  // 只发给发起者本人，不必让另一方看到这条拒绝提示
                        if(event=="UNDO_ACCEPTED") for(int k=0;k<8;++k) if(peers[k].player>=0) peers[k].socket.queue("EVENT UNDO_ACCEPTED");
                    // 首次调用定位：network_protocol.h:171 (netEncode)。
                    } else {p.socket.queue("EVENT INVALID");p.socket.queue(netEncode(state));}
                }
            }
            // 语句说明：已拒绝且待发缓冲清空后才关闭；queue成功本身并不意味着对方收到ERROR。
            if(p.reject && p.socket.output.empty()) alive=false;
            // 首次调用定位：network_protocol.h:89 (NetState::leave)；
            // network_transport.h:33 (NetSocket::close)。
            // 语句说明：失效连接先通知规则层清除在房状态，再关闭socket并释放连接槽。
            if(!alive) {if(p.player>=0) {state.leave(p.player);changed=true;}p.socket.close();p.player=-1;}
        }  // 这一轮处理完所有连接
        // 【阅读提示】离开确认：先广播含exitSerial的STATE；对方回SEEN，或3秒超时后回CLOSE_ACK。
        if(state.exitSerial!=watchedExit) {
            watchedExit=state.exitSerial;seen[0]=seen[1]=false;ackAt=GetTickCount64();ackSlot=-1;  // 忘记上一次的确认记录，重新开始等待
            // 语句说明：ackSlot定位离开者当前仍有效的连接槽；不是座位编号本身。
            for(int k=0;k<8;++k) if(peers[k].player==state.leaver && peers[k].socket.fd!=INVALID_SOCKET) ackSlot=k;
        }
        if(ackSlot>=0 && (seen[1-state.leaver] || GetTickCount64()-ackAt>=3000)) {
            peers[ackSlot].socket.queue("CLOSE_ACK");ackSlot=-1;  // 对方已SEEN或等待满3秒才发ACK；超时分支不保证对方实际收到。
        }
        // 语句说明：广播对象仅是已握手且socket有效的连接；两边接收同一个权威状态快照。
        if(changed) for(int i=0;i<8;++i) if(peers[i].player>=0 && peers[i].socket.fd!=INVALID_SOCKET) peers[i].socket.queue(netEncode(state));
        // 语句说明：每约3秒给正式玩家发送PING；收到任何字节会刷新底层received计时。
        if(GetTickCount64()-ping>3000) {for(int i=0;i<8;++i) if(peers[i].player>=0) peers[i].socket.queue("PING");ping=GetTickCount64();}
        // 语句说明：关房后仍运行约5秒，给广播/退出确认留发送机会。
        if(state.phase==N_CLOSED) {if(!closedAt) closedAt=GetTickCount64();if(GetTickCount64()-closedAt>5000) run=false;}
        // 语句说明：让出CPU约10毫秒，再开始下一轮非阻塞检查。
        Sleep(10);
    }
    for(int i=0;i<8;++i) peers[i].socket.close();
    closesocket(listener);WSACleanup();if(parentProcess) CloseHandle(parentProcess);return 0;  // 正常退出，返回 0
}
