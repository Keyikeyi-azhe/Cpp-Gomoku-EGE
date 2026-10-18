// 【建议阅读 03 / 结果与结束原因】match_metadata.h
// 先区分棋盘推导结果与对局声明结果，再看 EndReason -> validResult。
// reasonText、matchTime 只负责展示文字；认输可有胜者而没有五连。
// 下一站：record_store.h 的 GameRecord、encodeRecord、parseRecord。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_MATCH_METADATA_H
#define GOMOKU_MATCH_METADATA_H
#include <string>
#include <ctime>
#include <cwchar>
// 【类型 EndReason】棋局结束原因：0未知，1连五，2平局，3认输，4主动退出，5断线，6协商重开。与网络
// 连接的LeaveReason不同；例如认输后拒绝再战，两个原因分别保存。
enum EndReason { END_UNKNOWN, END_FIVE, END_DRAW, END_RESIGN, END_QUIT, END_DISCONNECT, END_RESTART };
// 【函数 validResult】检查棋盘推导结果 boardResult 与记录 result、reason 是否相容。
// 执行顺序：先范围检查；认输要求盘面未结束且一方胜；连五和平局要求吻合；退出/断线/重开要求两结果均
// 为 0；未知原因要求相等。
// 参数/局部变量：boardResult 是按历史重放得到的结果；result 是文件/消息声称的结果；reason 是结束原
// 因。
// 从上层理解：network_protocol.h:189 (netDecode)；record_store.h:96 (parseRecord)。
inline bool validResult(int boardResult,int result,int reason) {
    if(reason<END_UNKNOWN || reason>END_RESTART || result<0 || result>3) return false;  // 先做范围检查：原因或结果只要超出合法区间就判非法；符号 || 读作「或者」
    if(reason==END_RESIGN) return boardResult==0 && (result==1 || result==2);  // 认输：棋盘上没有五连(0)，但确实有一方获胜，所以 result 必须是 1 或 2；符号 && 读作「并且」
    if(reason==END_FIVE) return boardResult==result && (result==1 || result==2);  // 连五获胜：棋盘上的五连结果必须与记录的结果完全对得上，而且必须真有一方赢
    if(reason==END_DRAW) return boardResult==3 && result==3;  // 满盘平局：棋盘判平(3)，记录也必须是平(3)
    if(reason>=END_QUIT) return boardResult==0 && result==0;  // 主动退出、连接中断、协商重开这三种：棋盘上无胜负(0)，也没有记录结果(0)
    return boardResult==result;  // 其余情况(原因未记录)：只要求棋盘结果与记录结果一致
}
// 【函数 reasonText】将 reason 转成中文宽字符串常量。
// 执行顺序：switch 映射六种已知原因；default 提示旧记录未记录。
// 从上层理解：main.cpp:461 (drawGame)；online_ui.h:624 (drawOnlineSidebar)。
inline const wchar_t* reasonText(int reason) {
    switch(reason) {  // switch 按 reason 的值跳到对应的 case 分支
    case END_FIVE:return L"\u8fde\u4e94\u83b7\u80dc";  // 连五获胜
    case END_DRAW:return L"\u6ee1\u76d8\u5e73\u5c40";  // 满盘平局
    case END_RESIGN:return L"\u8ba4\u8f93\u7ed3\u675f";  // 认输结束
    case END_QUIT:return L"\u4e3b\u52a8\u9000\u51fa";  // 主动退出
    case END_DISCONNECT:return L"\u8fde\u63a5\u4e2d\u65ad";  // 连接中断
    case END_RESTART:return L"\u534f\u5546\u91cd\u5f00";  // 协商重开
    default:return L"\u65e7\u8bb0\u5f55\uff1a\u7ed3\u675f\u539f\u56e0\u672a\u8bb0\u5f55";  // 旧记录：结束原因未记录（default 是上面都没匹配时走的分支）
    }
}
// 【函数 matchTime】把 Unix 秒 timestamp 格式化为本机当地时间。
// 执行顺序：0 返回旧档提示；转 time_t；localtime 拆解；失败返回无法显示；swprintf 生成日期时间。
// 参数/局部变量：value 为 time_t；t 为 tm 指针；text[80] 为结果缓冲；tm_year 需加 1900，tm_mon 需
// 加 1。
// 从上层理解：main.cpp:461 (drawGame)。
inline std::wstring matchTime(unsigned long long timestamp) {
    if(!timestamp) return L"\u5bf9\u5c40\u65f6\u95f4\uff1a\u65e7\u8bb0\u5f55\u672a\u8bb0\u5f55";  // 对局时间：旧记录未记录。0 就表示「从没记过时间」
    std::time_t value=static_cast<std::time_t>(timestamp);std::tm* t=std::localtime(&value);  // 先转成时间类型，再按本机时区拆成年月日时分秒
    if(!t) return L"\u5bf9\u5c40\u65f6\u95f4\uff1a\u65e0\u6cd5\u663e\u793a";  // 对局时间：无法显示。localtime 失败时走的兜底分支
    wchar_t text[80];std::swprintf(text,80,L"%04d-%02d-%02d %02d:%02d:%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
    return text;  // 宽字符串会自动转换成 std::wstring 返回
}
#endif
