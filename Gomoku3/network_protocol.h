// 【建议阅读 07 / 网络规则状态机】network_protocol.h
// NetPhase/LeaveReason -> NetState成员 -> start/apply -> netEncode/netDecode。
// 座位0/1对应棋色1/2；服务端裁决，客户端收到快照后才更新显示盘。
// 下一站：network_transport.h；理解正文如何被包装成TCP长度帧。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_NETWORK_PROTOCOL_H
#define GOMOKU_NETWORK_PROTOCOL_H
#include <sstream>
#include <string>
#include "game_logic.h"
#include "match_metadata.h"

// 【类型 NetPhase】房间阶段：N_LOBBY=0等待室；N_PLAY=1对局；N_SELF=2发起者自己确认；N_PEER=3对方确
// 认；N_FINISH=4终局保存/再战；N_RESTART=5双方同意重开后处理保存；N_CLOSED=6关闭。
enum NetPhase { N_LOBBY, N_PLAY, N_SELF, N_PEER, N_FINISH, N_RESTART, N_CLOSED };
// 【类型 LeaveReason】连接离开原因：0无，1朋友离开等待室，2房主关房，3退出对局，4拒绝再战，5取消已
// 提交的等待，6掉线。不能与EndReason的同值数字混用。
enum LeaveReason { LEAVE_NONE, LEAVE_LOBBY, LEAVE_HOST, LEAVE_QUIT, LEAVE_DECLINE, LEAVE_CANCEL, LEAVE_DROP };
// 【类型 NetState】房间的规则状态，服务端是权威；客户端只保存收到的副本。game见
// game_logic.h:20 (LocalGame)，phase见network_protocol.h:18 (NetPhase)，reason见
// match_metadata.h:16 (EndReason)，leaveReason见network_protocol.h:21 (LeaveReason)。两个座
// 位数组描述同一盘棋的两名玩家，不是两盘棋。
struct NetState {
    // 类型定义：game_logic.h:20 (LocalGame)。
    LocalGame game;  // 棋盘与落子历史
    // 语句说明：round局号；phase阶段；owner发起座位0/1；kind请求1..4；question问答号；reason棋局结束原
    // 因。
    int round,phase,owner,kind,question,reason;
    int leaveReason,leaver,exitSerial,saveStage[2];  // leaveReason连接离开原因；leaver离开座位；exitSerial离开流水；saveStage[2]为双方保存进度0..3。
    unsigned long long started;  // 本局开始时的 Unix 时间戳
    bool present[2],ready[2],done[2],again[2];  // present 是否在房；ready 是否已准备；done 是否已处理保存；again 是否同意再战
    // 【函数 NetState::NetState】构造等待室状态。
    // 执行顺序：round=0、phase=N_LOBBY；标量流水和原因复位；两方数组全false，saveStage=0；game 自动创
    // 建空盘。
    // 从上层理解：online_ui.h:94 (onlineEnter)。
    // 类型定义：network_protocol.h:26 (NetState)。
    NetState():round(0),phase(N_LOBBY),owner(0),kind(0),question(0),reason(END_UNKNOWN),
        leaveReason(0),leaver(0),exitSerial(0),started(0) {
        for(int i=0;i<2;++i) {present[i]=ready[i]=done[i]=again[i]=false;saveStage[i]=0;}  // 两名玩家的标志位与保存进度一律清零
    }
    // 【函数 NetState::start】开始新一局。
    // 执行顺序：局号加1；清棋盘；phase=N_PLAY；清请求类型及原因；记当前Unix秒；清准备、保存、再战标志。
    // 参数/局部变量：i 为双方座位数组下标。
    // 边界与易错点：present 保留；question、exitSerial 也不归零，流水号继续递增。owner=0 只表示默认值，
    // 不代表正在询问房主。
    // 从上层理解：network_protocol.h:103 (NetState::apply)。
    void start() {
        // 首次调用定位：game_logic.h:39 (LocalGame::reset)。
        ++round;game.reset();phase=N_PLAY;owner=kind=0;reason=END_UNKNOWN;leaveReason=0;  // 新局清棋盘、请求类型和原因；保留present、question、exitSerial等跨局状态。
        started=static_cast<unsigned long long>(std::time(NULL));  // 记住开局的时刻，存档时要用
        for(int i=0;i<2;++i) {ready[i]=done[i]=again[i]=false;saveStage[i]=0;}  // 就绪、已处理保存、同意再战这三类标志复位；保存进度也归零
    }
    // 【函数 NetState::finish】将当前局转为结束阶段。
    // 执行顺序：reason=why；phase=N_FINISH；双方 saveStage=1。
    // 参数/局部变量：why 为 END_FIVE、END_DRAW 或 END_RESIGN。
    // 边界与易错点：它不计算胜者；连五由 place 设置 result，认输由 apply 设置 result。
    // 从上层理解：network_protocol.h:103 (NetState::apply)。
    void finish(int why) {reason=why;phase=N_FINISH;saveStage[0]=saveStage[1]=1;}
    // 【函数 NetState::lastMove】寻找 player 座位最近一手下标，无则 -1。
    // 执行顺序：从历史末尾找 stone==player+1。
    // 参数/局部变量：player 为0/1座位；i 为有符号历史下标。
    // 从上层理解：network_protocol.h:103 (NetState::apply)；online_ui.h:624 (drawOnlineSidebar)。
    int lastMove(int player) const {
        for(int i=static_cast<int>(game.moves.size())-1;i>=0;--i) if(game.moves[i].stone==player+1) return i;
        return -1;
    }
    // 【函数 NetState::closeRoom】记录座位 p 因 why 发起离开。
    // 执行顺序：写 leaver、leaveReason，增加 exitSerial；朋友在等待室离开时只清朋友在房/准备及房主准备，
    // 保留房间；其他场景设置未完结束原因并转 N_CLOSED。
    // 参数/局部变量：p 为离开者0/1；why 为 LeaveReason。
    // 边界与易错点：对已结束局或重开阶段保留原棋局 reason。该函数不总清 present，真正连接断开由 leave
    // 清理。
    // 从上层理解：network_protocol.h:103 (NetState::apply)；
    // network_protocol.h:89 (NetState::leave)。
    void closeRoom(int p,int why) {
        leaver=p;leaveReason=why;++exitSerial;  // 记下离开的人和原因，并让流水号加一
        if(phase==N_LOBBY && p==1) {present[p]=ready[p]=false;ready[0]=false;return;}  // 等待室里朋友离开的特殊处理：只让他退出，房主留在房间里继续等别人
        if(!game.result && phase!=N_RESTART) reason=why==LEAVE_DROP?END_DISCONNECT:END_QUIT;  // 只为未完且不处于重开阶段的棋局补退出/断线原因，保留已明确的原原因。
        phase=N_CLOSED;  // 房间进入已关闭状态
    }
    // 【函数 NetState::leave】处理底层连接消失。
    // 执行顺序：若已不在房就返回；清 present/ready；尚未关闭的房间交 closeRoom(...,LEAVE_DROP)。
    // 参数/局部变量：p 为失联座位。
    // 边界与易错点：房间已关闭时不重写离开原因，防止“拒绝再战”被随后断线覆盖。
    // 从上层理解：server_main.cpp:22 (main)。
    void leave(int p) {
        if(!present[p]) return;  // 本来就不在房，不必重复处理
        present[p]=ready[p]=false;  // 清掉这个人的在房与准备状态
        // 首次调用定位：network_protocol.h:78 (NetState::closeRoom)。
        if(phase!=N_CLOSED) closeRoom(p,LEAVE_DROP);
    }
    // 【函数 NetState::apply】检查并执行座位 p 发来的 message；返回是否接受；event 输出可选提示事件。
    // 执行顺序：检查座位/在房与命令格式；READY 单独处理；其余要求局号匹配；按命令验证阶段、手数、权限
    // 和问答编号，再改变状态。
    // 参数/局部变量：in 为消息流；cmd 指令；extra 检测多余字段；r 局号；a/b/c 是按命令解释的整数；at
    // 是悔棋目标历史下标。
    // 边界与易错点：N_SELF由申请者自己确认，N_PEER由另一方回答。MOVE没有棋色字段；根据座位和game.turn
    // 判权限。
    // 从上层理解：server_main.cpp:22 (main)。
    bool apply(int p,const std::string& message,std::string& event) {
        event.clear();if(p<0 || p>1 || !present[p]) return false;  // 先清空事件；发送者编号或「是否在房」不合法就直接拒绝
        std::istringstream in(message);std::string cmd,extra;int r=0,a=0,b=0,c=0;  // 把消息包成输入流，以便像读文件一样逐项读取
        if(!(in>>cmd)) return false;  // 连指令名都读不到，说明是空消息
        // 【阅读提示】READY不带局号；两方在房且都准备才开局。其余业务命令必须携带round。
        if(cmd=="READY") {
            if(!(in>>a) || (in>>extra) || phase!=N_LOBBY || (a!=0 && a!=1)) return false;  // 准备指令只在等待室有效，取值只能是 0 或 1
            // 首次调用定位：network_protocol.h:50 (NetState::start)。
            ready[p]=a!=0;if(present[0] && present[1] && ready[0] && ready[1]) start();return true;  // 两人都在房且都点了准备，立即开局
        }
        if(!(in>>r) || r!=round) return false;
        // 【阅读提示】MOVE round moveCount row col：服务端从p决定颜色，不接受客户端任意指定执棋方。
        if(cmd=="MOVE") {
            if(!(in>>a>>b>>c) || (in>>extra) || phase!=N_PLAY || game.turn!=p+1 ||  // a已有手数、b行、c列；没有颜色字段，轮次必须对应发送座位p+1。
               // 首次调用定位：game_logic.h:79 (LocalGame::place)。
               a!=static_cast<int>(game.moves.size()) || !game.place(b,c)) return false;  // 手数必须等于当前已有手数(不能跳号)，且这一手必须真的能落下去
            // 首次调用定位：network_protocol.h:61 (NetState::finish)。
            if(game.result) finish(game.result==3?END_DRAW:END_FIVE);  // 落子后如果分出胜负，结束本局，并区分是连五获胜还是满盘平局
        // 【阅读提示】ASK round moveCount kind：1重开、2退出、3悔棋、4认输；先进入本人确认。
        } else if(cmd=="ASK") {
            if(!(in>>a>>b) || (in>>extra) || phase!=N_PLAY || a!=static_cast<int>(game.moves.size()) || b<1 || b>4) return false;  // 要求手数正确、请求类型在 1 到 4 之间
            // 首次调用定位：network_protocol.h:66 (NetState::lastMove)。
            if(b==3 && lastMove(p)<0) return false;  // 一手未下时不允许申请悔棋
            owner=p;kind=b;++question;phase=N_SELF;  // 记录申请者和类型，分配新问答号；N_SELF先让申请者本人确认。
        // 【阅读提示】ANSWER round question yes：question必须匹配当前请求；本人/对方由phase限定。
        } else if(cmd=="ANSWER") {
            if(!(in>>a>>b) || (in>>extra) || a!=question || (b!=0 && b!=1) ||
               !((phase==N_SELF && p==owner) || (phase==N_PEER && p!=owner))) return false;  // N_SELF仅owner能回答；N_PEER仅另一方能回答；旧question被拒绝。
            // 语句说明：本人取消或对方拒绝均恢复对局；只有对方拒绝时给申请者额外拒绝事件。
            if(!b) {if(phase==N_PEER) event=kind==3?"UNDO_REFUSED":"REFUSED";phase=N_PLAY;}
            // 首次调用定位：network_protocol.h:78 (NetState::closeRoom)。
            else if(kind==2) closeRoom(owner,LEAVE_QUIT);  // 申请者确认退出即可关房，不要求对手批准。
            else if(kind==4) {game.result=2-owner;finish(END_RESIGN);}  // 申请者确认认输；座位0认输则白2胜，座位1认输则黑1胜。
            else if(phase==N_SELF) phase=N_PEER;  // 其余是重开/悔棋；本人同意后转N_PEER，等待对手答复。
            else if(kind==3) {
                int at=lastMove(owner);if(at<0) return false;  // 找到申请者最近一手；找不到说明状态异常
                // 首次调用定位：game_logic.h:97 (LocalGame::undo)。
                while(static_cast<int>(game.moves.size())>at) game.undo();  // 撤掉下标at的申请者一手及其后的应答；最终size等于at。
                phase=N_PLAY;event="UNDO_ACCEPTED";  // 回到对局中，并通知对方「悔棋已完成」
            } else {phase=N_RESTART;reason=END_RESTART;done[0]=done[1]=false;saveStage[0]=saveStage[1]=1;}  // 其余情况是重开：进入重开阶段，双方重新走一遍保存流程
        // 【阅读提示】SAVE round stage：1选择保存、2命名、3本机完成；同步状态，不传对方的文件名或文件内容。
        } else if(cmd=="SAVE") {
            if(!(in>>a) || (in>>extra) || (phase!=N_FINISH && phase!=N_RESTART && phase!=N_CLOSED) || a<1 || a>3 || done[p] || again[p]) return false;
            saveStage[p]=a;  // 记录这一方的保存进度
        // 【阅读提示】重开用DONE屏障，双方本机保存处理完才reset；终局再战用AGAIN，不要混用。
        } else if(cmd=="DONE") {
            if((in>>extra) || phase!=N_RESTART || saveStage[p]!=3) return false;
            done[p]=true;if(done[0] && done[1]) start();  // 标记此人已处理完；两边都完成就开局
        } else if(cmd=="AGAIN") {
            if((in>>extra) || phase!=N_FINISH || saveStage[p]!=3) return false;
            again[p]=true;if(again[0] && again[1] && present[0] && present[1]) start();  // 标记此人已同意；两边都同意且都在房就开局
        } else if(cmd=="CLOSE") {
            if(!(in>>a) || (in>>extra)) return false;  // 先做出基本格式校验
            if(phase==N_LOBBY && a==LEAVE_LOBBY) closeRoom(p,p==0?LEAVE_HOST:LEAVE_LOBBY);  // 等待室里离开：房主离开记为关房，朋友离开记为普通离开
            else if(phase==N_FINISH && a==LEAVE_DECLINE && saveStage[p]==3 && !again[p]) closeRoom(p,a);  // 一局结束后拒绝再战：此时才允许关房
            else if(((phase==N_FINISH && again[p]) || (phase==N_RESTART && done[p])) && a==LEAVE_CANCEL) closeRoom(p,a);  // 已经同意再战又反悔取消：同样记为关房
            else return false;  // 其余情况的 CLOSE 一律视为非法
        } else return false;  // 未知指令一律拒绝
        return true;
    }
};
// 【函数 netEncode】把 NetState s 编成一条 STATE 消息。
// 执行顺序：写局号/阶段/询问；写两方4类布尔标志；写手数和每手行列；最后写结果、原因、时间、保存进
// 度和离开信息。
// 参数/局部变量：o 为输出流；i 为座位或手序索引。
// 边界与易错点：消息只传每手行列；颜色由黑白交替重放恢复。快照不是把结构体内存 memcpy 到网络上。
// 从上层理解：server_main.cpp:22 (main)。
// 类型定义：network_protocol.h:26 (NetState)。
inline std::string netEncode(const NetState& s) {
    std::ostringstream o;o<<"STATE "<<s.round<<' '<<s.phase<<' '<<s.owner<<' '<<s.kind<<' '<<s.question;  // 第一段：局号、阶段、发起者、类型、问答编号
    for(int i=0;i<2;++i) o<<' '<<s.present[i]<<' '<<s.ready[i]<<' '<<s.done[i]<<' '<<s.again[i];  // 两名玩家的四个标志位
    o<<' '<<s.game.moves.size();  // 后面跟着当前的手数，再跟每一手的行列
    for(size_t i=0;i<s.game.moves.size();++i) o<<' '<<s.game.moves[i].row<<' '<<s.game.moves[i].col;
    o<<' '<<s.game.result<<' '<<s.reason<<' '<<s.started<<' '<<s.saveStage[0]<<' '<<s.saveStage[1]  // 末尾是结果、原因、开局时间、双方保存进度
        <<' '<<s.leaveReason<<' '<<s.leaver<<' '<<s.exitSerial;  // 以及离开原因、离开者、离开流水号
    return o.str();
}
// 【函数 netDecode】解析 message，成功后才整体赋给 result。
// 执行顺序：临时建 s；校验 STATE 头和范围；读双方标志和手数；逐手 place 重建；验证结果原因与其他范
// 围、拒绝多余字段；写最终结果并输出。
// 参数/局部变量：s 临时状态；tag 头标记；extra 多余字段；in 输入流；count 手数；outcome 消息宣称结
// 果；i/r/c 是重放下标与行列。
// 边界与易错点：这里做了多项格式和规则校验，但不是对所有字段组合做完整状态机证明。不要把它描述为能
// 够识别一切恶意状态；权威性主要来自服务器执行 apply。
// 从上层理解：online_ui.h:465 (updateOnline)。
// 类型定义：network_protocol.h:26 (NetState)。
inline bool netDecode(const std::string& message,NetState& result) {
    NetState s;std::string tag,extra;std::istringstream in(message);int count=0,outcome=0;  // 解析中的临时状态；tag 用来核对开头的关键字 STATE
    if(!(in>>tag>>s.round>>s.phase>>s.owner>>s.kind>>s.question) || tag!="STATE" ||
       s.round<0 || s.phase<0 || s.phase>N_CLOSED || s.owner<0 || s.owner>1 || s.kind<0 || s.kind>4) return false;  // phase为阶段；owner是请求发起座位，不一定是房主；kind为请求类型。
    for(int i=0;i<2;++i) if(!(in>>s.present[i]>>s.ready[i]>>s.done[i]>>s.again[i])) return false;
    if(!(in>>count) || count<0 || count>225) return false;  // 手数必须在 0 到 225 之间
    // 首次调用定位：game_logic.h:79 (LocalGame::place)。
    for(int i=0;i<count;++i) {int r,c;if(!(in>>r>>c) || !s.game.place(r,c)) return false;}
    if(!(in>>outcome>>s.reason>>s.started>>s.saveStage[0]>>s.saveStage[1]>>s.leaveReason>>s.leaver>>s.exitSerial) || (in>>extra) ||
       // 首次调用定位：match_metadata.h:23 (validResult)。
       !validResult(s.game.result,outcome,s.reason) || s.started>7258118400ULL ||
       s.saveStage[0]<0 || s.saveStage[0]>3 || s.saveStage[1]<0 || s.saveStage[1]>3 ||  // 保存进度、离开原因、离开者编号、流水号也都要在合法范围内
       s.leaveReason<0 || s.leaveReason>6 || s.leaver<0 || s.leaver>1 || s.exitSerial<0) return false;
    s.game.result=outcome;result=s;return true;
}
#endif
