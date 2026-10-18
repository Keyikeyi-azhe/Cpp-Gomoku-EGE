// 【建议阅读 10 / 联网界面整合】online_ui.h
// 全局状态 -> onlineEnter/onlineConnect -> updateOnline -> onlineMove/onlineAsk。
// 再读onlineAction、保存与退出流程，最后看drawOnlinePage/drawOnlineSidebar。
// 本文件依赖main.cpp已声明的全局变量和绘图函数；include位置本身也是依赖关系。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_ONLINE_UI_H
#define GOMOKU_ONLINE_UI_H
#include <iphlpapi.h>
#include "network_protocol.h"

// 类型定义：network_transport.h:98 (NetClient)。
NetClient netClient;
// 类型定义：network_protocol.h:26 (NetState)。
NetState netState;    // 服务端广播来的权威对局状态
HANDLE serverProcess=NULL,serverReady=NULL;  // serverProcess 是房主启动的服务端进程句柄；serverReady 是它的就绪事件
HWND ipEdit=NULL,portEdit=NULL;  // 两个原生输入框：填 IP 和端口
// 【阅读提示】netStage：0菜单/1建房表单/2加入表单/3连接中/4等待室/5关闭提示。
// 【阅读提示】archiveStage：0正常/1询问保存/2命名/3决定再战/4等待对方/5结束返回。
// 【阅读提示】以上是本机UI状态；权威房间阶段是netState.phase，见
// network_protocol.h:18 (NetPhase)。
int netStage=0,netRole=-1,archiveStage=0,archiveRound=-1;  // netStage连接页阶段0..5；netRole座位-1/0/1；archiveStage本机保存阶段0..5；archiveRound已触发保存的局号。
// 语句说明：连接成立/等状态确认/本机建房/曾开局/连接不可继续；五个标志职责不同。
bool netConnected=false,netPending=false,netHosting=false,netStarted=false,netLost=false;
// 语句说明：lobbyExit为等待室退出询问；closeAfterOnline表示联网收尾后关闭整个程序。
bool lobbyExit=false,closeAfterOnline=false;
// 语句说明：netLeaving正在等待离开确认；netExitAck已收CLOSE_ACK；netMovePending区分落子等待提示。
bool netLeaving=false,netExitAck=false,netMovePending=false;
// 语句说明：netSeenExit已处理的离开流水；netLatency为RTT毫秒，-1表示尚未测到。
int netSeenExit=0,netLatency=-1;
// 语句说明：自动返回时刻/离开等待截止/最近操作发送时刻，均为GetTickCount64毫秒。
ULONGLONG netAutoHomeAt=0,netLeaveDeadline=0,netPendingAt=0;
std::wstring netEndNotice,copyAddress;  // netEndNotice 结束时的提示文字；copyAddress 供复制按钮用的纯地址
// 【阅读提示】下面是前置声明，实际函数体分别见online_ui.h:215 (onlineRequestHome)、
// online_ui.h:203 (onlineDeparture)、online_ui.h:194 (onlineSaveStatus)。
void onlineRequestHome();
void onlineDeparture(int reason);
void onlineSaveStatus(int stage);
ULONGLONG netConnectAt=0,netDeadline=0;  // netConnectAt为最早尝试启动连接时刻（启动后清0）；netDeadline为连接阶段总截止毫秒。
std::string netAddress,netGreeting;  // 要连接的地址和连接后要发的问候语
int netPort=5000;  // 端口，默认 5000
std::wstring netError,hostAddresses;  // netError 当前要显示的错误文字；hostAddresses 本机地址提示
// 【类型 NetAction】联网按钮编号，从A_ONLINE=100开始，统一交onlineAction。RESTART/EXIT/UNDO/RESIGN
// 分别请求kind=1/2/3/4；SAVE类仅推进本机保存流程；AGAIN为同意再战，HOME为请求离开。
enum NetAction { A_ONLINE=100,A_NET_HOST,A_NET_JOIN,A_NET_BACK,A_NET_CONNECT,A_NET_CANCEL,
    A_NET_READY,A_NET_LEAVE,A_NET_YES,A_NET_NO,A_NET_RESTART,A_NET_EXIT,
    A_NET_SAVE_YES,A_NET_SAVE_NO,A_NET_WRITE,A_NET_NAME_CANCEL,A_NET_AGAIN,A_NET_HOME,A_NET_UNDO,A_NET_RESIGN,A_NET_COPY };
