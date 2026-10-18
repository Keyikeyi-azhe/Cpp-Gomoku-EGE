// 【建议阅读 04 / 存档】record_store.h
// 先看 GameRecord，再看 encodeRecord 的格式，然后逆向读 parseRecord 的验证过程。
// 最后看 readRecord/writeRecord；读取重放真实落子，写入先自检再用临时文件替换。
// 下一站：replay_logic.h；读懂历史记录怎样还原为任意播放进度。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_RECORD_STORE_H
#define GOMOKU_RECORD_STORE_H
#include <windows.h>
#include <string>
#include <sstream>
#include "game_logic.h"
#include "match_metadata.h"

// 【类型 GameRecord】存档记录把棋局、名称、模式及元数据打包；game类型见
// game_logic.h:20 (LocalGame)，reason取值见match_metadata.h:16 (EndReason)。path仅是本机路径，
// 不写入存档正文；replay为真不一定表示已分胜负，联网未完局也保存为复盘。
struct GameRecord {  // 结构体：把一盘棋的所有信息打包在一起
    std::wstring name, path;  // name为显示名；path为本机读写路径（本项目调用通常用相对路径），不写入正文。
    std::string mode;  // mode 对局模式：LOCAL_TWO 单机 / HUMAN_AI 人机 / ONLINE 联网
    int difficulty, human, reason, version;  // difficulty=0..2；human=1/2；reason见EndReason；version记录读入格式版本。
    unsigned long long started;  // started 对局开始的 Unix 时间戳(秒)
    bool replay;  // true为复盘、false为残局；联网未完记录也只能作为复盘保存。
    // 类型定义：game_logic.h:20 (LocalGame)。
    LocalGame game;  // game 棋盘与落子历史本体
    // 【函数 GameRecord::GameRecord】创建默认单机记录。
    // 执行顺序：mode=LOCAL_TWO，difficulty=0，human=1，reason 未知，version=4，started=0，replay=false；
    // game 自动构造为空棋盘。
    // 参数/局部变量：默认 version=4 不代表本地写出来是 v4；看 encodeRecord。
    // 类型定义：record_store.h:20 (GameRecord)。
    GameRecord():mode("LOCAL_TWO"),difficulty(0),human(1),reason(END_UNKNOWN),version(4),started(0),replay(false) {}
};
// 【函数 toUtf8】wstring → UTF-8 string。
// 执行顺序：空串直接返回；第一次 WideCharToMultiByte 问所需字节数，第二次填入已分配 out。
// 参数/局部变量：s 输入；n 目标字节数；out 目标缓冲。
// 从上层理解：record_store.h:174 (encodeRecord)；online_ui.h:296 (onlineConnect)。
inline std::string toUtf8(const std::wstring& s) {
    if(s.empty()) return "";  // 空串直接返回，省去一次系统调用
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),NULL,0,NULL,NULL);
    std::string out(n,0);
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),&out[0],n,NULL,NULL);
    return out;  // 转好的 UTF-8 字节串就存在这里并返回
}
// 【函数 fromUtf8】UTF-8 string → wstring；无效 UTF-8 返回空串。
// 执行顺序：用 MB_ERR_INVALID_CHARS 严格测长度；n=0 失败；分配 out 后转换。
// 参数/局部变量：s 输入字节；n 目标宽字符代码单元数；out 转换结果。
// 从上层理解：online_ui.h:580 (drawOnlinePage)；online_ui.h:296 (onlineConnect)；
// record_store.h:96 (parseRecord)。
inline std::wstring fromUtf8(const std::string& s) {
    if(s.empty()) return L"";  // 空串直接返回
    // 语句说明：MB_ERR_INVALID_CHARS要求拒绝无效UTF-8；n为所需宽字符代码单元数。
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),NULL,0);
    if(!n) return L"";
    std::wstring out(n,0);
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),&out[0],n);
    return out;
}
// 【函数 recordDirectories】确保 data、unfinished、replays 三个目录存在。
// 执行顺序：创建失败但“已存在”可接受；再查询属性确认实际是目录。
// 参数/局部变量：dirs 为三个路径；i 为索引；a 为属性位集合。
// 从上层理解：main.cpp:669 (main)；online_ui.h:296 (onlineConnect)；
// record_store.h:200 (writeRecord)。
inline bool recordDirectories() {
    const wchar_t* dirs[]={L"data",L"data\\unfinished",L"data\\replays"};
    for(int i=0;i<3;++i) {  // 三个目录依次创建
        if(!CreateDirectoryW(dirs[i],NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        DWORD a=GetFileAttributesW(dirs[i]);
        if(a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    return true;
}
// 【函数 numberLine】读取且校验一行“关键字 整数”；结果写入 value。
// 执行顺序：getline 读整行；istringstream 拆字段；必须 k==key，并且不存在 extra。
// 参数/局部变量：input 输入流；key 期望关键字；value 输出整数引用；line 原行；row 行解析流；k 读到
// 的键；extra 检测多余字段。
// 从上层理解：record_store.h:96 (parseRecord)。
inline bool numberLine(std::istream& input,const char* key,int& value) {
    std::string line,k,extra;
    if(!std::getline(input,line)) return false;
    std::istringstream row(line);
    return (row>>k>>value) && k==key && !(row>>extra);  // 三个条件全满足才算这行合法
}
// 【函数 parseRecord】解析 bytes；replay 指定预期文件类型；全部成功才赋值 output。
// 执行顺序：限制 2 MiB；移除回车；校验头与版本 1..4；读名称与模式；按版本读附加字段；逐步检查手序、
// 棋色并调用 place；最后验证 END、结果、轮次、文件类型。
// 参数/局部变量：normalized 是去回车文本；input 总流；r 临时记录；line/type/extra 当前行、关键字、
// 多余字段；version 为头部版本；head/modeLine/timestamp/number/move 是不同段的解析流；
// size/result/turn/count 为声明的盘宽、结果、回合、手数；stamp 是时间文本；i/index/row/col/stone
// 是第 i 手与它的字段。
// 边界与易错点：v4 只允许 ONLINE 复盘；v1 不能是 HUMAN_AI。只接受非零结果的本地复盘；v3/v4 ONLINE
// 允许 result=0。不应误读成所有版本都允许任意模式。
// 从上层理解：record_store.h:155 (readRecord)；record_store.h:200 (writeRecord)。
// 类型定义：record_store.h:20 (GameRecord)。
inline bool parseRecord(const std::string& bytes,bool replay,GameRecord& output) {
    if(bytes.size()>2*1024*1024) return false;
    std::string normalized;
    for(size_t i=0;i<bytes.size();++i) if(bytes[i]!='\r') normalized+=bytes[i];
    std::istringstream input(normalized);  // 把去掉回车后的内容包成输入流，后面逐行读取
    GameRecord r; r.replay=replay;  // r 是解析中的临时对象；先记下本次读的是不是复盘，供后面分支判断使用
    std::string line,type,extra; int version=0;
    if(!std::getline(input,line)) return false;  // 读第一行，连一行都读不到说明文件是空的
    std::istringstream head(line);
    if(!(head>>type>>version) || (head>>extra) || (version!=1 && version!=2 && version!=3 && version!=4) ||
       type!=(replay?"GOMOKU_LOCAL_REPLAY":"GOMOKU_LOCAL_SAVE")) return false;  // 要求：关键字与类型相符，且版本号在 1 到 4 之间
    if(!std::getline(input,line) || line.substr(0,5)!="NAME ") return false;
    // 首次调用定位：record_store.h:51 (fromUtf8)。
    r.name=fromUtf8(line.substr(5));
    // 语句说明：name.size计Windows UTF-16代码单元；24并非所有Unicode字符都能按24个计。
    if(r.name.empty() || r.name.size()>24 || r.name.find_first_of(L"\t\r\n")!=std::wstring::npos ||
       r.name.find(L'\0')!=std::wstring::npos || r.name.find_first_not_of(L' ')==std::wstring::npos) return false;
    int size=0,result=0,turn=0,count=0;
    // 首次调用定位：record_store.h:79 (numberLine)。
    if(!numberLine(input,"SIZE",size) || size!=15 || !std::getline(input,line)) return false;  // 要求下一行是 "SIZE 15"，也就是确认棋盘尺寸；随后还要再读一行模式行
    std::istringstream modeLine(line);  // 这一行形如 "MODE HUMAN_AI"
    if(!(modeLine>>type>>r.mode) || type!="MODE" || (modeLine>>extra) ||  // 模式必须是三种已知取值之一，否则判为坏档
       (r.mode!="LOCAL_TWO" && r.mode!="HUMAN_AI" && r.mode!="ONLINE")) return false;
    if(version>=2) {
        if(!numberLine(input,"DIFFICULTY",r.difficulty) || r.difficulty<0 || r.difficulty>2 ||
           !numberLine(input,"HUMAN",r.human) || (r.human!=1 && r.human!=2)) return false;  // 难度只能是 0 到 2；玩家颜色只能是 1(黑) 或 2(白)
    } else if(r.mode=="HUMAN_AI") return false;
    r.version=version;  // 记下版本号，后面几个地方都要按版本决定校验规则
    if(version>=4) {
        if(r.mode!="ONLINE" || !replay) return false;
        if(!numberLine(input,"REASON",r.reason) || r.reason<0 || r.reason>END_RESTART || !std::getline(input,line)) return false;  // 结束原因必须在枚举范围内
        std::istringstream timestamp(line);std::string stamp;
        if(!(timestamp>>type>>stamp) || type!="STARTED" || (timestamp>>extra) || stamp.empty() ||  // 要求关键字是 STARTED、时间戳全是数字且不太长
           stamp.size()>10 || stamp.find_first_not_of("0123456789")!=std::string::npos) return false;
        std::istringstream number(stamp);if(!(number>>r.started) || r.started>7258118400ULL) return false;  // 转成整数并限制上限(约合 2200 年)，防止异常的巨大数字
    }
    if(!numberLine(input,"RESULT",result) || !numberLine(input,"TURN",turn) ||
       !numberLine(input,"MOVES",count) || count<0 || count>225) return false;  // 手数越界就判为坏档
    for(int i=0;i<count;++i) {
        int index,row,col,stone;
        if(!std::getline(input,line)) return false;  // 读这一手那一行；读不到说明手数写多了，文件被截断或篡改
        std::istringstream move(line);
        if(!(move>>type>>index>>row>>col>>stone) || type!="MOVE" || (move>>extra) || index!=i+1 ||
           // 首次调用定位：game_logic.h:79 (LocalGame::place)。
           stone!=r.game.turn || !r.game.place(row,col)) return false;  // 关键一步：game.place 会用真实规则走这一手，不合法它自己就会返回 false
    }
    if(!std::getline(input,line) || line!="END" || (input>>extra)) return false;
    // 首次调用定位：match_metadata.h:23 (validResult)。
    // 语句说明：结果/轮次必须与重放及原因相容；v3及以后ONLINE复盘允许未完结果0。
    if(!validResult(r.game.result,result,r.reason) || (version<4 && r.game.result!=result) || r.game.turn!=turn || (replay ? (result==0 && !(version>=3 && r.mode=="ONLINE")) : result!=0)) return false;
    r.game.result=result;output=r; return true;  // 校验通过后采用记录声明的结果；认输结果不能由盘面五连直接推出。
}
// 【函数 readRecord】从 path 读文件并解析，成功补 output.path。
// 执行顺序：CreateFileW 打开；检查长度；一次读取且要求实读长度相等；关闭句柄；交 parseRecord。
// 参数/局部变量：replay 为期望类型；f 文件句柄；size 为 LARGE_INTEGER；data 为字节缓冲；got 为实读
// 字节数；ok 汇总成功状态。
// 从上层理解：main.cpp:289 (loadEntry)；online_ui.h:253 (onlineSave)；
// main.cpp:215 (openList)。
// 类型定义：record_store.h:20 (GameRecord)。
inline bool readRecord(const std::wstring& path,bool replay,GameRecord& output) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return false;  // 打开失败(文件不存在、没有权限等)直接返回假
    LARGE_INTEGER size;  // LARGE_INTEGER 是 Windows 用来表示 64 位长度的类型
    if(!GetFileSizeEx(f,&size) || size.QuadPart<=0 || size.QuadPart>2*1024*1024) {CloseHandle(f);return false;}
    std::string data(static_cast<size_t>(size.QuadPart),0); DWORD got=0;
    bool ok=ReadFile(f,&data[0],static_cast<DWORD>(data.size()),&got,NULL)!=0 && got==data.size();
    CloseHandle(f);  // 读完后立刻关闭句柄
    // 首次调用定位：record_store.h:96 (parseRecord)。
    if(!ok || !parseRecord(data,replay,output)) return false;  // 读取失败或解析失败都算读档失败
    output.path=path; return true;  // 把路径一并记进结果，界面上删除存档时要用到它
}
// 【函数 encodeRecord】将记录 r 编成存档文本。
// 执行顺序：写文件头、名称、SIZE/MODE/难度/颜色；联网加原因与开始时间；写 RESULT/TURN/MOVES；逐手
// 写 MOVE；最后 END。
// 参数/局部变量：out 为字符串输出流；i 为手序下标；m 是这一手的 const 引用。
// 边界与易错点：本地写 v2；联网写 v4。它只序列化，真正写盘前还要由 writeRecord 调 parseRecord 自检。
// 从上层理解：record_store.h:200 (writeRecord)。
// 类型定义：record_store.h:20 (GameRecord)。
inline std::string encodeRecord(const GameRecord& r) {
    std::ostringstream out;  // ostringstream 是内存里的字符串输出流，可像写文件一样往里追加
    // 语句说明：实际输出版本由mode决定：ONLINE用v4，其余用v2，不直接采用r.version。
    out<<(r.replay?"GOMOKU_LOCAL_REPLAY ":"GOMOKU_LOCAL_SAVE ")<<(r.mode=="ONLINE"?4:2)<<"\n";
    // 首次调用定位：record_store.h:39 (toUtf8)。
    out<<"NAME "<<toUtf8(r.name)<<"\nSIZE 15\nMODE "<<r.mode<<"\nDIFFICULTY "<<r.difficulty;
    out<<"\nHUMAN "<<r.human;  // 玩家颜色单独写一行
    if(r.mode=="ONLINE") out<<"\nREASON "<<r.reason<<"\nSTARTED "<<r.started;
    out<<"\nRESULT "<<r.game.result<<"\nTURN "<<r.game.turn;  // 结果与轮到谁
    out<<"\nMOVES "<<r.game.moves.size()<<"\n";  // 接下来是手数，然后是每一手
    for(size_t i=0;i<r.game.moves.size();++i) {
        // 类型定义：game_logic.h:16 (Move)。
        const Move& m=r.game.moves[i];out<<"MOVE "<<i+1<<' '<<m.row<<' '<<m.col<<' '<<m.stone<<"\n";
    }  // 最后用 END 收尾
    out<<"END\n";return out.str();  // str() 取出流里累积的全部文本
}
// 【函数 writeRecord】把 r 安全写入磁盘；overwrite 为空表示创建新文件。
// 执行顺序：建目录；编码后反向解析自检；生成时间名和序号；CREATE_NEW 建同目录 .tmp；写完并刷新；
// MoveFileExW 改为正式名；失败清理临时文件。
// 参数/局部变量：bytes 为编码文本；check 为自检对象；now 为当前本地时间；sequence 为 0..999 避让序
// 号；filename 为文件名缓冲；path 为最终路径；temp 为临时路径；f 为写句柄；written 实写长度；ok 为
// 状态。
// 边界与易错点：新记录不会因显示名相同覆盖旧文件；只有显式 overwrite 才替换指定路径。先临时写再替
// 换可避免直接把原档写到一半；仍可能因权限或 I/O 错误失败。
// 从上层理解：online_ui.h:253 (onlineSave)；main.cpp:247 (saveGame)。
// 类型定义：record_store.h:20 (GameRecord)。
inline bool writeRecord(GameRecord& r,const std::wstring& overwrite=L"") {
    // 首次调用定位：record_store.h:65 (recordDirectories)。
    if(!recordDirectories()) return false;  // 目录不存在就先建好，建不出来自然写不成功
    // 首次调用定位：record_store.h:174 (encodeRecord)。
    // 语句说明：bytes为将写出的文本；check仅用来验证这些文本能被本程序正确读回。
    std::string bytes=encodeRecord(r); GameRecord check;
    // 首次调用定位：record_store.h:96 (parseRecord)。
    if(!parseRecord(bytes,r.replay,check)) return false;  // 自检：能解析回来才允许落盘
    SYSTEMTIME now;GetLocalTime(&now);
    for(int sequence=0;sequence<1000;++sequence) {
        wchar_t filename[256];  // filename 用来装拼出来的文件名
        std::swprintf(filename,256,L"data\\%ls\\%04d%02d%02d_%02d%02d%02d_%03d_%03d.%ls",
            r.replay?L"replays":L"unfinished",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,  // 拼接路径的后续参数：目录名、各项时间数字、序号、扩展名
            now.wSecond,now.wMilliseconds,sequence,r.replay?L"greplay":L"gsave");  // %03d 补零 3 位的毫秒和序号；%ls 插入判定出来的扩展名
        std::wstring path=overwrite.empty()?filename:overwrite;
        if(overwrite.empty() && GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) continue;
        std::wstring temp=path+L".tmp";  // 临时文件放在目标旁；避免直接覆盖原文件后写入失败留下半份数据。
        HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f==INVALID_HANDLE_VALUE) {if(overwrite.empty() && GetLastError()==ERROR_FILE_EXISTS) continue; return false;}
        DWORD written=0;  // written 会被 WriteFile 填成实际写入的字节数
        bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&written,NULL)!=0 && written==bytes.size();
        // 语句说明：先刷新文件数据，再关闭句柄；刷新失败同样视为保存失败。
        ok=ok && FlushFileBuffers(f);CloseHandle(f);
        // 语句说明：有overwrite时才允许替换原路径；新文件不请求覆盖已有目标。
        if(ok) ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite.empty()?0:MOVEFILE_REPLACE_EXISTING))!=0;
        if(!ok) {DeleteFileW(temp.c_str());return false;}
        r.path=path;return true;  // 成功则记下路径
    }
    return false;  // 1000 次都没能写出，返回失败
}
#endif