// 【函数 onlineForms】按 visible 控制联网表单输入框。
// 执行顺序：IP仅在 netStage=2 时显示；端口在 visible 时显示；焦点放到适合的框。
// 参数/局部变量：visible 为显示开关。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:296 (onlineConnect)；
// online_ui.h:438 (onlineDisconnected)；online_ui.h:67 (onlineStop)；
// online_ui.h:465 (updateOnline)。
void onlineForms(bool visible) {
    ShowWindow(ipEdit,visible && netStage==2?SW_SHOW:SW_HIDE);  // IP 框只在「加入房间」这一步显示
    ShowWindow(portEdit,visible?SW_SHOW:SW_HIDE);  // 端口框在创建和加入两步都显示
    if(visible) SetFocus(netStage==2?ipEdit:portEdit);
}
// 【函数 onlineStop】停止连接并回收本机启动的服务进程。
// 执行顺序：停 netClient；清连接与待确认；服务进程等50毫秒未退就终止；关进程/事件句柄；隐藏表单。
// 参数/局部变量：不负责完整重置所有联网字段，完整重置由 onlineEnter 处理。
// 从上层理解：main.cpp:669 (main)；online_ui.h:353 (onlineAction)；
// online_ui.h:438 (onlineDisconnected)；online_ui.h:94 (onlineEnter)；
// online_ui.h:84 (onlineHome)。
void onlineStop() {
    // 首次调用定位：network_transport.h:186 (NetClient::stop)。
    netClient.stop();netConnected=false;netPending=false;  // 断开后台连接线程，并清掉等待标记
    if(serverProcess) {
        if(WaitForSingleObject(serverProcess,50)==WAIT_TIMEOUT) TerminateProcess(serverProcess,0);  // 还在运行就强行结束；WAIT_TIMEOUT 说明它没自己退出
        CloseHandle(serverProcess);serverProcess=NULL;  // 收掉句柄并置空，避免重复使用已失效的句柄
    }
    if(serverReady) {CloseHandle(serverReady);serverReady=NULL;}  // 就绪事件句柄同样要释放
    // 首次调用定位：online_ui.h:56 (onlineForms)。
    onlineForms(false);  // 收起输入框
}
// 【函数 onlineHome】完成联网清理后回主菜单。
// 执行顺序：onlineStop、hideName；复位保存阶段/开局标志/游戏模式；mainMenu；若 closeAfterOnline 则
// 停止程序。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:229 (onlineArchiveDone)；
// online_ui.h:438 (onlineDisconnected)；online_ui.h:215 (onlineRequestHome)；
// online_ui.h:465 (updateOnline)。
void onlineHome() {
    // 首次调用定位：online_ui.h:67 (onlineStop)；main.cpp:188 (hideName)；
    // main.cpp:194 (mainMenu)。
    onlineStop();hideName();archiveStage=0;netStarted=false;gameMode="LOCAL_TWO";mainMenu();
    if(closeAfterOnline) {running=false;closeAfterOnline=false;}
}
// 【函数 onlineEnter】从零进入联网菜单。
// 执行顺序：停止旧连接、取消AI、隐藏命名；重新构造 NetState；重置身份、连接页面、保存流程、离开标
// 志、提示与延迟；page=NETWORK_PAGE。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:465 (updateOnline)。
void onlineEnter() {
    // 类型定义：network_protocol.h:26 (NetState)。
    // 首次调用定位：online_ui.h:67 (onlineStop)；main.cpp:81 (cancelAI)；
    // main.cpp:188 (hideName)；network_protocol.h:40 (NetState::NetState)。
    onlineStop();cancelAI();hideName();netState=NetState();netRole=-1;netStage=0;  // 先把一切停掉，再重置状态对象，等于「从零开始」
    archiveStage=0;archiveRound=-1;netLost=false;netStarted=false;lobbyExit=false;  // 复位各个标志
    netLeaving=netExitAck=netMovePending=false;netSeenExit=0;netLatency=-1;netAutoHomeAt=0;netEndNotice.clear();  // 复位与「离开」相关的标志、延迟显示和自动返回时刻
    netError.clear();page=NETWORK_PAGE;dialog=NO_DIALOG;  // 清掉错误文字，切到联网页面，关掉询问弹窗
}
// 【函数 localAddresses】枚举本机活动IPv4地址，返回显示文本，并设置 copyAddress。
// 执行顺序：GetAdaptersAddresses，缓冲不足则扩容重试；遍历启用网卡和单播地址；排除回环；优先收集
// 100.64..100.127段；否则取首个普通地址。
// 参数/局部变量：size/bytes 为缓冲长度和字节数组；list 为链表头；rc 为API结果；tailscale/other 为
// 候选文本；a 是网卡节点；u 是地址节点；b 为IPv4四字节指针；value 为格式化地址缓冲。
// 边界与易错点：这里按地址段启发式标记“Tailscale”，没有验证网卡确实属于Tailscale。界面也提示人工核
// 对；识别成功不等于网络已经连通。
// 从上层理解：online_ui.h:353 (onlineAction)。
std::wstring localAddresses() {
    ULONG size=16384;std::vector<unsigned char> bytes(size);  // size 既是缓冲区大小，也是调用后系统回报的需要大小
    IP_ADAPTER_ADDRESSES* list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&bytes[0]);  // 把字节缓冲区当结构体链表来用
    ULONG rc=GetAdaptersAddresses(AF_INET,0,NULL,list,&size);
    if(rc==ERROR_BUFFER_OVERFLOW) {bytes.resize(size);list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&bytes[0]);rc=GetAdaptersAddresses(AF_INET,0,NULL,list,&size);}
    std::wstring tailscale,other;  // tailscale收集100.64.0.0/10段候选；other留首个其他非回环IPv4。
    if(rc==NO_ERROR) for(IP_ADAPTER_ADDRESSES* a=list;a;a=a->Next) {  // a 遍历网卡链表
        if(a->OperStatus!=IfOperStatusUp) continue;
        for(IP_ADAPTER_UNICAST_ADDRESS* u=a->FirstUnicastAddress;u;u=u->Next) {  // u 遍历这块网卡上的每一个 IP
            if(u->Address.lpSockaddr->sa_family!=AF_INET) continue;
            const unsigned char* b=reinterpret_cast<const unsigned char*>(&reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr);
            if(b[0]==127) continue;
            wchar_t value[64];std::swprintf(value,64,L"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);  // 格式化成常见的点分十进制文字
            if(b[0]==100 && b[1]>=64 && b[1]<=127) {if(!tailscale.empty()) tailscale+=L" / ";tailscale+=value;}  // 按100.64.0.0/10启发式筛选；本代码未核实网卡归属，不能仅凭该段确认是Tailscale。
            else if(other.empty()) other=value;  // 普通地址只留第一个
        }  // 继续看下一个地址
    }
    if(!tailscale.empty()) {copyAddress=tailscale.substr(0,tailscale.find(L" / "));return L"Tailscale: "+tailscale;}  // 显示全部优先地址候选；copyAddress仅取第一个供复制。
    copyAddress=other;  // 没有优先地址候选则使用首个普通地址，界面仍提示用户核对。
    return L"\u672c\u673a: "+other+L"\uff08\u8bf7\u6838\u5bf9Tailscale\u5730\u5740\uff09";
}
// 【函数 onlineCanMove】联网规则下是否允许本机点棋盘。
// 执行顺序：要求已连接、未丢失、未离开、无待确认、非保存阶段、N_PLAY、座位有效，且 turn==netRole+1。
// 从上层理解：main.cpp:71 (humanCanMove)；online_ui.h:164 (onlineMove)。
bool onlineCanMove() {
    return netConnected && !netLost && !netLeaving && !netPending && archiveStage==0 &&
        netState.phase==N_PLAY && netRole>=0 && game.turn==netRole+1;  // 所有条件必须同时成立；身份有效且轮到本机颜色时才可落子。
}
// 【函数 onlineSend】提交需要等待状态更新的命令 cmd。
// 执行顺序：若仍连接且未lost，sendMessage；设netPending；记录netPendingAt。
// 参数/局部变量：cmd 为完整正文。
// 边界与易错点：netPending 只是UI防连点标志，不是TCP送达证明。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:184 (onlineAnswer)；
// online_ui.h:229 (onlineArchiveDone)；online_ui.h:175 (onlineAsk)；
// online_ui.h:203 (onlineDeparture)；其余调用处也标有定位。
void onlineSend(const std::string& cmd) {
    // 首次调用定位：network_transport.h:208 (NetClient::sendMessage)。
    if(netConnected && !netLost) {netClient.sendMessage(cmd);netPending=true;netPendingAt=GetTickCount64();}  // 记下发送时刻并标记为等待回复
}
// 【函数 onlineCommand】生成“cmd 当前局号”前缀字符串。
// 执行顺序：ostringstream out 写命令与 netState.round，再返回str。
// 参数/局部变量：cmd 为命令名；out 为输出流。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:184 (onlineAnswer)；
// online_ui.h:229 (onlineArchiveDone)；online_ui.h:175 (onlineAsk)；
// online_ui.h:203 (onlineDeparture)；其余调用处也标有定位。
std::string onlineCommand(const char* cmd) {
    std::ostringstream out;out<<cmd<<' '<<netState.round;return out.str();  // 指令格式：指令名 + 空格 + 局号
}
// 【函数 onlineMove】把压平格子编号 cell 变成 MOVE 请求。
// 执行顺序：检查可下与空格；设netMovePending；拼命令、已有手数、cell/15、cell%15；onlineSend。
// 参数/局部变量：out 为命令流；cell 来自 boardCell 的合法结果。
// 边界与易错点：函数本身不检查cell越界，依赖调用者。不会在这里 game.place。
// 从上层理解：main.cpp:669 (main)。
void onlineMove(int cell) {
    // 首次调用定位：online_ui.h:135 (onlineCanMove)。
    if(!onlineCanMove() || game.cells[cell/15][cell%15]) return;
    // 首次调用定位：online_ui.h:156 (onlineCommand)；online_ui.h:146 (onlineSend)。
    netMovePending=true;std::ostringstream out;out<<onlineCommand("MOVE")<<' '<<game.moves.size()<<' '<<cell/15<<' '<<cell%15;onlineSend(out.str());  // 拼出落子消息并发出去
}
// 【函数 onlineAsk】提出 kind 类型的操作询问。
// 执行顺序：要求已连接、无待确认、N_PLAY；拼ASK、局号、手数、kind；发送。
// 参数/局部变量：kind=1重开、2退出、3悔棋、4认输；out 为消息流。
// 从上层理解：main.cpp:669 (main)；online_ui.h:353 (onlineAction)；
// online_ui.h:465 (updateOnline)。
void onlineAsk(int kind) {
    if(!netConnected || netPending || netState.phase!=N_PLAY) return;
    // 首次调用定位：online_ui.h:156 (onlineCommand)；online_ui.h:146 (onlineSend)。
    std::ostringstream out;out<<onlineCommand("ASK")<<' '<<game.moves.size()<<' '<<kind;onlineSend(out.str());  // 拼出请求消息并发出
}
// 【函数 onlineAnswer】针对当前 question 答复 yes。
// 执行顺序：拼ANSWER、局号、问答编号、0/1；onlineSend。
// 参数/局部变量：yes 为是否同意；out 为消息流。
// 从上层理解：online_ui.h:353 (onlineAction)。
void onlineAnswer(bool yes) {
    // 首次调用定位：online_ui.h:156 (onlineCommand)；online_ui.h:146 (onlineSend)。
    std::ostringstream out;out<<onlineCommand("ANSWER")<<' '<<netState.question<<' '<<(yes?1:0);onlineSend(out.str());  // 拼出答复消息并发出；yes 转成 1 或 0
}
// 【函数 onlineSaveStatus】报告本机保存进度 stage。
// 执行顺序：若连接未lost，直接 sendMessage 发送SAVE，不设置netPending。
// 参数/局部变量：stage=1/2/3；o 为输出流。
// 边界与易错点：这是状态通知；为了不锁死本地命名流程，没有走 onlineSend。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:229 (onlineArchiveDone)；
// online_ui.h:281 (onlineName)。
void onlineSaveStatus(int stage) {
    // 首次调用定位：online_ui.h:156 (onlineCommand)；
    // network_transport.h:208 (NetClient::sendMessage)。
    if(netConnected && !netLost) {std::ostringstream o;o<<onlineCommand("SAVE")<<' '<<stage;netClient.sendMessage(o.str());}  // 直接提交保存状态，不设置netPending；后续仍会收到服务端STATE更新。
}
// 【函数 onlineDeparture】提交离开原因 why，并开始等待退出确认。
// 执行顺序：已netLeaving就返回；置标志；设6秒deadline并取消自动返回；发送CLOSE。
// 参数/局部变量：why 为LeaveReason；o 为命令流。
// 从上层理解：online_ui.h:353 (onlineAction)；online_ui.h:215 (onlineRequestHome)。
void onlineDeparture(int why) {
    if(netLeaving) return;
    netLeaving=true;netLeaveDeadline=GetTickCount64()+6000;netAutoHomeAt=0;  // 设 6 秒兜底时限；离开流程期间不再自动回主菜单
    // 首次调用定位：online_ui.h:156 (onlineCommand)；online_ui.h:146 (onlineSend)。
    std::ostringstream o;o<<onlineCommand("CLOSE")<<' '<<why;onlineSend(o.str());  // 拼出离开消息，附上原因
}
// 【函数 onlineRequestHome】根据当前阶段选择正确退出方式。
// 执行顺序：已离开则不重复；丢失/已关房直接onlineHome；未开局走等待室离开；终局按是否已提交再战选
// 取消或拒绝；重开等待选取消。
// 参数/局部变量：它不处理正在正常下棋时直接退出；那条路径先 onlineAsk(2)。
// 从上层理解：main.cpp:669 (main)；online_ui.h:353 (onlineAction)；
// online_ui.h:229 (onlineArchiveDone)。
void onlineRequestHome() {
    if(netLeaving) return;  // 已经在离开流程里就不重复触发
    // 首次调用定位：online_ui.h:84 (onlineHome)。
    if(netLost || netState.phase==N_CLOSED) {onlineHome();return;}  // 断线或房间已关，直接回主菜单
    // 首次调用定位：online_ui.h:203 (onlineDeparture)。
    if(!netStarted) {onlineDeparture(LEAVE_LOBBY);return;}  // 还没开局，按等待室离开处理
    if(netState.phase==N_FINISH) onlineDeparture(archiveStage==4?LEAVE_CANCEL:LEAVE_DECLINE);  // 一局结束时，区分「取消再战」和「拒绝再战」
    else if(netState.phase==N_RESTART) onlineDeparture(LEAVE_CANCEL);  // 正在协商重开时离开，记为取消
}
// 【函数 onlineArchiveDone】本机保存选择完成后的分流。
// 执行顺序：隐藏命名；报告SAVE 3；若连接结束，进入阶段5并等ACK或2秒返回；重开则阶段4并发送DONE；普
// 通终局则阶段3让用户选再战；需关程序则继续请求退出。
// 参数/局部变量：没有另建棋谱；它只推进保存后的交互。
// 从上层理解：online_ui.h:353 (onlineAction)。
void onlineArchiveDone() {
    // 首次调用定位：main.cpp:188 (hideName)；online_ui.h:194 (onlineSaveStatus)。
    hideName();onlineSaveStatus(3);  // 隐藏命名框，并告诉对方我这一步做完了
    if(netLost || netState.phase==N_CLOSED) {  // 连接已失效或房间已关闭；与“对方的进程已退出”不是同一保证。
        archiveStage=5;  // 进入第 5 阶段：对方已离开，本机也处理完了
        if(netState.leaver==netRole && netState.leaveReason!=LEAVE_DROP) {  // 区分「正常通知离开」和「意外掉线」
            // 首次调用定位：online_ui.h:84 (onlineHome)。
            if(netExitAck) onlineHome();else {netLeaving=true;netLeaveDeadline=GetTickCount64()+4000;}
        } else netAutoHomeAt=GetTickCount64()+2000;  // 别人的离开：两秒后自动回主菜单
        return;
    }
    // 首次调用定位：online_ui.h:146 (onlineSend)；online_ui.h:156 (onlineCommand)。
    if(netState.phase==N_RESTART) {archiveStage=4;onlineSend(onlineCommand("DONE"));}  // 重开流程：进入等待双方阶段，并通知服务端我已完成
    else archiveStage=3;
    // 首次调用定位：online_ui.h:215 (onlineRequestHome)。
    if(closeAfterOnline) onlineRequestHome();  // 如果本来就要关程序，那就顺势走离开流程
}
// 【函数 onlineSave】以 raw 名称把权威历史存为本机联网复盘。
// 执行顺序：修剪/校验名称；枚举已有合法复盘名称；重名附加(2)、(3)...且截断基名控制长度；组装ONLINE
// 记录及本机颜色/结果原因/开局时间；writeRecord。
// 参数/局部变量：first/last 为首尾位置；name 清理后的名称；names 已有名数组；info/find 为文件枚举；
// r 为读回记录；base 为原名；i 为重名序号；suffix 为编号缓冲；record 为待写记录。
// 边界与易错点：联网未完也 replay=true；只保存已收到确认的 game，不记录尚未获确认的落子请求。
// 从上层理解：online_ui.h:353 (onlineAction)。
bool onlineSave(const std::wstring& raw) {
    size_t first=raw.find_first_not_of(L" \t\r\n"),last=raw.find_last_not_of(L" \t\r\n");
    // 首次调用定位：main.cpp:113 (toast)。
    if(first==std::wstring::npos) {toast(L"\u8bf7\u8f93\u5165\u68cb\u5c40\u540d\u79f0");return false;}
    std::wstring name=raw.substr(first,last-first+1);  // 修剪首尾空白后的候选显示名；后面再检查长度、换行和重名。
    if(name.size()>24 || name.find_first_of(L"\r\n\t")!=std::wstring::npos) {toast(L"\u540d\u79f0\u6700\u591a24\u5b57\uff0c\u4e0d\u80fd\u5305\u542b\u6362\u884c");return false;}
    std::vector<std::wstring> names;WIN32_FIND_DATAW info;
    HANDLE find=FindFirstFileW(L"data\\replays\\*.greplay",&info);
    if(find!=INVALID_HANDLE_VALUE) {do {
        if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        // 类型定义：record_store.h:20 (GameRecord)。
        // 首次调用定位：record_store.h:155 (readRecord)。
        GameRecord r;if(readRecord(std::wstring(L"data\\replays\\")+info.cFileName,true,r)) names.push_back(r.name);
    } while(FindNextFileW(find,&info));FindClose(find);}
    std::wstring base=name;  // base 保存原始名字，追加编号时要在它基础上截断
    for(int i=2;std::find(names.begin(),names.end(),name)!=names.end();++i) {
        wchar_t suffix[32];std::swprintf(suffix,32,L"(%d)",i);name=base.substr(0,24-std::wcslen(suffix))+suffix;  // 给后缀留出宽字符代码单元数，最终name.size不超过24。
    }
    GameRecord record;record.name=name;record.mode="ONLINE";record.human=netRole+1;  // human 记录本机当时执的颜色，复盘时才能正确显示「本机当时执黑/白」
    record.replay=true;record.game=game;record.reason=netState.reason;record.started=netState.started;  // 结束原因和开局时间也一并记下
    // 首次调用定位：record_store.h:200 (writeRecord)。
    if(!writeRecord(record)) {toast(L"\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5\u6216\u53d6\u6d88");return false;}
    toast(L"\u590d\u76d8\u5df2\u4fdd\u5b58\u5230\u672c\u673a");return true;  // 保存成功提示
}
// 【函数 onlineName】打开联网命名框并生成默认名字。
// 执行顺序：archiveStage=2；报告SAVE 2；取本地时间组成“联网年月日_时分秒”；显示并聚焦nameEdit。
// 参数/局部变量：t 为SYSTEMTIME；value 为名称缓冲。
// 从上层理解：online_ui.h:353 (onlineAction)。
void onlineName() {
    // 首次调用定位：online_ui.h:194 (onlineSaveStatus)。
    archiveStage=2;onlineSaveStatus(2);SYSTEMTIME t;GetLocalTime(&t);wchar_t value[64];  // 用当前时间生成一个默认名字，玩家不改也能直接保存
    std::swprintf(value,64,L"\u8054\u7f51%04d%02d%02d_%02d%02d%02d",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    SetWindowTextW(nameEdit,value);ShowWindow(nameEdit,SW_SHOW);SetFocus(nameEdit);
}
// 【函数 onlineConnect】验证表单，必要时启动服务端，并进入异步连接阶段。
// 执行顺序：端口纯数字且1..65535；选择本地回环/填写IP并校验IPv4；记住有效表单；朋友准备HELLO 2
// GUEST；房主生成GUID口令、建就绪事件、隐藏启动服务器；隐藏表单并设10秒总deadline。
// 参数/局部变量：portText/ipText 是输入缓冲；p 是端口字符串；number 为解析整数；a/b/c/d 为IPv4四段，
// extra 检测尾随字符；id 为GUID；token 为口令文本；eventName 为事件名；directory/exe 为目录与服务
// exe路径；line 为宽输出流；args 为可修改命令行；si 为启动信息；pi 为进程/线程句柄结果。
// 边界与易错点：CreateProcess 参数包含端口、口令和父PID。这里没有立即连接：updateOnline 等
// serverReady亮起，避免服务端还没listen就连接失败。
// 从上层理解：online_ui.h:353 (onlineAction)。
void onlineConnect() {
    wchar_t portText[32],ipText[128];GetWindowTextW(portEdit,portText,32);GetWindowTextW(ipEdit,ipText,128);  // 取回玩家填的端口和地址
    std::wstring p=portText;
    if(p.empty() || p.find_first_not_of(L"0123456789")!=std::wstring::npos) {netError=L"\u7aef\u53e3\u5fc5\u987b\u662f1\u81f365535\u7684\u6574\u6570";return;}
    long number=std::wcstol(portText,NULL,10);if(number<1 || number>65535) {netError=L"\u7aef\u53e3\u5fc5\u987b\u662f1\u81f365535\u7684\u6574\u6570";return;}
    netPort=static_cast<int>(number);netHosting=netStage==1;  // 记录这次到底是房主还是朋友
    // 首次调用定位：record_store.h:39 (toUtf8)。
    netAddress=netHosting?"127.0.0.1":toUtf8(ipText);  // 房主连自己，朋友连填进来的地址
    unsigned a,b,c,d;char extra;
    if(std::sscanf(netAddress.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4 || a>255 || b>255 || c>255 || d>255 || a==0 || a>=224) {
        netError=L"\u8bf7\u586b\u5199\u6709\u6548\u7684IPv4\u5730\u5740";return;  // 格式不对就提示并中止，保留玩家已填的内容
    }
    // 首次调用定位：record_store.h:65 (recordDirectories)。
    recordDirectories();
    WritePrivateProfileStringW(L"network",netHosting?L"host_port":L"port",portText,L".\\data\\network.ini");  // 分别保存创建/加入端口；只有下一行的非房主分支才保存加入地址。
    if(!netHosting) WritePrivateProfileStringW(L"network",L"address",ipText,L".\\data\\network.ini");
    netGreeting="HELLO 2 GUEST";  // 默认先按朋友端准备问候语
    // 【阅读提示】房主启动独立服务器后也作为客户端连接127.0.0.1；两边落子都经过同一个裁判。
    if(netHosting) {
        GUID id;if(FAILED(CoCreateGuid(&id))) {netError=L"\u65e0\u6cd5\u751f\u6210\u623f\u95f4\u6807\u8bc6\uff0c\u8bf7\u91cd\u8bd5";return;}  // GUID格式化为房主座位口令，同时传给本机服务进程；不是网络加密机制。
        char token[64];std::sprintf(token,"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",id.Data1,id.Data2,id.Data3,
            id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);
        netGreeting=std::string("HELLO 2 HOST ")+token;  // 房主的问候语要带上这个口令
        std::string eventName=std::string("Local\\GomokuReady-")+token;
        serverReady=CreateEventA(NULL,TRUE,FALSE,eventName.c_str());  // 创建与子进程约定的就绪事件
        if(!serverReady) {netError=L"\u521b\u5efa\u623f\u95f4\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5";return;}
        wchar_t directory[32768];GetCurrentDirectoryW(32768,directory);
        std::wstring exe=std::wstring(directory)+L"\\GomokuServer.exe";  // 服务端程序就在本程序旁边，用当前目录拼出完整路径
        // 首次调用定位：record_store.h:51 (fromUtf8)。
        std::wostringstream line;line<<L'"'<<exe<<L"\" "<<netPort<<L' '<<fromUtf8(token)<<L' '<<GetCurrentProcessId();  // 参数依次是端口、口令、本进程编号
        std::wstring args=line.str();STARTUPINFOW si;ZeroMemory(&si,sizeof(si));si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi;ZeroMemory(&pi,sizeof(pi));
        if(!CreateProcessW(exe.c_str(),&args[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) {
            CloseHandle(serverReady);serverReady=NULL;netError=L"\u542f\u52a8\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5GomokuServer.exe";return;  // 创建失败时清理句柄并提示玩家检查文件是否齐全
        }
        serverProcess=pi.hProcess;CloseHandle(pi.hThread);  // serverProcess 记下句柄，之后要负责收尾
    }
    // 首次调用定位：online_ui.h:56 (onlineForms)。
    onlineForms(false);netStage=3;netError.clear();netRole=-1;netConnected=false;netLost=false;  // 进入「连接中」这一步
    // 语句说明：房主还需等serverReady；netConnectAt只设最早时刻，netDeadline给整个阶段10秒上限。
    netConnectAt=GetTickCount64()+1;netDeadline=GetTickCount64()+10000;
}
// 【函数 onlineLobbyLeave】打开等待室离开询问。
// 执行顺序：lobbyExit=true；若本机已准备，先发送READY 0取消准备。
// 参数/局部变量：此时还没真正CLOSE；用户随后可选择否。
// 从上层理解：main.cpp:669 (main)；online_ui.h:353 (onlineAction)。
void onlineLobbyLeave() {
    lobbyExit=true;
    // 首次调用定位：online_ui.h:146 (onlineSend)。
    if(netRole>=0 && netState.ready[netRole]) onlineSend("READY 0");
}  // 这里只发取消准备并弹询问；确认离开后才由onlineDeparture提交CLOSE。
// 【函数 onlineAction】处理所有 NetAction 按钮。
// 执行顺序：创建/加入设置表单并恢复INI；连接/取消转相应函数；准备/询问/保存/再战按按钮分流；复制按
// 钮组装IP端口并写系统剪贴板。
// 参数/局部变量：id 动作；value 在不同分支分别是INI/命名缓冲或复制输出流；content 为复制正文；ok
// 成功标志；memory 为可移动全局内存；pointer 为GlobalLock后地址。
// 边界与易错点：A_NET_AGAIN为同意再战；剪贴板内存仅在交付失败时自行释放，交付成功后由系统管理。
// 从上层理解：main.cpp:331 (act)。
void onlineAction(int id) {
    switch(id) {
    // 首次调用定位：online_ui.h:94 (onlineEnter)。
    case A_ONLINE:onlineEnter();break;
    case A_NET_HOST:case A_NET_JOIN:
        // 首次调用定位：online_ui.h:111 (localAddresses)。
        netStage=id==A_NET_HOST?1:2;netError.clear();hostAddresses=localAddresses();  // 进入创建或加入的页面，并查询本机地址
        {wchar_t value[128];GetPrivateProfileStringW(L"network",netStage==1?L"host_port":L"port",L"5000",value,128,L".\\data\\network.ini");SetWindowTextW(portEdit,value);
         if(netStage==2) {GetPrivateProfileStringW(L"network",L"address",L"",value,128,L".\\data\\network.ini");SetWindowTextW(ipEdit,value);}}
        // 首次调用定位：online_ui.h:56 (onlineForms)。
        onlineForms(true);break;
    case A_NET_BACK:
        onlineForms(false);netError.clear();if(netStage==0) page=MODE_MENU;else netStage=0;break;  // 连接页和联网菜单的「返回」去向不同
    // 首次调用定位：online_ui.h:296 (onlineConnect)。
    case A_NET_CONNECT:onlineConnect();break;
    // 首次调用定位：online_ui.h:67 (onlineStop)。
    case A_NET_CANCEL:onlineStop();netStage=netHosting?1:2;netError=L"\u5df2\u53d6\u6d88\u8fde\u63a5";onlineForms(true);break;
    // 首次调用定位：online_ui.h:146 (onlineSend)。
    case A_NET_READY:if(!netPending && netRole>=0) onlineSend(netState.ready[netRole]?"READY 0":"READY 1");break;  // 等待室里点准备或取消准备
    // 首次调用定位：online_ui.h:341 (onlineLobbyLeave)。
    case A_NET_LEAVE:onlineLobbyLeave();break;
    case A_NET_YES:
        // 首次调用定位：online_ui.h:203 (onlineDeparture)。
        if(lobbyExit) onlineDeparture(LEAVE_LOBBY);  // 确认离开等待室时发送CLOSE；不同于先前只撤销READY。
        // 首次调用定位：online_ui.h:184 (onlineAnswer)。
        else onlineAnswer(true);break;
    case A_NET_NO:
        closeAfterOnline=false;if(lobbyExit) lobbyExit=false;else onlineAnswer(false);break;  // 取消关闭程序或拒绝对方请求
    // 首次调用定位：online_ui.h:175 (onlineAsk)。
    case A_NET_RESTART:onlineAsk(1);break;  // 申请重开
    case A_NET_EXIT:onlineAsk(2);break;
    case A_NET_UNDO:onlineAsk(3);break;
    case A_NET_RESIGN:onlineAsk(4);break;
    // 首次调用定位：online_ui.h:281 (onlineName)。
    case A_NET_SAVE_YES:onlineName();break;
    // 首次调用定位：online_ui.h:229 (onlineArchiveDone)。
    case A_NET_SAVE_NO:onlineArchiveDone();break;  // 不保存，直接进入下一步
    // 首次调用定位：online_ui.h:253 (onlineSave)。
    case A_NET_WRITE:{wchar_t value[128];GetWindowTextW(nameEdit,value,128);if(onlineSave(value)) onlineArchiveDone();break;}  // 取回命名框内容，保存成功后进入下一步
    // 首次调用定位：main.cpp:188 (hideName)；online_ui.h:194 (onlineSaveStatus)。
    case A_NET_NAME_CANCEL:hideName();archiveStage=1;onlineSaveStatus(1);break;
    case A_NET_AGAIN:  // 同意再战；有效连接下设置等待阶段并发送AGAIN。
        // 首次调用定位：online_ui.h:84 (onlineHome)；online_ui.h:156 (onlineCommand)。
        if(netLost || netState.phase==N_CLOSED) onlineHome();else {archiveStage=4;onlineSend(onlineCommand("AGAIN"));}break;  // 同意再战；如果对方已不在就直接回去
    // 首次调用定位：online_ui.h:215 (onlineRequestHome)。
    case A_NET_HOME:onlineRequestHome();break;  // 其余情况都走「请求回主菜单」
    // 【阅读提示】剪贴板：成功SetClipboardData后系统接管memory；失败才由本函数GlobalFree。
    case A_NET_COPY: {
        // 首次调用定位：main.cpp:113 (toast)。
        if(copyAddress.empty()) {toast(L"\u672a\u8bc6\u522b\u5730\u5740\uff0c\u8bf7\u4eceTailscale\u590d\u5236");break;}
        std::wostringstream value;value<<L"IP: "<<copyAddress<<L"\r\n\u7aef\u53e3: "<<netPort;  // 拼出要复制的文字：地址加换行加端口
        std::wstring content=value.str();bool ok=false;
        if(OpenClipboard(getHWnd())) {
            HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(content.size()+1)*sizeof(wchar_t));  // GMEM_MOVEABLE 是剪贴板要求的内存类型
            if(memory) {void* pointer=GlobalLock(memory);if(pointer) {
                std::memcpy(pointer,content.c_str(),(content.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);  // 把文字拷进这块内存
                if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)) ok=true;
            } if(!ok) GlobalFree(memory);}  // 失败时才需要自己释放这块内存
            CloseClipboard();  // 用完必须关闭剪贴板，否则别的程序无法使用
        }
        toast(ok?L"\u8fde\u63a5\u4fe1\u606f\u5df2\u590d\u5236":L"\u590d\u5236\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5");break;
    }
    }
}
// 【函数 onlineLeaveText】按 why 和是否自己 mine 生成离开提示。
// 执行顺序：自己主动离开统一显示已通知；其他情况switch映射朋友离开、房主关房、退出、拒绝、取消、断
// 线。
// 参数/局部变量：why 为LeaveReason；mine 表示离开者是否本机。
// 从上层理解：online_ui.h:465 (updateOnline)。
std::wstring onlineLeaveText(int why,bool mine) {
    if(mine && why!=LEAVE_DROP) return L"\u5df2\u901a\u77e5\u5bf9\u65b9\uff0c\u672c\u6b21\u8fde\u63a5\u7ed3\u675f";  // 自己主动离开且原因不是掉线，就统一显示这句
    switch(why) {
    case LEAVE_LOBBY:return L"\u5bf9\u65b9\u5df2\u79bb\u5f00\u623f\u95f4\uff0c\u7b49\u5f85\u5176\u4ed6\u73a9\u5bb6\u52a0\u5165";  // 下面按不同原因给出对方视角的说明
    case LEAVE_HOST:return L"\u623f\u4e3b\u5df2\u5173\u95ed\u623f\u95f4";
    case LEAVE_QUIT:return L"\u5bf9\u624b\u9000\u51fa\u4e86\u5bf9\u5c40\uff0c\u672c\u5c40\u5df2\u4e2d\u6b62";
    case LEAVE_DECLINE:return L"\u5bf9\u624b\u62d2\u7edd\u4e86\u518d\u6765\u4e00\u5c40";
    case LEAVE_CANCEL:return L"\u5bf9\u624b\u53d6\u6d88\u4e86\u518d\u6765\u4e00\u5c40";
    default:return L"\u4e0e\u5bf9\u624b\u7684\u8fde\u63a5\u5df2\u4e2d\u65ad";  // 其余(含掉线)统一显示为连接中断
    }
}
// 【函数 onlineDisconnected】处理连接失败/丢失。
// 执行顺序：清连接及待确认；本来正在离开就回首页；尚未开局则退表单重试；已开局保留明确结束原因，否
// 则补断线；未处理保存就询问，已处理则安排返回。
// 参数/局部变量：不是重连功能：本局断线后只能保存复盘，不能继续原联网对局。
// 从上层理解：online_ui.h:465 (updateOnline)。
void onlineDisconnected() {
    netConnected=false;netLost=true;netPending=false;netMovePending=false;
    // 首次调用定位：online_ui.h:84 (onlineHome)。
    if(netLeaving) {onlineHome();return;}  // 本来就在离开流程里，断线只是顺理成章的结果
    if(!netStarted) {
        // 首次调用定位：online_ui.h:67 (onlineStop)；online_ui.h:56 (onlineForms)。
        onlineStop();netStage=netHosting?1:2;page=NETWORK_PAGE;onlineForms(true);  // 还没开局就断线，退回填写页面
        if(netError.empty()) netError=L"\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u5730\u5740\u4e0e\u7f51\u7edc";  // 给出连接失败提示
        lobbyExit=false;return;  // 清掉「要离开」的标记，让玩家可以重新填
    }
    if(netEndNotice.empty()) {
        netState.leaveReason=LEAVE_DROP;netState.leaver=1-netRole;netState.phase=N_CLOSED;  // 让界面把这次离开的原因显示出来
        netEndNotice=L"\u4e0e\u5bf9\u624b\u7684\u8fde\u63a5\u5df2\u4e2d\u65ad\uff0c\u672c\u5c40\u65e0\u6cd5\u7ee7\u7eed";
        if(!game.result && netState.reason!=END_RESTART) netState.reason=END_DISCONNECT;  // 未完的棋局记为「连接中断」结束
    }
    if(archiveStage==0) {archiveStage=1;archiveRound=netState.round;}
    else if(archiveStage>=3) {archiveStage=5;if(!netAutoHomeAt) netAutoHomeAt=GetTickCount64()+2000;}
}
// 【函数 updateOnline】每帧处理联网计时、收消息、更新棋局和保存UI。
// 执行顺序：无关页面直接返回；处理自动返回/离开超时；连接中等就绪再start；循环take，分别处理延迟、
// ACK、WELCOME、事件、错误、STATE；合法STATE更新netState及game；新局清旧UI；新exitSerial回SEEN；终
// 局每个round只启动一次保存。
// 参数/局部变量：message 当前正文；value 解析LATENCY的流；closing 临时保留关闭窗口意图；state 临时
// 解码快照；oldRound 原局号；leaveOnStart 记录等待室退出与开局竞争；seen 为SEEN输出流。
// 边界与易错点：收到STATE时整体覆盖game；不要在UI保留未被服务端确认的“预测棋子”。一条普通STATE也会
// 清netPending，不是靠每条请求独立ACK配对。
// 从上层理解：main.cpp:669 (main)。
void updateOnline() {
    if(page!=NETWORK_PAGE && !(page==GAME_PAGE && gameMode=="ONLINE")) return;
    if(netAutoHomeAt && GetTickCount64()>=netAutoHomeAt) {
        // 首次调用定位：online_ui.h:84 (onlineHome)；online_ui.h:94 (onlineEnter)。
        if(netStarted) onlineHome();else {onlineEnter();if(closeAfterOnline) {running=false;closeAfterOnline=false;}}  // 该自动返回了
        return;
    }
    if(netLeaving && GetTickCount64()>=netLeaveDeadline) {onlineHome();return;}
    // 【阅读提示】整体连接UI最多等待10秒；房主必须观察就绪事件后再启动网络线程。
    if(netStage==3 && !netConnected) {
        if(serverProcess && WaitForSingleObject(serverProcess,0)==WAIT_OBJECT_0) {
            // 首次调用定位：online_ui.h:438 (onlineDisconnected)。
            netError=L"\u670d\u52a1\u7aef\u542f\u52a8\u5931\u8d25\uff0c\u7aef\u53e3\u53ef\u80fd\u5df2\u88ab\u5360\u7528";onlineDisconnected();return;  // 提示可能是端口被占用
        }
        if(netConnectAt && GetTickCount64()>=netConnectAt && (!netHosting || (serverReady && WaitForSingleObject(serverReady,0)==WAIT_OBJECT_0))) {
            // 首次调用定位：network_transport.h:196 (NetClient::start)。
            netConnectAt=0;if(!netClient.start(netAddress,netPort,netGreeting)) {netError=L"\u7f51\u7edc\u7ebf\u7a0b\u542f\u52a8\u5931\u8d25";onlineDisconnected();return;}
        }  // 连接线程启动逻辑结束；下一判断单独处理整体连接超时。
        if(GetTickCount64()>netDeadline) {netError=L"\u8fde\u63a5\u8d85\u65f6\uff0c\u8bf7\u68c0\u67e5\u5730\u5740\u3001\u9632\u706b\u5899\u548cTailscale";onlineDisconnected();return;}
    }
    std::string message;
    // 首次调用定位：network_transport.h:214 (NetClient::take)。
    while(netClient.take(message)) {
        // 首次调用定位：network_transport.h:208 (NetClient::sendMessage)。
        if(message=="PING") {netClient.sendMessage("PONG");continue;}
        if(message.compare(0,8,"LATENCY ")==0) {std::istringstream value(message.substr(8));value>>netLatency;continue;}  // 延迟数据单独处理
        if(message=="CLOSE_ACK") {
            netExitAck=true;  // 收到确认
            if(netLeaving) {
                if(!netStarted) {bool closing=closeAfterOnline;onlineEnter();if(closing) running=false;}
                else onlineHome();  // 已开局走onlineHome；上方未开局分支回到联网菜单，并保留关窗口意图。
                return;
            }
            continue;
        }
        if(message=="WELCOME 0" || message=="WELCOME 1") {
            netRole=message.back()-'0';netConnected=true;netStage=4;netPending=false;netError.clear();continue;
        }  
        // 首次调用定位：main.cpp:113 (toast)。
        if(message=="EVENT REFUSED") {toast(L"\u5bf9\u65b9\u62d2\u7edd\u4e86\u4f60\u7684\u91cd\u5f00\u8bf7\u6c42");continue;}  // 服务端通知重开请求被对方拒绝；短提示不改变棋盘。
        if(message=="EVENT UNDO_REFUSED") {toast(L"\u5bf9\u624b\u62d2\u7edd\u4e86\u4f60\u7684\u6094\u68cb\u8bf7\u6c42");continue;}  // 服务端通知悔棋请求被对方拒绝。
        // 语句说明：服务端通知悔棋已完成，随后以STATE中的历史为准。
        if(message=="EVENT UNDO_ACCEPTED") {toast(L"\u6094\u68cb\u5df2\u5b8c\u6210\uff0c\u8bf7\u91cd\u65b0\u843d\u5b50");continue;}
        if(message=="EVENT INVALID") {
            netPending=false;netMovePending=false;
            if(netLeaving) {netLeaving=false;closeAfterOnline=false;}
            toast(L"\u72b6\u6001\u5df2\u66f4\u65b0\uff0c\u8bf7\u91cd\u65b0\u64cd\u4f5c");continue;  // 提示玩家重新操作
        }
        if(message.compare(0,6,"ERROR ")==0 || message=="LOST") {
            if(netState.phase==N_CLOSED && !netEndNotice.empty()) {
                if(netLeaving) {onlineHome();return;}
                netConnected=false;continue;  // 房间已正常关闭，安静处理
            }
            if(message=="ERROR FULL") netError=L"\u623f\u95f4\u5df2\u6ee1";  // 房间已满
            else if(message=="ERROR STARTED") netError=L"\u5bf9\u65b9\u6b63\u5728\u5bf9\u5c40\uff0c\u4e0d\u80fd\u4e2d\u9014\u52a0\u5165";  // 对方正在对局，不能中途加入
            else if(message=="ERROR VERSION") netError=L"\u53cc\u65b9\u7248\u672c\u4e0d\u517c\u5bb9\uff0c\u8bf7\u540c\u65f6\u66f4\u65b0\u6e38\u620f\u548c\u670d\u52a1\u7aef";  // 双方版本不一致
            else if(message!="LOST") netError=L"\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u5730\u5740\u3001\u7aef\u53e3\u548c\u7f51\u7edc";  // 其它连接问题
            onlineDisconnected();break;  // 断线处理
        }
        // 【阅读提示】只有成功解码的STATE才整体替换netState；已开局后再将其game复制到显示用game。
        // 类型定义：network_protocol.h:26 (NetState)。
        NetState state;
        // 首次调用定位：network_protocol.h:189 (netDecode)。
        if(netRole<0 || !netDecode(message,state)) {netError=L"\u6536\u5230\u5f02\u5e38\u7f51\u7edc\u6570\u636e\uff0c\u8fde\u63a5\u5df2\u505c\u6b62";onlineDisconnected();break;}
        netPending=false;netMovePending=false;int oldRound=netState.round;netState=state;bool leaveOnStart=false;  // 每次合法STATE都解除待确认标志；oldRound用于识别是否进入新局。
        if(netState.round>0) {
            if(oldRound!=netState.round || !netStarted) {
                leaveOnStart=lobbyExit;lobbyExit=false;  // 新局开始，清掉上一局的界面残留
                // 首次调用定位：main.cpp:188 (hideName)。
                hideName();archiveStage=0;archiveRound=-1;currentName.clear();loadedPath.clear();netEndNotice.clear();netAutoHomeAt=0;netExitAck=false;  // 清空棋局名称与路径，新的这一局要重新命名
            }  // 各种与结束相关的提示和标记也一并复位
            netStarted=true;game=netState.game;gameMode="ONLINE";page=GAME_PAGE;dialog=NO_DIALOG;  // 用服务端的棋盘刷新本机显示的棋局
        }
        // 首次调用定位：online_ui.h:175 (onlineAsk)。
        if(leaveOnStart && netState.phase==N_PLAY) onlineAsk(2);
        // 【阅读提示】每个新的exitSerial只处理一次并回SEEN，让服务端知道离开通知已到达本机。
        if(netState.exitSerial>netSeenExit) {
            netSeenExit=netState.exitSerial;std::ostringstream seen;seen<<"SEEN "<<netSeenExit;netClient.sendMessage(seen.str());
            if(netState.phase==N_LOBBY) {  // 等待室里有人离开：直接提示即可
                // 首次调用定位：online_ui.h:422 (onlineLeaveText)。
                if(netState.leaver!=netRole) toast(onlineLeaveText(netState.leaveReason,false));  // 还没开始对局时的离开提示
            } else {
                netEndNotice=onlineLeaveText(netState.leaveReason,netState.leaver==netRole);  // 记录明确离开原因；下行netLost也用于正常关房后的交互锁定，不仅表示故障掉线。
                netLost=true;netPending=false;  // 不允许再落子
                // 首次调用定位：online_ui.h:56 (onlineForms)。
                if(!netStarted) {netStage=5;onlineForms(false);if(!netLeaving) netAutoHomeAt=GetTickCount64()+2000;}
                else if(archiveStage==0) {archiveStage=1;archiveRound=netState.round;}
                else if(archiveStage>=3 && !netLeaving) {archiveStage=5;netAutoHomeAt=GetTickCount64()+2000;}
            }
        }  // 一局结束或进入重开协商时，开始本机的保存询问
        if((netState.phase==N_FINISH || netState.phase==N_RESTART) && archiveRound!=netState.round) {
            archiveRound=netState.round;archiveStage=1;  // 每个局号只触发一次
        }
    }
}
// 【函数 opponentProgress】给出对方保存/再战的进度文字。
// 执行顺序：身份无效或lost返回空；按again、done、saveStage顺序选提示。
// 参数/局部变量：other=1-netRole 是对方座位。
// 从上层理解：online_ui.h:624 (drawOnlineSidebar)。
std::wstring opponentProgress() {
    if(netRole<0 || netLost) return L"";
    int other=1-netRole;
    if(netState.again[other]) return L"\u5bf9\u65b9\u5df2\u540c\u610f\u518d\u6765\u4e00\u5c40\uff0c\u7b49\u5f85\u4f60\u9009\u62e9";  // 对方已同意再来一局
    if(netState.done[other]) return L"\u5bf9\u65b9\u5df2\u5904\u7406\u4fdd\u5b58\uff0c\u7b49\u5f85\u4f60\u5b8c\u6210";  // 对方已处理完保存，正在等我
    if(netState.saveStage[other]==1) return L"\u5bf9\u65b9\u6b63\u5728\u9009\u62e9\u662f\u5426\u4fdd\u5b58";  // 对方正在选择是否保存
    if(netState.saveStage[other]==2) return L"\u5bf9\u65b9\u6b63\u5728\u547d\u540d\u4fdd\u5b58";  // 对方正在命名
    if(netState.saveStage[other]==3) return netState.phase==N_RESTART?L"\u5bf9\u65b9\u5df2\u5904\u7406\u4fdd\u5b58":L"\u5bf9\u65b9\u6b63\u5728\u51b3\u5b9a\u662f\u5426\u518d\u6765\u4e00\u5c40";  // 对方在决定要不要再来一局，或已经处理完
    return L"";
}
// 【函数 drawOnlinePage】绘制联网菜单、表单、连接中、等待室和开局前关闭提示。
// 执行顺序：根据netStage分支；登记能用的按钮；等待室显示双方present/ready；退出询问时清buttons只保
// 留是/否。
// 参数/局部变量：value 为端口说明缓冲；i 为两方索引。
// 边界与易错点：绘图之外还登记按钮，实际执行仍在onlineAction。输入框本体由Win32控件绘制。
// 从上层理解：main.cpp:595 (render)。
void drawOnlinePage() {
    putimage(0,0,background);setfillcolor(EGERGB(246,241,224));solidroundrect(350,140,1450,1220,28);  // 联网页面的背景和一个大圆角面板
    // 首次调用定位：main.cpp:128 (text)。
    text(680,190,L"\u8054\u7f51\u5bf9\u5f08",58);
    if(netStage==0) {
        // 首次调用定位：main.cpp:137 (addButton)。
        addButton(A_NET_HOST,680,420,440,90,L"\u521b\u5efa\u623f\u95f4");addButton(A_NET_JOIN,680,550,440,90,L"\u52a0\u5165\u623f\u95f4");  // 创建房间 / 加入房间 / 返回
        addButton(A_NET_BACK,680,750,440,90,L"\u8fd4\u56de");text(510,960,L"\u53cc\u65b9\u5148\u8fde\u63a5Tailscale\uff0c\u623f\u4e3b\u4fdd\u6301\u5728\u7ebf\u3002",32);  // 提醒玩家双方都要先连上 Tailscale
    } else if(netStage==1 || netStage==2) {
        text(620,320,netStage==1?L"\u521b\u5efa\u623f\u95f4 \u00b7 \u623f\u4e3b\u6267\u9ed1":L"\u52a0\u5165\u623f\u95f4 \u00b7 \u4f60\u5c06\u6267\u767d",40);  // 页面标题说明本机将执黑还是执白
        if(netStage==1) {text(420,450,hostAddresses,28);text(430,520,L"\u628aTailscale\u5730\u5740\u548c\u7aef\u53e3\u544a\u8bc9\u670b\u53cb\u3002",30);}  // 房主端显示本机地址，并把端口告诉朋友
        else text(460,425,L"\u623f\u4e3bIPv4\u5730\u5740",32);  // 朋友端显示输入标签
        text(460,600,L"\u7aef\u53e3",32);  // 端口输入标签
        addButton(A_NET_CONNECT,510,790,320,80,netError.empty()?(netStage==1?L"\u521b\u5efa":L"\u8fde\u63a5"):L"\u91cd\u8bd5");addButton(A_NET_BACK,970,790,320,80,L"\u8fd4\u56de");  // 连接按钮；失败过就显示「重试」
        text(420,980,netError,28);  // 错误提示文字
    } else if(netStage==3) {
        text(610,460,netHosting?L"\u6b63\u5728\u521b\u5efa\u623f\u95f4\u2026\u2026":L"\u6b63\u5728\u8fde\u63a5\u623f\u4e3b\u2026\u2026",40);  // 正在创建或正在连接
        addButton(A_NET_CANCEL,690,700,420,80,L"\u53d6\u6d88\u8fde\u63a5");  // 取消按钮
    } else if(netStage==5) {text(500,520,netEndNotice,36);text(630,650,L"\u5373\u5c06\u8fd4\u56de\u8054\u7f51\u83dc\u5355",32);}  // 结束/断线后的提示
    else {
        // 首次调用定位：record_store.h:51 (fromUtf8)。
        text(470,335,hostAddresses.empty()?L"\u623f\u4e3b\u5730\u5740\uff1a"+fromUtf8(netAddress):netHosting?hostAddresses:L"\u623f\u4e3b\u5730\u5740\uff1a"+fromUtf8(netAddress),30);  // 房主显示自己的地址，朋友显示房主地址
        wchar_t value[80];std::swprintf(value,80,L"\u7aef\u53e3 %d \u00b7 \u9ed1\u68cb\u5148\u4e0b",netPort);text(600,395,value,32);  // 端口和先手说明
        for(int i=0;i<2;++i) text(540,500+i*100,(i==0?std::wstring(L"\u623f\u4e3b \u00b7 \u9ed1\u68cb\uff1a"):std::wstring(L"\u670b\u53cb \u00b7 \u767d\u68cb\uff1a"))+
            (!netState.present[i]?L"\u7b49\u5f85\u52a0\u5165":netState.ready[i]?L"\u5df2\u51c6\u5907":L"\u672a\u51c6\u5907"),36);  // 房主一栏、朋友一栏
        text(520,785,netRole>=0 && netState.ready[netRole]?L"\u4f60\u5df2\u51c6\u5907\uff0c\u7b49\u5f85\u5bf9\u65b9\u51c6\u5907":L"\u4f60\u5c1a\u672a\u51c6\u5907\uff0c\u8bf7\u70b9\u51fb\u51c6\u5907",30);  // 本机准备状态提示
        text(650,720,netRole==0?L"\u4f60\u6267\u9ed1\uff0c\u9ed1\u68cb\u5148\u4e0b":L"\u4f60\u6267\u767d\uff0c\u7b49\u5f85\u9ed1\u68cb\u5148\u4e0b",32);  // 说明本机执什么颜色
        if(netHosting) addButton(A_NET_COPY,1010,380,320,75,L"\u590d\u5236\u8fde\u63a5\u4fe1\u606f",!lobbyExit && !netLeaving);
        addButton(A_NET_READY,510,850,320,80,netRole>=0 && netState.ready[netRole]?L"\u53d6\u6d88\u51c6\u5907":L"\u51c6\u5907",!lobbyExit && !netPending && !netLeaving);  // 准备 / 取消准备
        addButton(A_NET_LEAVE,970,850,320,80,L"\u79bb\u5f00\u623f\u95f4",!lobbyExit && !netLeaving);  // 离开房间
        if(lobbyExit) {
            buttons.clear();text(690,1000,L"\u662f\u5426\u79bb\u5f00\u623f\u95f4\uff1f",36);  // 确认是否离开
            addButton(A_NET_YES,570,1090,260,75,L"\u662f",!netLeaving);addButton(A_NET_NO,970,1090,260,75,L"\u5426",!netLeaving);  // 是 / 否
        }
    }
}
// 【函数 drawOnlineSidebar】绘制对局右侧联网控制区。
// 执行顺序：先画状态/颜色/步数/RTT；离开中只提示等待；archiveStage非0则画保存流程并return；否则登
// 记悔棋/重开/认输/退出，再按N_SELF/N_PEER显示相应人的询问按钮。
// 参数/局部变量：step 步数缓冲；latency 为RTT输出流；free 表示N_PLAY且非pending；mine 是否为owner；
// prompt 为请求类型对应文字。
// 边界与易错点：不是每个玩家在N_SELF都能回答；只有owner能回答。N_PEER则只有另一方能回答，界面启用
// 条件与apply权限一致。
// 从上层理解：main.cpp:461 (drawGame)。
void drawOnlineSidebar() {
    // 首次调用定位：main.cpp:128 (text)。
    text(1270,150,netLost?L"\u8fde\u63a5\u5df2\u7ed3\u675f":game.result==3?L"\u672c\u5c40\u5e73\u5c40":game.result?  // 顶部大字：连接状态、胜负、该谁落子
        (game.result==netRole+1?L"\u4f60\u83b7\u80dc\u4e86\uff01":L"\u5bf9\u65b9\u83b7\u80dc\uff01"):netState.phase==N_PLAY?
        (game.turn==netRole+1?L"\u8f6e\u5230\u4f60\u843d\u5b50":L"\u7b49\u5f85\u5bf9\u65b9\u843d\u5b50"):L"\u68cb\u5c40\u5df2\u6682\u505c",42);
    text(1270,210,netRole==0?L"\u4f60\u6267\u9ed1 \u00b7 \u623f\u4e3b":L"\u4f60\u6267\u767d \u00b7 \u670b\u53cb",30);  // 本机身份说明
    wchar_t step[80];std::swprintf(step,80,L"\u5df2\u4e0b %u \u6b65",static_cast<unsigned>(game.moves.size()));text(1270,260,step,28);  // 已下多少手
    std::wostringstream latency;latency<<L"\u5ef6\u8fdf\uff1a";if(netLatency<0) latency<<L"\u6d4b\u91cf\u4e2d";else latency<<netLatency<<L" ms";  // 延迟文字
    text(1500,260,netLost?L"\u5df2\u65ad\u5f00":latency.str(),26);  // 断线时就显示已断开，否则显示延迟
    if(netLeaving) {text(1250,1050,L"\u6b63\u5728\u901a\u77e5\u5bf9\u65b9\uff0c\u8bf7\u7a0d\u5019\u2026\u2026",28);return;}
    if(archiveStage) {
        // 首次调用定位：match_metadata.h:34 (reasonText)。
        text(1250,340,game.result?reasonText(netState.reason):L"\u672a\u5b8c\u590d\u76d8\uff0c\u4e0d\u53ef\u7ee7\u7eed\u5bf9\u5c40",30);  // 这一局的结果原因，或者「未完不可继续」的说明
        if(!netEndNotice.empty()) {
            text(1250,405,netEndNotice,26);text(1250,455,L"\u5b8c\u6210\u672c\u673a\u4fdd\u5b58\u9009\u62e9\u540e\u8fd4\u56de",26);  // 对方已经离开时的提示
        // 首次调用定位：online_ui.h:564 (opponentProgress)。
        } else text(1250,405,opponentProgress(),26);  // 否则显示对方正在进行到哪一步
        if(archiveStage==1) {
            text(1250,610,L"\u662f\u5426\u4fdd\u5b58\u5230\u672c\u673a\u4f9b\u4ee5\u540e\u590d\u76d8\uff1f",30);  // 询问是否保存
            // 首次调用定位：main.cpp:137 (addButton)。
            addButton(A_NET_SAVE_YES,1260,900,220,75,L"\u662f");addButton(A_NET_SAVE_NO,1500,900,220,75,L"\u5426");
        } else if(archiveStage==2) {
            text(1260,650,L"\u8bf7\u4e3a\u672c\u68cb\u5c40\u547d\u540d\uff1a",32);text(1260,800,L"\u6700\u591a24\u5b57\uff0c\u652f\u6301\u4e2d\u6587",28);  // 命名提示与字数限制说明
            addButton(A_NET_WRITE,1260,900,220,75,L"\u4fdd\u5b58");addButton(A_NET_NAME_CANCEL,1500,900,220,75,L"\u53d6\u6d88");
        } else if(archiveStage==3) {
            text(1290,620,L"\u8981\u4e0d\u8981\u518d\u6765\u4e00\u5c40\uff1f",36);  // 询问要不要再来一局
            addButton(A_NET_AGAIN,1260,900,220,75,L"\u662f");addButton(A_NET_HOME,1500,900,220,75,L"\u5426");
        } else if(archiveStage==4) {
            text(1250,600,L"\u4f60\u7684\u9009\u62e9\u5df2\u63d0\u4ea4\uff0c\u7b49\u5f85\u5bf9\u65b9",30);  // 等待对方
            addButton(A_NET_HOME,1290,850,400,80,L"\u53d6\u6d88\u5e76\u9000\u51fa");
        } else text(1280,650,L"\u4e24\u79d2\u540e\u8fd4\u56de\u4e3b\u754c\u9762",32);  // 两秒后返回主界面
        return;
    }
    bool free=netState.phase==N_PLAY && !netPending;
    // 首次调用定位：network_protocol.h:66 (NetState::lastMove)。
    addButton(A_NET_UNDO,1290,325,400,70,L"\u8bf7\u6c42\u6094\u68cb\uff08\u4e00\u56de\u5408\uff09",free && netState.lastMove(netRole)>=0);
    addButton(A_NET_RESTART,1290,410,400,70,L"\u8bf7\u6c42\u91cd\u5f00",free);  // 请求重开
    addButton(A_NET_RESIGN,1290,495,400,70,L"\u8ba4\u8f93",free);  // 认输
    addButton(A_NET_EXIT,1290,580,400,70,L"\u9000\u51fa\u5bf9\u5c40",free);  // 退出对局
    if(netPending) text(1250,1050,netMovePending?L"\u843d\u5b50\u786e\u8ba4\u4e2d\u2026\u2026":L"\u7b49\u5f85\u670d\u52a1\u7aef\u786e\u8ba4\u2026\u2026",28);
    if(netLatency>500 || (netPending && GetTickCount64()-netPendingAt>1500)) text(1250,1100,L"\u7f51\u7edc\u8f83\u6162\uff0c\u6b63\u5728\u7b49\u5f85\u786e\u8ba4",26);
    if(netState.phase==N_SELF || netState.phase==N_PEER) {
        bool mine=netState.owner==netRole;
        if(netState.phase==N_SELF && mine) {
            const wchar_t* prompt=netState.kind==1?L"\u662f\u5426\u5411\u5bf9\u65b9\u53d1\u8d77\u91cd\u5f00\u8bf7\u6c42\uff1f":netState.kind==2?L"\u9000\u51fa\u540e\u4e0d\u80fd\u6062\u590d\uff0c\u662f\u5426\u9000\u51fa\uff1f":  // 按请求类型显示对应的确认文字
                netState.kind==3?L"\u662f\u5426\u7533\u8bf7\u64a4\u56de\u81ea\u5df1\u6700\u8fd1\u4e00\u56de\u5408\uff1f":L"\u786e\u5b9a\u8ba4\u8f93\u5417\uff1f";  // kind=3显示悔棋确认；其他剩余类型kind=4显示认输确认。
            text(1250,730,prompt,28);  // 询问玩家是否同意
        } else if(netState.phase==N_PEER && !mine) text(1250,730,netState.kind==3?L"\u5bf9\u65b9\u8bf7\u6c42\u6094\u68cb\uff0c\u662f\u5426\u540c\u610f\uff1f":L"\u5bf9\u65b9\u8bf7\u6c42\u91cd\u5f00\uff0c\u662f\u5426\u540c\u610f\uff1f",28);
        else text(1250,730,netState.phase==N_PEER?L"\u7b49\u5f85\u5bf9\u65b9\u786e\u8ba4\u8bf7\u6c42\u2026\u2026":L"\u5bf9\u65b9\u6b63\u5728\u786e\u8ba4\u64cd\u4f5c\u2026\u2026",28);  // 等待对方确认或对方正在确认
        if((netState.phase==N_SELF && mine) || (netState.phase==N_PEER && !mine)) {
            addButton(A_NET_YES,1260,900,220,75,L"\u662f",!netPending);addButton(A_NET_NO,1500,900,220,75,L"\u5426",!netPending);  // 是 / 否
        }
    }
}
#endif
