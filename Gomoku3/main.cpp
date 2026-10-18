// 【建议阅读 02 / 客户端主线】main.cpp
// 先看 main 的初始化/循环/清理，再看 boardCell -> humanCanMove -> LocalGame::place。
// 随后读 Page/Dialog/Action、Button、act 和 render；遇到存档、复盘、AI、联网先按定位跳转。
// 后续阅读：match_metadata.h -> record_store.h -> replay_logic.h -> ai_engine.h。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 【阅读提示】NOMINMAX禁止Windows定义min/max宏，便于使用std::min/max。
// 【阅读提示】_WIN32_WINNT和WINVER的0x0601表示Windows 7级别的API声明要求，并不等于运行时版本检测。
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#include "network_transport.h"
#include <windows.h>
#include <graphics.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "game_logic.h"
#include "record_store.h"
#include "replay_logic.h"
#include "ai_engine.h"

const int BOARD_X = 100, BOARD_Y = 200, FIRST_X = 178, FIRST_Y = 278, SPACING = 66;  // BOARD_X/Y图片左上角；FIRST_X/Y第一个交点；SPACING相邻交点像素距，勿混用。
const wchar_t* MAIN_LABELS[] = {L"\u5f00\u59cb\u6e38\u620f", L"\u7ee7\u7eed\u6e38\u620f", L"\u590d\u76d8", L"\u9000\u51fa"}; // 开始游戏；继续游戏；复盘；退出
const wchar_t* MODE_LABELS[] = {L"\u5355\u673a\u5bf9\u5f08", L"\u4eba\u673a\u5bf9\u5f08", L"\u8054\u7f51\u5bf9\u5f08", L"\u7b97\u4e86\u4e0d\u73a9\u4e86"}; // 单机对弈；人机对弈；联网对弈；算了不玩了
// 【类型 Page】客户端页面枚举：主菜单、模式菜单、对局、残局列表、复盘列表、复盘播放、难度选择、联
// 网页面。page决定render画哪个页面；与Dialog独立。
enum Page { MAIN_MENU, MODE_MENU, GAME_PAGE, SAVE_LIST, REPLAY_LIST, REPLAY_PAGE, DIFFICULTY_PAGE, NETWORK_PAGE };
// 【类型 Dialog】本地弹窗类型：NO_DIALOG无；EXIT_SAVE退出保存；RESTART_CONFIRM重开；FINISHED_SAVE
// 终局保存；NAME_SAVE命名；AGAIN再战；DELETE_CONFIRM删除；REPLAY_EXIT退出复盘；CHOOSE_COLOR选色。
// 同一A_YES由act按dialog分派。
enum Dialog { NO_DIALOG, EXIT_SAVE, RESTART_CONFIRM, FINISHED_SAVE, NAME_SAVE, AGAIN, DELETE_CONFIRM, REPLAY_EXIT, CHOOSE_COLOR };
// 【类型 Action】本地按钮动作编号，A_START从1起。难度/颜色的连续编号用于减法换算；100起的联网动作
// 见online_ui.h:47 (NetAction)；1000起的动态编号用于列表加载/删除。
enum Action { A_START=1, A_CONTINUE, A_REPLAY, A_QUIT, A_LOCAL, A_BACK,
              A_UNDO, A_EXIT_GAME, A_RESTART, A_YES, A_NO, A_SAVE, A_CANCEL, A_AI, A_EASY, A_MEDIUM, A_HARD,
              A_BLACK, A_WHITE, A_PLAY, A_SPEED1, A_SPEED2, A_PAUSE, A_RESUME,
              A_PREVIOUS, A_NEXT, A_REPLAY_EXIT };
// 【类型 Button】一枚界面按钮的数据。id是动作编号；x/y/w/h为像素矩形；text是显示文字；enabled控制
// 能否命中；menu控制菜单外观。用addButton创建，用hitButton判断点击，再由act执行动作。
struct Button { int id, x, y, w, h; std::wstring text; bool enabled, menu; };
// 【类型 SavedEntry】列表中的一项摘要：name显示名、path磁盘路径、mode模式、valid是否能解析、result
// 结果（坏档为-1）。载入时仍重新readRecord，不把摘要当完整棋盘。
struct SavedEntry { std::wstring name,path; std::string mode; bool valid; int result; };
// 类型定义：main.cpp:32 (Page)。
Page page = MAIN_MENU;  // 当前页面，决定render绘制哪一类界面。
// 类型定义：main.cpp:36 (Dialog)。
// 语句说明：当前本地弹窗；非NO_DIALOG时通常锁定棋盘操作。
Dialog dialog = NO_DIALOG;
// 类型定义：game_logic.h:20 (LocalGame)。
LocalGame game;  // 本地/人机真实棋盘；联网模式下从收到的NetState.game复制更新。
// 类型定义：replay_logic.h:15 (ReplayPlayer)。
ReplayPlayer replayPlayer;  // 复盘播放器对象，内部有独立的展示棋盘与完整历史。
// 类型定义：record_store.h:20 (GameRecord)。
GameRecord replayRecord;  // 当前复盘的完整记录元数据，供时间、结束原因和本机颜色等展示使用。
// 类型定义：ai_engine.h:310 (AIWorker)。
AIWorker aiWorker;  // 后台AI工作器；只在主线程采用结果时才改变全局game。
std::string gameMode="LOCAL_TWO";  // 当前模式：LOCAL_TWO同机双人，HUMAN_AI人机，ONLINE联网。
int difficulty=0, humanStone=1, generation=0, deleteIndex=-1;  // difficulty=0/1/2；humanStone=1/2；generation是AI有效任务版本；deleteIndex待删列表下标。
// 语句说明：currentName当前棋局名；loadedPath续局原路径；replayName当前复盘标题。
std::wstring currentName, loadedPath, replayName;
// 【阅读提示】这里只做前置声明，函数体实际在online_ui.h:135 (onlineCanMove)。
bool onlineCanMove();
// 【函数 humanCanMove】返回当前是否允许人类在棋盘上操作。
// 执行顺序：要求 GAME_PAGE、无本地弹窗、未终局；人机模式必须轮到人；联网模式还须 onlineCanMove。
// 从上层理解：main.cpp:461 (drawGame)；main.cpp:669 (main)。
bool humanCanMove() {return page==GAME_PAGE && dialog==NO_DIALOG && !game.result &&
    (gameMode!="HUMAN_AI" || game.turn==humanStone) &&
    // 首次调用定位：online_ui.h:135 (onlineCanMove)。
    (gameMode!="ONLINE" || onlineCanMove());}  // 人机模式要求轮到玩家；联网模式交给联网规则判断
// 【函数 cancelAI】使旧 AI 任务失效并请求停止。
// 执行顺序：先 ++generation，再 aiWorker.cancel。
// 参数/局部变量：全局 generation 是任务版本，不是棋盘步数。
// 从上层理解：main.cpp:331 (act)；main.cpp:289 (loadEntry)；main.cpp:194 (mainMenu)；
// main.cpp:200 (newGame)；online_ui.h:94 (onlineEnter)；其余调用处也标有定位。
// 首次调用定位：ai_engine.h:337 (AIWorker::cancel)。
void cancelAI() {++generation;aiWorker.cancel();}
// 【函数 lastHumanMove】返回历史中玩家最近一手下标，找不到返回 -1。
// 执行顺序：从末尾倒序找 stone==humanStone。
// 参数/局部变量：i 是有符号下标，避免空 vector 的 unsigned 下溢。
// 从上层理解：main.cpp:331 (act)；main.cpp:461 (drawGame)。
int lastHumanMove() {
    for(int i=static_cast<int>(game.moves.size())-1;i>=0;--i)  // 只要有一手是自己的，就返回它的下标
        if(game.moves[i].stone==humanStone) return i;  // 倒序遇到的第一手属于玩家的棋，也就是玩家最近的一手。
    return -1;
}
// 类型定义：main.cpp:45 (Button)。
std::vector<Button> buttons;  // 当前页面的按钮，每帧重建
// 类型定义：main.cpp:48 (SavedEntry)。
std::vector<SavedEntry> entries;  // 存档/复盘列表的内容
PIMAGE frames[90] = {}, background = NULL, boardImage = NULL;  // frames为90帧菜单动画；background为背景；boardImage为棋盘图。
// 语句说明：三张EGE图片指针分别用于黑子、白子、普通按钮底板。
PIMAGE blackImage = NULL, whiteImage = NULL, buttonImage = NULL;
// 语句说明：Win32命名输入框句柄；文字输入由窗口消息机制处理。
HWND nameEdit = NULL;
// 语句说明：输入框字体句柄；退出时DeleteObject释放。
HFONT editFont = NULL;
bool running = true, savingFinished = false;  // running 主循环开关；savingFinished 记住这局是否已结束
int pressed = 0, pressedCell = -1, listOffset = 0;  // 当前按下的按钮编号、按下的交点、列表滚动位置
std::wstring notice;  // 当前短提示文本；到期时刻单独存于noticeUntil。
// 语句说明：短提示到期的毫秒时刻；0表示尚未安排提示。
ULONGLONG noticeUntil = 0;

// 【函数 toast】设置两秒短提示。
// 执行顺序：将参数 text 存入 notice；noticeUntil=当前毫秒+2000；真正绘制在 render。
// 参数/局部变量：text 是提示宽字符串，不是同名绘图函数。
// 从上层理解：main.cpp:331 (act)；main.cpp:289 (loadEntry)；main.cpp:669 (main)；
// online_ui.h:353 (onlineAction)；online_ui.h:253 (onlineSave)；其余调用处也标有定位。
void toast(const std::wstring& text) { notice = text; noticeUntil = GetTickCount64() + 2000; }  // 记录提示该消失的时刻(当前时间 + 2000 毫秒)
// 【函数 font】设置 EGE 文字字体与字号。
// 执行顺序：清零 LOGFONTW f；写负字号、抗锯齿选项和楷体；设透明文字背景。
// 参数/局部变量：size 为像素高度；f 为字体配置对象。
// 从上层理解：main.cpp:168 (drawButtons)；main.cpp:461 (drawGame)；main.cpp:128 (text)。
void font(int size) {
    LOGFONTW f; ZeroMemory(&f, sizeof(f));
    f.lfHeight = -size; f.lfQuality = ANTIALIASED_QUALITY;
    std::wcscpy(f.lfFaceName, L"\u6977\u4f53"); setfont(&f); setbkmode(TRANSPARENT); // 楷体；老式的字符串拷贝写法，把「楷体」写进字体名
}
// 【函数 text】在 x、y 绘制 value，默认字号 32。
// 执行顺序：调用 font；设深棕色；outtextxy 输出宽字符串。
// 参数/局部变量：value 为文字；size 为字号。
// 从上层理解：main.cpp:461 (drawGame)；online_ui.h:580 (drawOnlinePage)；
// online_ui.h:624 (drawOnlineSidebar)；main.cpp:595 (render)。
void text(int x, int y, const std::wstring& value, int size=32) {  // 默认用 32 号字，颜色是深棕色
    // 首次调用定位：main.cpp:118 (font)。
    font(size); setcolor(EGERGB(58,46,31)); outtextxy(x,y,value.c_str());  // outtextxy 把文字画在指定位置
}
// 【函数 addButton】把按钮信息登记进本帧 buttons，不立即执行动作。
// 执行顺序：用参数组成局部 Button b，push_back 入数组。
// 参数/局部变量：id 是动作；x/y/w/h 是矩形；label 是文字；enabled 默认 true；menu 默认 false。
// 从上层理解：main.cpp:461 (drawGame)；online_ui.h:580 (drawOnlinePage)；
// online_ui.h:624 (drawOnlineSidebar)；main.cpp:595 (render)。
void addButton(int id, int x, int y, int w, int h, const std::wstring& label,
               bool enabled=true, bool menu=false) {
    // 类型定义：main.cpp:45 (Button)。
    Button b = {id,x,y,w,h,label,enabled,menu}; buttons.push_back(b);  // 用大括号一次性构造一个 Button 并追加到列表
}
// 【函数 contains】判断像素 x、y 是否在按钮 b 的圆角形状内。
// 执行顺序：先检查外接矩形；把局部坐标夹在内部矩形上找最近点；比较距离平方与半径平方。
// 参数/局部变量：r=21 圆角半径；lx/ly 为相对按钮左上角坐标；cx/cy 为夹取后的最近点。
// 从上层理解：main.cpp:168 (drawButtons)；main.cpp:158 (hitButton)。
// 类型定义：main.cpp:45 (Button)。
bool contains(const Button& b, int x, int y) {  // 先判断是否落在矩形范围内
    if (x < b.x || y < b.y || x >= b.x+b.w || y >= b.y+b.h) return false;  // 先拒绝外接矩形之外的位置，再在后续语句排除圆角外部。
    int r=21, lx=x-b.x, ly=y-b.y;
    int cx=std::max(r,std::min(lx,b.w-r)), cy=std::max(r,std::min(ly,b.h-r));
    return (lx-cx)*(lx-cx)+(ly-cy)*(ly-cy) <= r*r;
}
// 【函数 hitButton】返回鼠标下可用按钮 id，没命中返回 0。
// 执行顺序：i 从 0 到 buttons.size()-1；首个 enabled 且 contains 的按钮立即返回。
// 参数/局部变量：x/y 是鼠标位置；i 为按钮下标。
// 边界与易错点：实际遍历从下标0向后，重叠时先登记的可用按钮优先。
// 从上层理解：main.cpp:669 (main)。
int hitButton(int x,int y) {  // 从buttons[0]向后找；首个可用命中项优先。
    for (size_t i=0;i<buttons.size();++i)
        // 首次调用定位：main.cpp:147 (contains)。
        if (buttons[i].enabled && contains(buttons[i],x,y)) return buttons[i].id;  // 只认「可点」的按钮；没点中返回 0
    return 0;
}
// 【函数 drawButtons】按 buttons 绘制按钮外观，无返回值。
// 执行顺序：非菜单按钮先贴底板；若按下且仍位于按钮内就画灰色；设字体与禁用色；文字居中。
// 参数/局部变量：mouseX/mouseY 是鼠标位置；i 是索引；b 是当前按钮的 const 引用。
// 从上层理解：main.cpp:595 (render)。
void drawButtons(int mouseX,int mouseY) {
    for (size_t i=0;i<buttons.size();++i) {
        // 类型定义：main.cpp:45 (Button)。
        const Button& b=buttons[i];
        if (!b.menu) putimage_withalpha(NULL,buttonImage,b.x,b.y,b.w,b.h,0,0,320,80,true);  // 菜单大按钮不贴素材底板，只用透明背景画文字
        // 首次调用定位：main.cpp:147 (contains)。
        if (b.enabled && pressed==b.id && contains(b,mouseX,mouseY)) {
            setfillcolor(EGERGB(220,220,220)); solidroundrect(b.x,b.y,b.x+b.w,b.y+b.h,21);
        }
        // 首次调用定位：main.cpp:118 (font)。
        font(b.menu ? 48 : 32);
        setcolor(b.enabled ? EGERGB(58,46,31) : EGERGB(125,125,115));
        outtextxy(b.x+(b.w-textwidth(b.text.c_str()))/2,  // 让文字在按钮里居中显示
                  b.y+(b.h-textheight(b.text.c_str()))/2,b.text.c_str());  // 可点的是深色，不可点的是浅灰色，一眼能分辨
    }
}
// 【函数 hideName】隐藏命名框并把焦点交回游戏窗口。
// 执行顺序：ShowWindow 隐藏 nameEdit；SetFocus 切换焦点。
// 从上层理解：main.cpp:331 (act)；main.cpp:289 (loadEntry)；main.cpp:194 (mainMenu)；
// main.cpp:200 (newGame)；online_ui.h:353 (onlineAction)；其余调用处也标有定位。
void hideName() { ShowWindow(nameEdit,SW_HIDE); SetFocus(getHWnd()); }
// 【函数 mainMenu】切回主菜单并清理本地交互状态。
// 执行顺序：取消 AI；隐藏命名框；设 MAIN_MENU、NO_DIALOG、pressed=0。
// 从上层理解：main.cpp:331 (act)；online_ui.h:84 (onlineHome)；
// main.cpp:270 (savedSuccessfully)。
// 首次调用定位：main.cpp:81 (cancelAI)；main.cpp:188 (hideName)。
void mainMenu() {cancelAI();hideName();page=MAIN_MENU;dialog=NO_DIALOG;pressed=0;}
// 【函数 newGame】开始空棋局；keepName 默认 false。
// 执行顺序：取消旧 AI；隐藏输入框；game.reset；切 GAME_PAGE；按 keepName 决定是否清除名字与
// loadedPath；人机模式弹选色。
// 参数/局部变量：keepName=true 用于“重开同一局”，不是从头读取存档。
// 从上层理解：main.cpp:331 (act)。
void newGame(bool keepName=false) {
    // 首次调用定位：main.cpp:81 (cancelAI)；main.cpp:188 (hideName)；
    // game_logic.h:39 (LocalGame::reset)。
    cancelAI();hideName();game.reset();page=GAME_PAGE;
    if(!keepName) {currentName.clear();loadedPath.clear();}  // keepName为false才清名称和原残局路径；true用于保留身份的重开。
    dialog=gameMode=="HUMAN_AI"?CHOOSE_COLOR:NO_DIALOG;  // 人机模式还要先问玩家执黑还是执白
}


// 【函数 openList】扫描残局或复盘，更新 entries 与列表页面。
// 执行顺序：按扩展名查文件；跳过目录/重解析点；readRecord 验证并取显示信息；按路径倒序排；夹紧滚动
// 位置；非刷新且为空时只提示。
// 参数/局部变量：isReplay 决定目录；refresh 是否保留滚动位置；dir 为目录；info 为查找结果；find 为
// 搜索句柄；entry 为列表项；record 为解析记录；排序 lambda 的 a/b 为两个条目。
// 从上层理解：main.cpp:331 (act)。
void openList(bool isReplay,bool refresh=false) {
    entries.clear();  // 每次都重建列表内容；是否保留滚动位置由下一行refresh决定。
    if(!refresh) listOffset=0;
    std::wstring dir=isReplay?L"data\\replays\\":L"data\\unfinished\\";
    WIN32_FIND_DATAW info;
    HANDLE find=FindFirstFileW((dir+(isReplay?L"*.greplay":L"*.gsave")).c_str(),&info);  // FindFirstFileW 按通配符找文件
    if(find!=INVALID_HANDLE_VALUE) {  // 无匹配文件或查找失败时句柄无效；当前界面统一按无结果处理。
        do {
            if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            // 类型定义：main.cpp:48 (SavedEntry)。
            SavedEntry entry;entry.path=dir+info.cFileName;  // 路径存起来，删除时要用
            // 类型定义：record_store.h:20 (GameRecord)。
            // 首次调用定位：record_store.h:155 (readRecord)。
            GameRecord record;entry.valid=readRecord(entry.path,isReplay,record);
            entry.name=entry.valid?record.name:info.cFileName;
            entry.mode=entry.valid?record.mode:"INVALID";entry.result=entry.valid?record.game.result:-1;entries.push_back(entry);
        } while(FindNextFileW(find,&info));  // 一直找到没有下一个文件为止
        FindClose(find);  // 用完记得关闭查找句柄
    }
    // 语句说明：lambda比较路径，降序排列；文件名含时间信息，但这里没有读取文件修改时间。
    std::sort(entries.begin(),entries.end(),[](const SavedEntry& a,const SavedEntry& b){return a.path>b.path;});
    listOffset=std::max(0,std::min(listOffset,std::max(0,static_cast<int>(entries.size())-8)));
    // 首次调用定位：main.cpp:113 (toast)。
    if(entries.empty() && !refresh) {toast(L"\u6682\u65e0\u5b58\u6863");return;} // 暂无存档；列表为空就提示「暂无存档」，并停在原页面
    page=isReplay?REPLAY_LIST:SAVE_LIST;dialog=NO_DIALOG;
}
// 【函数 saveGame】保存本地/人机棋局，成功返回 true。
// 执行顺序：去掉名称两端空白并检查长度；构造记录；残局可覆盖 loadedPath，完成局创建复盘；成功后更
// 新 currentName；完成续局时尝试删除旧残局。
// 参数/局部变量：name 按值传入，可在函数内修剪；first/last 是首尾有效字符下标；record 为待写记录。
// 边界与易错点：联网保存走 onlineSave。删除原残局失败时会提示，但复盘本身已写成功。
// 从上层理解：main.cpp:331 (act)；main.cpp:278 (beginName)。
bool saveGame(std::wstring name) {
    size_t first=name.find_first_not_of(L" \t\r\n"),last=name.find_last_not_of(L" \t\r\n");
    // 首次调用定位：main.cpp:113 (toast)。
    if(first==std::wstring::npos) {toast(L"\u8bf7\u8f93\u5165\u68cb\u5c40\u540d\u79f0");return false;} // 请输入棋局名称
    name=name.substr(first,last-first+1);  // 去掉了首尾空白后的名字
    if(name.size()>24 || name.find_first_of(L"\r\n\t")!=std::wstring::npos) {toast(L"\u540d\u79f0\u965024\u5b57\uff0c\u4e0d\u80fd\u5305\u542b\u6362\u884c");return false;} // 名称限24字，不能包含换行
    // 类型定义：record_store.h:20 (GameRecord)。
    GameRecord record;record.game=game;record.mode=gameMode;record.difficulty=difficulty;
    record.human=humanStone;record.name=name;record.replay=game.result!=0;  // 已结束的棋局存成复盘，未结束的存成残局
    // 首次调用定位：record_store.h:200 (writeRecord)。
    if(!writeRecord(record,record.replay?L"":loadedPath)) {toast(L"\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u76ee\u5f55\u540e\u91cd\u8bd5");return false;} // 保存失败，请检查目录后重试
    currentName=name;
    if(record.replay && !loadedPath.empty()) {
        if(!DeleteFileW(loadedPath.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND)
            toast(L"\u590d\u76d8\u5df2\u4fdd\u5b58\uff0c\u539f\u6b8b\u5c40\u8bf7\u624b\u52a8\u5220\u9664"); // 复盘已保存，原残局请手动删除
        else toast(L"\u590d\u76d8\u5df2\u4fdd\u5b58"); // 复盘已保存；复盘已保存，原残局可以删掉了
        loadedPath.clear();
    } else {if(!record.replay) loadedPath=record.path;toast(L"\u68cb\u5c40\u5df2\u4fdd\u5b58");} // 棋局已保存；残局存好了，记下路径，方便以后续局时覆盖
    return true;
}
// 【函数 savedSuccessfully】保存完成后的页面收尾。
// 执行顺序：隐藏命名框；已结束则弹 AGAIN，未结束则回主菜单。
// 从上层理解：main.cpp:331 (act)；main.cpp:278 (beginName)。
void savedSuccessfully() {
    // 首次调用定位：main.cpp:188 (hideName)；main.cpp:194 (mainMenu)。
    hideName();if(game.result) dialog=AGAIN;else mainMenu();  // 棋局结束就接着问「再来一局」，否则回主菜单
}
// 【函数 beginName】准备保存：沿用旧名字或显示命名框。
// 执行顺序：记录 savingFinished；有 currentName 就直接保存，成功后收尾；否则设置 NAME_SAVE、清空并
// 聚焦编辑框。
// 从上层理解：main.cpp:331 (act)。
void beginName() {
    savingFinished=game.result!=0;
    // 首次调用定位：main.cpp:247 (saveGame)；main.cpp:270 (savedSuccessfully)。
    if(!currentName.empty()) {if(saveGame(currentName)) savedSuccessfully();return;}
    dialog=NAME_SAVE;SetWindowTextW(nameEdit,L"");ShowWindow(nameEdit,SW_SHOW);SetFocus(nameEdit);
}
// 【函数 loadEntry】加载列表中 index 对应文件。
// 执行顺序：先检查下标并重新读档；拒绝联网残局继续；取消 AI；复盘模式打开 replayPlayer，续局模式恢
// 复 game 及模式、难度、颜色、名字、路径。
// 参数/局部变量：record 为临时记录；isReplay 由 page 是否为 REPLAY_LIST 决定。
// 从上层理解：main.cpp:331 (act)。
void loadEntry(int index) {
    if(index<0 || index>=static_cast<int>(entries.size())) return;
    // 类型定义：record_store.h:20 (GameRecord)。
    GameRecord record;bool isReplay=page==REPLAY_LIST;
    // 首次调用定位：record_store.h:155 (readRecord)；main.cpp:113 (toast)。
    if(!readRecord(entries[index].path,isReplay,record)) {toast(L"\u5b58\u6863\u635f\u574f\u6216\u65e0\u6cd5\u8bfb\u53d6");return;} // 存档损坏或无法读取
    if(!isReplay && record.mode=="ONLINE") {toast(L"\u8054\u7f51\u68cb\u5c40\u4e0d\u80fd\u79bb\u7ebf\u7eed\u5c40");return;} // 联网棋局不能离线续局
    // 首次调用定位：main.cpp:81 (cancelAI)；main.cpp:188 (hideName)。
    cancelAI();hideName();dialog=NO_DIALOG;
    // 首次调用定位：replay_logic.h:35 (ReplayPlayer::open)。
    if(isReplay) {replayRecord=record;replayName=record.name;replayPlayer.open(record.game);page=REPLAY_PAGE;}
    else {  // 恢复出这一局的所有信息
        game=record.game;gameMode=record.mode;difficulty=record.difficulty;humanStone=record.human;
        currentName=record.name;loadedPath=record.path;page=GAME_PAGE;  // 记下名字和路径，后续保存时沿用
    }
}
// 【阅读提示】此处才包含online_ui.h：它用到前面定义的game、page、toast、hideName等名字。
// 【阅读提示】这不是另一个main；仍属于客户端这一个编译单元。联网入口见
// online_ui.h:94 (onlineEnter)。
#include "online_ui.h"

// 【阅读提示】WM_CLOSE回调只写请求，主循环再按页面决定立即退出还是先处理联网保存/离开。
volatile LONG windowCloseRequested=0;
WNDPROC originalWindowProc=NULL;
// 【函数 gameWindowProc】Win32 窗口回调，返回 LRESULT。
// 执行顺序：WM_CLOSE 只把请求标志原子置 1，稍后由主循环处理；其他消息交给原回调。
// 参数/局部变量：hwnd 为窗口；msg 为消息种类；wp/lp 为消息附加参数。
// 边界与易错点：本地页面点击窗口 X 会直接退出；想保存请走界面“退出”。联网对局的 X 则走退出/保存流
// 程。
// 从上层理解：main.cpp:669 (main)。
LRESULT CALLBACK gameWindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE) {InterlockedExchange(&windowCloseRequested,1);return 0;}  // 收到关闭消息时只记个标志，先不真的退出
    return CallWindowProcW(originalWindowProc,hwnd,msg,wp,lp);  // 其它消息交回给原来的处理函数，保证窗口功能正常
}
// 【函数 act】按钮动作的总分发器，输入 id。
// 执行顺序：联网编号交给 onlineAction；>=1000 按偶奇区分载入和删除；其余 switch 处理菜单、对局、保
// 存和复盘。
// 参数/局部变量：index=(id-1000)/2 是列表下标；at 为最近人类落子下标；isReplay 为删除后刷新哪类列
// 表；name[128] 为输入框读回缓冲。
// 边界与易错点：重点跟读 A_UNDO、A_YES、A_NO：同一个“是/否”按钮，由 dialog 决定具体操作。悔棋先取
// 消 AI，再撤掉玩家最近一手及其后的电脑应答。
// 从上层理解：main.cpp:669 (main)。
void act(int id) {
    // 首次调用定位：online_ui.h:353 (onlineAction)。
    if(id>=A_ONLINE && id<=A_NET_COPY) {onlineAction(id);return;}
    if(id>=1000) {
        if(dialog!=NO_DIALOG) return;  // 有询问框时不允许操作列表
        int index=(id-1000)/2;
        if(index<0 || index>=static_cast<int>(entries.size())) return;
        if((id-1000)%2) {deleteIndex=index;dialog=DELETE_CONFIRM;}  // 偶数编号是载入，奇数编号是删除
        // 首次调用定位：main.cpp:289 (loadEntry)。
        else loadEntry(index);
        return;
    }
    switch(id) {
    case A_START:page=MODE_MENU;break;  // 开始游戏 → 进入模式菜单
    // 首次调用定位：main.cpp:215 (openList)。
    case A_CONTINUE:openList(false);break;
    case A_REPLAY:openList(true);break;
    case A_QUIT:running=false;break;
    // 首次调用定位：main.cpp:200 (newGame)。
    case A_LOCAL:gameMode="LOCAL_TWO";newGame();break;  // 单机双人
    case A_AI:page=DIFFICULTY_PAGE;break;
    case A_EASY:case A_MEDIUM:case A_HARD:difficulty=id-A_EASY;gameMode="HUMAN_AI";newGame();break;  // 选了难度后进入人机模式
    case A_BLACK:case A_WHITE:humanStone=id==A_BLACK?1:2;dialog=NO_DIALOG;break;  // 记下玩家选的颜色并关闭询问
    // 首次调用定位：main.cpp:194 (mainMenu)。
    case A_BACK:mainMenu();break;
    // 【阅读提示】人机悔棋以最近一次人类落子为界，一并撤掉后续电脑应答；不是固定撤两手。
    case A_UNDO:
        // 首次调用定位：main.cpp:81 (cancelAI)。
        cancelAI();
        if(gameMode=="HUMAN_AI") {
            // 首次调用定位：main.cpp:86 (lastHumanMove)。
            int at=lastHumanMove();
            // 首次调用定位：game_logic.h:97 (LocalGame::undo)。
            if(at>=0) while(static_cast<int>(game.moves.size())>at) game.undo();
        } else game.undo();  // 单机模式直接撤最后一手
        break;
    case A_EXIT_GAME:cancelAI();dialog=game.result?AGAIN:EXIT_SAVE;break;  // 棋局已结束就直接问「再来一局」，否则问是否保存
    case A_RESTART:if(!game.result) {cancelAI();dialog=RESTART_CONFIRM;}break;  // 重开前先确认，而且只有棋局没结束时才需要
    // 【阅读提示】同一个A_YES按钮按dialog解释；先核对正在询问什么，再看该分支会保存、重开还是删除。
    case A_YES:
        if(dialog==DELETE_CONFIRM) {
            if(deleteIndex>=0 && deleteIndex<static_cast<int>(entries.size()) &&  // 删除成功后刷新列表并提示
               DeleteFileW(entries[deleteIndex].path.c_str())) {
                // 首次调用定位：main.cpp:113 (toast)。
                bool isReplay=page==REPLAY_LIST;openList(isReplay,true);toast(L"\u5df2\u6c38\u4e45\u5220\u9664"); // 已永久删除
            } else toast(L"\u5220\u9664\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u6587\u4ef6\u6743\u9650"); // 删除失败会提示检查权限；失败原因也可能是文件占用、路径变化等。
        // 首次调用定位：replay_logic.h:56 (ReplayPlayer::pause)。
        } else if(dialog==REPLAY_EXIT) {replayPlayer.pause(GetTickCount64());mainMenu();}
        // 首次调用定位：main.cpp:278 (beginName)。
        else if(dialog==EXIT_SAVE || dialog==FINISHED_SAVE) beginName();  // 要保存就进入命名流程
        else if(dialog==RESTART_CONFIRM) newGame(true);  // 重开：保留名字
        else if(dialog==AGAIN) newGame();  // 再来一局：用全新的棋局
        break;
    case A_NO:
        if(dialog==DELETE_CONFIRM || dialog==REPLAY_EXIT) dialog=NO_DIALOG;  // 删除询问和退出复盘询问都是直接关闭弹窗
        else if(dialog==EXIT_SAVE || dialog==AGAIN) mainMenu();  // 退出对局或不再来一局，都回主菜单
        else if(dialog==RESTART_CONFIRM) dialog=NO_DIALOG;
        else if(dialog==FINISHED_SAVE) dialog=AGAIN;  // 本局已结束时取消保存，回到「再来一局」的询问
        break;
    case A_SAVE: {
        wchar_t name[128];GetWindowTextW(nameEdit,name,128);  // 缓冲容量128个wchar_t（含末尾0）；控件此前已设置输入长度24。
        // 首次调用定位：main.cpp:247 (saveGame)；main.cpp:270 (savedSuccessfully)。
        if(saveGame(name)) savedSuccessfully();break;
    }
    // 首次调用定位：main.cpp:188 (hideName)。
    case A_CANCEL:hideName();dialog=savingFinished?FINISHED_SAVE:EXIT_SAVE;break;
    // 首次调用定位：replay_logic.h:64 (ReplayPlayer::play)。
    case A_PLAY:case A_RESUME:replayPlayer.play(GetTickCount64());break;  // PLAY与RESUME共用play；PAUSE在下一分支单独处理。
    case A_PAUSE:replayPlayer.pause(GetTickCount64());break;
    // 首次调用定位：replay_logic.h:74 (ReplayPlayer::speed)。
    case A_SPEED1:replayPlayer.speed(3000,GetTickCount64());break;  // 1x 速度：每手停 3 秒
    case A_SPEED2:replayPlayer.speed(1500,GetTickCount64());break;  // 2x 速度：每手停 1.5 秒
    // 首次调用定位：replay_logic.h:45 (ReplayPlayer::seek)。
    case A_PREVIOUS:if(replayPlayer.cursor) replayPlayer.seek(replayPlayer.cursor-1);break;  // 单步回看
    case A_NEXT:if(replayPlayer.cursor<replayPlayer.moves.size()) replayPlayer.seek(replayPlayer.cursor+1);break;
    case A_REPLAY_EXIT:replayPlayer.pause(GetTickCount64());dialog=REPLAY_EXIT;break;  // 退出复盘前先暂停，再弹询问
    }
}
// 【函数 updateAI】每帧收答案并按需启动新的 AI。
// 执行顺序：先 take；仅 gen==generation 且仍在无弹窗的人机未完局、轮到电脑时 place；再检查若轮到电
// 脑且无线程则 start。
// 参数/局部变量：point 为取得的落点；gen 为该答案携带的版本。
// 边界与易错点：不是只检查“电脑线程算完了”。重开、读档、悔棋后 generation 已变，旧结果即使合法也应
// 丢弃。
// 从上层理解：main.cpp:669 (main)。
void updateAI() {
    // 类型定义：ai_engine.h:20 (AIPoint)。
    AIPoint point;int gen=0;  // point和gen先准备为输出变量；真正领取发生在下一条件中的take。
    // 首次调用定位：ai_engine.h:356 (AIWorker::take)。
    if(aiWorker.take(point,gen) && gen==generation && page==GAME_PAGE && dialog==NO_DIALOG &&
       gameMode=="HUMAN_AI" && !game.result && game.turn!=humanStone) {
        // 首次调用定位：game_logic.h:79 (LocalGame::place)；main.cpp:81 (cancelAI)。
        if(game.place(point.row,point.col) && game.result) {cancelAI();dialog=FINISHED_SAVE;}  // 落子成功且分出胜负，就询问是否保存
    }
    if(page==GAME_PAGE && dialog==NO_DIALOG && gameMode=="HUMAN_AI" && !game.result &&
       game.turn!=humanStone && !aiWorker.thread) {  // thread句柄为空才允许启动；已结束但未take的旧句柄也需先回收。
        // 首次调用定位：ai_engine.h:343 (AIWorker::start)。
        if(!aiWorker.start(game,difficulty,generation)) {
            // 首次调用定位：main.cpp:113 (toast)。
            toast(L"AI\u7ebf\u7a0b\u542f\u52a8\u5931\u8d25\uff0c\u53ef\u91cd\u5f00\u6216\u9000\u51fa"); // AI线程启动失败，可重开或退出；启动失败也可能是系统资源不足
            dialog=RESTART_CONFIRM;
        }
    }
}

// 【函数 boardCell】把像素 x、y 转成 0..224 格子编号，失败返回 -1。
// 执行顺序：减去首交点；超出外缘 30 像素即拒绝；除以 66 得最近行列；重新算与交点偏移；圆形距离超过
//  30 像素拒绝；返回 row*15+col。
// 参数/局部变量：dx/dy 先是到棋盘原点偏移，后是到最近交点偏移；col/row 为格子坐标。
// 边界与易错点：例如 (640,740) 对应 (7,7)，编号 112；用 cell/15 还原行，用 cell%15 还原列。不能只
// 按棋盘图片矩形判断落点。
// 从上层理解：main.cpp:461 (drawGame)；main.cpp:669 (main)。
int boardCell(int x,int y) {
    int dx=x-FIRST_X, dy=y-FIRST_Y;
    if(dx < -30 || dy < -30 || dx > 14*SPACING+30 || dy > 14*SPACING+30) return -1;  // 棋盘外太远的点击直接丢弃
    int col=(dx+SPACING/2)/SPACING, row=(dy+SPACING/2)/SPACING;  // 四舍五入吸附到最近的交点
    dx-=col*SPACING; dy-=row*SPACING;
    if(row<0 || row>=15 || col<0 || col>=15 || dx*dx+dy*dy>900) return -1;  // 换算偏移后判断是否真的足够近
    return row*15+col;  // 把二维坐标压成一维编号返回
}

// 【函数 drawGame】画对局或复盘；主要读取状态，登记对应按钮。
// 执行顺序：选择 shown 棋盘；铺背景与棋子；联网显示悬停预览和末手圈；终局画横幅与胜线；随后按复盘/
// 联网/本地分支画侧栏。
// 参数/局部变量：replay 是是否复盘；shown 是当前展示盘引用；title/status/banner 为标题文字；m 为正
// 在画的一手；mx/my/cell 为悬停目标；last 为末手；endReason 为结束原因；dirs/d/sign/r/c/line 为胜
// 线收集；step 为步数文字；free 为按钮启用条件；level 为难度文字；i 为遍历索引。
// 边界与易错点：认输有胜负但没有连五，所以 END_RESIGN 跳过金圈胜线。绘制按 moves 遍历，不是逐格扫
// cells；两份数据须保持一致。
// 从上层理解：main.cpp:595 (render)。
void drawGame() {
    bool replay=page==REPLAY_PAGE;
    // 类型定义：game_logic.h:20 (LocalGame)。
    const LocalGame& shown=replay?replayPlayer.board:game;
    putimage(0,0,background);  // 先铺背景，再叠棋盘
    putimage_withalpha(NULL,boardImage,BOARD_X,BOARD_Y,1080,1080,0,0,720,720,true);
    std::wstring title=(!replay && gameMode=="ONLINE")?L"\u8054\u7f51\u5bf9\u5f08":replay?L"\u68cb\u5c40\u590d\u76d8":gameMode=="HUMAN_AI"?L"\u4eba\u673a\u5bf9\u5f08":L"\u5355\u673a\u5bf9\u5f08"; // 棋局复盘；人机对弈；单机对弈；标题：联网、复盘、人机、单机各有说法
    // 首次调用定位：main.cpp:128 (text)。
    if(!shown.result) text(150,95,title+L" \u00b7 \u9ed1\u5148\u767d\u540e",48); // · 黑先白后；棋局结束时顶部不再显示标题和先手说明
    for(size_t i=0;i<shown.moves.size();++i) {
        // 类型定义：game_logic.h:16 (Move)。
        const Move& m=shown.moves[i];  // 逐手画出棋子
        putimage_withalpha(NULL,m.stone==1?blackImage:whiteImage,FIRST_X+m.col*SPACING-33,
            FIRST_Y+m.row*SPACING-33,66,66,0,0,44,44,true);  // 目标棋子图像66×66像素；读取源图的44×44区域，再居中放到交点。
    }
    // 【阅读提示】联网悬停预览只是半透明图像，不会写入cells/moves；正式棋子要等权威STATE。
    // 首次调用定位：main.cpp:71 (humanCanMove)。
    if(!replay && gameMode=="ONLINE" && humanCanMove()) {
        // 首次调用定位：main.cpp:443 (boardCell)。
        int mx=-1,my=-1;mousepos(&mx,&my);int cell=boardCell(mx,my);
        if(cell>=0 && !shown.cells[cell/15][cell%15])  // 只有空位才显示预览
            putimage_alphablend(NULL,game.turn==1?blackImage:whiteImage,FIRST_X+(cell%15)*SPACING-33,
                FIRST_Y+(cell/15)*SPACING-33,66,66,110,0,0,44,44,true,COLORTYPE_ARGB32);  // 110 是半透明程度
    }
    if(((!replay && gameMode=="ONLINE") || (replay && replayRecord.mode=="ONLINE")) && !shown.moves.empty()) {
        const Move& last=shown.moves.back();setcolor(EGERGB(213,74,35));setlinewidth(3);  // 最后一手用小红圈标出来
        circle(FIRST_X+last.col*SPACING,FIRST_Y+last.row*SPACING,7);setlinewidth(1);
    }
    int endReason=replay?replayRecord.reason:gameMode=="ONLINE"?netState.reason:END_UNKNOWN;  // reason 来自复盘记录或联网状态
    if(shown.result) {
        setfillcolor(EGERGB(128,48,32));solidroundrect(100,65,1180,185,25);  // 深红色横幅
        // 首次调用定位：main.cpp:118 (font)。
        font(68);setcolor(EGERGB(255,231,151));  // 金色大字
        const wchar_t* banner=shown.result==1?L"\u9ed1 \u65b9 \u83b7 \u80dc\uff01":shown.result==2?L"\u767d \u65b9 \u83b7 \u80dc\uff01":L"\u672c \u5c40 \u5e73 \u5c40"; // 黑 方 获 胜！；白 方 获 胜！；本 局 平 局
        if(endReason==END_RESIGN) banner=shown.result==1?L"\u9ed1\u65b9\u83b7\u80dc\uff08\u5bf9\u65b9\u8ba4\u8f93\uff09":L"\u767d\u65b9\u83b7\u80dc\uff08\u5bf9\u65b9\u8ba4\u8f93\uff09";
        outtextxy(640-textwidth(banner)/2,88,banner);  // 文字水平居中
        // 【阅读提示】认输有胜者但未必五连，禁止把它画成连五；平局也没有获胜连线。
        if(endReason!=END_RESIGN && shown.result!=3 && !shown.moves.empty()) {
            Move last=shown.moves.back();int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};  // 方向增量按行/列解释：竖直、水平、两条斜线；line用于收集胜线上的棋子。
            setcolor(EGERGB(229,152,28));setlinewidth(4);  // 金色，并加粗线宽
            for(int d=0;d<4;++d) {
                std::vector<Move> line;line.push_back(last);
                for(int sign=-1;sign<=1;sign+=2) {  // 往两头各延伸一次
                    int r=last.row+sign*dirs[d][0],c=last.col+sign*dirs[d][1];  // 从相邻一格开始
                    while(r>=0 && r<15 && c>=0 && c<15 && shown.cells[r][c]==last.stone) {  // 同色就继续往前
                        Move m={r,c,last.stone};line.push_back(m);r+=sign*dirs[d][0];c+=sign*dirs[d][1];  // 把这一颗收进连线里
                    }
                }
                if(line.size()>=5) for(size_t i=0;i<line.size();++i)  // 半径 35 的金圈套住每一颗棋子
                    circle(FIRST_X+line[i].col*SPACING,FIRST_Y+line[i].row*SPACING,35);
            }
            setlinewidth(1);  // 线宽还原
        }
    }
    setfillcolor(EGERGB(246,241,224));solidroundrect(1220,120,1760,1250,24);
    if(replay) {
        text(1270,155,L"\u68cb\u5c40\u590d\u76d8",44); // 棋局复盘；面板标题
        text(1250,220,replayName.size()>14?replayName.substr(0,14)+L"\u2026":replayName,30); // …；棋局名太长时截断并加省略号
        wchar_t step[80];std::swprintf(step,80,L"%u / %u",static_cast<unsigned>(replayPlayer.cursor),static_cast<unsigned>(replayPlayer.moves.size()));  // 当前步数与总步数
        text(1370,275,step,36);  // 这里只显示播放进度；下面依据started决定是否出现速度按钮。
        bool free=dialog==NO_DIALOG;
        // 首次调用定位：main.cpp:137 (addButton)。
        if(!replayPlayer.started) addButton(A_PLAY,1330,340,320,80,L"\u81ea\u52a8\u64ad\u653e",free && !replayPlayer.moves.empty()); // 自动播放
        else {  // 曾经开始过播放就显示速度按钮；正在播放与暂停状态都可看到。
            addButton(A_SPEED1,1260,340,220,75,replayPlayer.interval==3000?L"1x\uff08\u5f53\u524d\uff09":L"1x",free); // 1x（当前）；当前速度的按钮会加「（当前）」字样
            addButton(A_SPEED2,1500,340,220,75,replayPlayer.interval==1500?L"2x\uff08\u5f53\u524d\uff09":L"2x",free); // 2x（当前）
            addButton(replayPlayer.playing?A_PAUSE:A_RESUME,1330,440,320,80,  // 暂停或继续播放
                replayPlayer.playing?L"\u6682\u505c":L"\u7ee7\u7eed",free && replayPlayer.cursor<replayPlayer.moves.size()); // 暂停；继续；播完了就不能再点继续
        }
        if(!replayPlayer.playing) {
            addButton(A_PREVIOUS,1260,560,220,75,L"\u4e0a\u4e00\u6b65",free && replayPlayer.cursor>0); // 上一步；上一步 / 下一步
            addButton(A_NEXT,1500,560,220,75,L"\u4e0b\u4e00\u6b65",free && replayPlayer.cursor<replayPlayer.moves.size()); // 下一步；到头了按钮会自动变灰
        }
        if(replayPlayer.cursor==replayPlayer.moves.size()) text(1290,665,L"\u64ad\u653e\u7ed3\u675f\uff0c\u53ef\u624b\u52a8\u56de\u770b",28); // 播放结束，可手动回看
        addButton(A_REPLAY_EXIT,1330,740,320,80,L"\u9000\u51fa",free); // 退出；退出复盘
        if(dialog==NO_DIALOG) {
            // 首次调用定位：match_metadata.h:50 (matchTime)。
            text(1250,860,matchTime(replayRecord.started),26);  // 显示这一局的开始时间
            if(replayRecord.mode=="ONLINE") text(1250,905,replayRecord.version<2?L"\u672c\u673a\u6267\u68cb\uff1a\u65e7\u8bb0\u5f55\u672a\u8bb0\u5f55":
                replayRecord.human==1?L"\u672c\u673a\u5f53\u65f6\u6267\u9ed1 \u00b7 \u623f\u4e3b":L"\u672c\u673a\u5f53\u65f6\u6267\u767d \u00b7 \u670b\u53cb",28);
            // 首次调用定位：match_metadata.h:34 (reasonText)。
            text(1250,950,reasonText(replayRecord.reason),28);  // 显示结束原因
            if(!replayRecord.game.result) text(1250,995,L"\u672a\u5b8c\u590d\u76d8\uff0c\u4e0d\u53ef\u7ee7\u7eed\u5bf9\u5c40",28);
        }
        if(dialog==REPLAY_EXIT) {
            text(1260,885,L"\u5df2\u6682\u505c\uff0c\u662f\u5426\u9000\u51fa\u590d\u76d8\uff1f",32); // 已暂停，是否退出复盘？；询问是否退出复盘
            addButton(A_YES,1260,1000,220,75,L"\u662f");addButton(A_NO,1500,1000,220,75,L"\u5426"); // 是；否；是 / 否
        }
        return;  // 复盘页面到此结束
    }
    // 首次调用定位：online_ui.h:624 (drawOnlineSidebar)。
    if(gameMode=="ONLINE") {drawOnlineSidebar();return;}  // 联网页面的侧边栏另有专门的绘制函数
    const wchar_t* status=game.result==1?L"\u9ed1\u65b9\u83b7\u80dc":game.result==2?L"\u767d\u65b9\u83b7\u80dc": // 黑方获胜；白方获胜
        game.result==3?L"\u672c\u5c40\u5e73\u5c40":gameMode=="HUMAN_AI" && game.turn!=humanStone && dialog!=CHOOSE_COLOR? // 本局平局；人机模式下轮到自己时，才显示「轮到谁」，否则显示「电脑思考中」
        L"\u7535\u8111\u601d\u8003\u4e2d\u2026":game.turn==1?L"\u8f6e\u5230\u9ed1\u65b9\u843d\u5b50":L"\u8f6e\u5230\u767d\u65b9\u843d\u5b50"; // 电脑思考中…；轮到黑方落子；轮到白方落子；轮到黑方或白方
    text(1280,150,status,40);  // 把状态文字画在右侧面板上
    if(gameMode=="HUMAN_AI") {
        std::wstring level=difficulty==0?L"\u7b80\u5355":difficulty==1?L"\u4e00\u822c":L"\u56f0\u96be"; // 简单；一般；困难；三个难度名
        text(1260,202,level+(dialog==CHOOSE_COLOR?L" \u00b7 \u7b49\u5f85\u9009\u8272":humanStone==1?L" \u00b7 \u4f60\u6267\u9ed1":L" \u00b7 \u4f60\u6267\u767d"),25); // · 等待选色； · 你执黑； · 你执白
    }
    bool free=dialog==NO_DIALOG && !game.result;
    addButton(A_UNDO,1290,250,400,80,gameMode=="HUMAN_AI"?L"\u6094\u68cb\uff08\u64a4\u56de\u4e00\u56de\u5408\uff09":L"\u6094\u68cb", // 悔棋（撤回一回合）；悔棋；人机模式要撤回一回合，所以提示文字不同
        // 首次调用定位：main.cpp:86 (lastHumanMove)。
        free && (gameMode=="HUMAN_AI"?lastHumanMove()>=0:!game.moves.empty()));  // 人机模式下必须玩家真的下过子才允许悔棋
    addButton(A_RESTART,1330,350,320,80,L"\u91cd\u5f00",free); // 重开；重开
    addButton(A_EXIT_GAME,1330,450,320,80,L"\u9000\u51fa",dialog==NO_DIALOG); // 退出；退出对局
    if(dialog==CHOOSE_COLOR) {
        buttons.clear(); // 选色询问只显示黑、白两个可操作按钮。
        setfillcolor(EGERGB(247,237,202));solidroundrect(380,440,1420,860,30);  // 绘制实心米色圆角面板；本调用没有透明混合参数。
        text(590,490,L"\u8bf7\u9009\u62e9\u4f60\u6267\u9ed1\u8fd8\u662f\u6267\u767d",52); // 请选择你执黑还是执白；询问文字
        text(670,580,L"\u9ed1\u68cb\u5148\u4e0b\uff0c\u767d\u68cb\u540e\u4e0b",40); // 黑棋先下，白棋后下；补充说明谁先下
        addButton(A_BLACK,510,700,320,90,L"\u6211\u6267\u9ed1");addButton(A_WHITE,970,700,320,90,L"\u6211\u6267\u767d"); // 我执黑；我执白；我执黑 / 我执白
    } else if(dialog!=NO_DIALOG) {
        text(1270,580,game.result?L"\u672c\u5c40\u5df2\u7ed3\u675f":L"\u68cb\u5c40\u63d0\u793a",40); // 本局已结束；棋局提示
        if(dialog==EXIT_SAVE) text(1260,650,L"\u662f\u5426\u4fdd\u5b58\u5f53\u524d\u8fdb\u5ea6\uff1f"); // 是否保存当前进度？
        if(dialog==FINISHED_SAVE) text(1260,650,L"\u662f\u5426\u4fdd\u5b58\u672c\u5c40\u590d\u76d8\uff1f"); // 是否保存本局复盘？
        if(dialog==AGAIN) text(1260,650,L"\u8981\u4e0d\u8981\u518d\u6765\u4e00\u5c40\uff1f"); // 要不要再来一局？
        if(dialog==RESTART_CONFIRM) {text(1260,650,L"\u8fdb\u5ea6\u4e0d\u4f1a\u88ab\u4fdd\u5b58\u3002");text(1260,695,L"\u662f\u5426\u91cd\u5f00\uff1f");} // 进度不会被保存。；是否重开？
        if(dialog==NAME_SAVE) {  // 命名框由原生控件实现，这里只画提示文字
            text(1260,650,L"\u8bf7\u4e3a\u672c\u68cb\u5c40\u547d\u540d\uff1a");text(1260,800,L"\u6700\u591a24\u5b57\uff0c\u652f\u6301\u4e2d\u6587",28); // 请为本棋局命名：；最多24字，支持中文；说明字数限制
            addButton(A_SAVE,1260,900,220,75,L"\u4fdd\u5b58");addButton(A_CANCEL,1500,900,220,75,L"\u53d6\u6d88"); // 保存；取消；保存 / 取消
        } else {
            addButton(A_YES,1260,900,220,75,L"\u662f");addButton(A_NO,1500,900,220,75,L"\u5426"); // 是；否；是 / 否
        }
    }
}
// 【函数 render】按 page 绘制整页，并重建可点击按钮。
// 执行顺序：先清 buttons；菜单按时间选动画帧；网络/棋局交给专门函数；列表只画最多 8 项；最后获取鼠
// 标位置画按钮和未到期 toast。
// 参数/局部变量：startedAt 为动画时间原点；frame=((当前-原点)%3000)*30/1000；ys/ids/levels 是菜单
// 位置、动作、难度标签；id/i 为按钮编号与循环；at/entry/label 为当前列表项；name 为待删名；x/y 为
// 鼠标位置；left/top 为提示框位置。
// 边界与易错点：动画共 90 帧、每轮 3 秒；图形主循环目标 60 FPS，不代表素材动画也有 60 个不同帧/秒。
// 从上层理解：main.cpp:669 (main)。
void render(ULONGLONG startedAt) {
    buttons.clear();
    if(page==MAIN_MENU || page==MODE_MENU || page==DIFFICULTY_PAGE) {
        int frame=static_cast<int>(((GetTickCount64()-startedAt)%3000)*30/1000);  // 当前是第几帧(0 到 89)
        putimage(0,0,frames[frame]);  // 画出这一帧动画
        int ys[]={476,591,705,821};  // 四个按钮的纵坐标
        int ids[]={A_START,A_CONTINUE,A_REPLAY,A_QUIT};  // 主菜单四个按钮的编号
        for(int i=0;i<4;++i) {  // 依次添加四个按钮
            int id=page==MAIN_MENU?ids[i]:(i==0?A_LOCAL:i==1?A_AI:i==3?static_cast<int>(A_BACK):static_cast<int>(A_ONLINE));
            const wchar_t* levels[]={L"\u7b80\u5355",L"\u4e00\u822c",L"\u56f0\u96be",L"\u8fd4\u56de\u4e3b\u754c\u9762"}; // 简单；一般；困难；返回主界面；难度菜单的四个按钮文字
            if(page==DIFFICULTY_PAGE) id=i<3?A_EASY+i:A_BACK;  // 难度菜单前三个是难度，第四个是返回
            // 首次调用定位：main.cpp:137 (addButton)。
            addButton(id,687,ys[i],441,95,page==DIFFICULTY_PAGE?levels[i]:page==MAIN_MENU?MAIN_LABELS[i]:MODE_LABELS[i],id!=0,true);  // 文字随页面变化：难度菜单 / 主菜单 / 模式菜单
        }
    // 首次调用定位：online_ui.h:580 (drawOnlinePage)。
    } else if(page==NETWORK_PAGE) drawOnlinePage();
    // 首次调用定位：main.cpp:461 (drawGame)。
    else if(page==GAME_PAGE || page==REPLAY_PAGE) drawGame();
    else {
        putimage(0,0,background);  // 列表页也要铺背景
        setfillcolor(EGERGB(246,241,224)); solidroundrect(380,100,1420,1250,28);  // 一个大的米色面板
        // 首次调用定位：main.cpp:128 (text)。
        text(560,150,page==SAVE_LIST?L"\u7ee7\u7eed\u6e38\u620f \u00b7 \u672a\u5b8c\u6210\u68cb\u5c40":L"\u590d\u76d8 \u00b7 \u68cb\u5c40\u8bb0\u5f55",44); // 继续游戏 · 未完成棋局；复盘 · 已完成棋局；标题区分「继续游戏」和「复盘」
        text(510,220,L"\u68cb\u5c40\u540d\u79f0",28);text(1080,220,L"\u6a21\u5f0f",28);text(1250,220,L"\u64cd\u4f5c",28); // 棋局名称；模式；操作；三个列表表头
        for(int i=0;i<8 && listOffset+i<static_cast<int>(entries.size());++i) {
            // 类型定义：main.cpp:48 (SavedEntry)。
            int at=listOffset+i;const SavedEntry& entry=entries[at];  // at是entries内真实下标，i才是当前页自上而下第几行。
            std::wstring label=entry.name.size()>18?entry.name.substr(0,18)+L"\u2026":entry.name; // 名称超过18个宽字符代码单元时截断并加省略号。
            addButton(1000+at*2,410,290+i*95,640,80,label,dialog==NO_DIALOG);
            text(1080,316+i*95,entry.mode=="LOCAL_TWO"?L"\u5355\u673a":entry.mode=="HUMAN_AI"?L"\u4eba\u673a": // 单机；人机；模式列
                entry.mode=="ONLINE"?L"\u8054\u7f51":L"\u635f\u574f",30);
            if(page==REPLAY_LIST) text(1080,351+i*95,entry.result==1?L"\u9ed1\u80dc":entry.result==2?L"\u767d\u80dc":entry.result==3?L"\u5e73\u5c40":L"\u672a\u5b8c",22); // 联网；损坏；复盘列表额外显示胜负结果
            addButton(1001+at*2,1200,290+i*95,180,80,L"\u5220\u9664",dialog==NO_DIALOG); // 删除
        }
        if(entries.empty()) text(730,500,L"\u6682\u65e0\u5b58\u6863",48); // 暂无存档；一条都没有时显示提示
        text(530,1070,L"\u53ef\u6eda\u52a8\u9f20\u6807\u6eda\u8f6e\u67e5\u770b\u5176\u4ed6\u68cb\u5c40",26); // 可滚动鼠标滚轮查看其他棋局；操作说明
        addButton(A_BACK,740,1140,320,80,L"\u9000\u51fa",dialog==NO_DIALOG); // 退出；返回
        if(dialog==DELETE_CONFIRM) {
            buttons.clear();setfillcolor(EGERGB(249,232,200));solidroundrect(450,430,1350,840,30);  // 清除底层按钮，仅保留删除确认按钮，并绘制实心确认面板。
            text(650,475,L"\u662f\u5426\u6c38\u4e45\u5220\u9664\uff1f",52); // 是否永久删除？；询问文字
            std::wstring name=entries[deleteIndex].name;  // 显示要删除的棋局名
            text(510,570,name.size()>24?name.substr(0,24)+L"\u2026":name,30); // …；名字太长同样截断
            text(560,630,L"\u5b58\u6863\u6587\u4ef6\u5c06\u88ab\u5220\u9664\uff0c\u65e0\u6cd5\u64a4\u9500\u3002",32); // 存档文件将被删除，无法撤销。；强调不可撤销
            addButton(A_YES,570,720,260,80,L"\u662f\uff0c\u6c38\u4e45\u5220\u9664");addButton(A_NO,970,720,260,80,L"\u5426"); // 是，永久删除；否；是，永久删除 / 否
        }
    }

    // 首次调用定位：main.cpp:168 (drawButtons)。
    int x=-1,y=-1; mousepos(&x,&y); drawButtons(x,y);
    if(GetTickCount64()<noticeUntil) {  // 提示画在页面下方或右侧，避开棋盘
        int left=(page==GAME_PAGE || page==REPLAY_PAGE)?1235:520;  // 提示的位置随页面不同而不同
        int top=(page==SAVE_LIST || page==REPLAY_LIST)?20:1100;  // 在列表页显示在顶部，其他页面显示在下方
        setfillcolor(EGERGB(240,234,208)); solidroundrect(left,top,left+520,top+100,20);  // 提示底板
        text(left+20,top+32,notice,26);  // 提示文字
    }
}
// 【函数 loadImage】读取 path 图片，返回 PIMAGE，失败返回 NULL。
// 执行顺序：newimage 分配；getimage 加载；任一步失败就释放已分配 im。
// 参数/局部变量：path 为窄字符串路径；im 为新图像指针。
// 从上层理解：main.cpp:669 (main)。
PIMAGE loadImage(const char* path) {
    PIMAGE im=newimage();  // 创建一个新的图像对象
    if(!im || getimage(im,path)!=0) { if(im) delimage(im); return NULL; }  // getimage 从文件读入图片
    return im;
}
// 【函数 main（客户端）】程序入口，返回 0 表示素材/控件加载成功，1 表示加载失败等早期错误。
// 执行顺序：把当前目录改为 exe 目录；设置 DPI；建 1800×1350 窗口；加载图片与 90 帧；建输入框并挂窗
// 口回调；运行主循环；退出时停止网络与 AI，释放控件、字体、图片。
// 参数/局部变量：path、n、slash 是 exe 路径、长度、末级分隔符；DpiFunction 是函数指针类型，dpi 是
// 动态获取的 DPI 函数；loaded 是资源加载总结果；file 为动画文件名；started 为动画基准毫秒；message
//  为 Windows 消息；m 为 EGE 鼠标消息；hit 为命中按钮；cell 为松开时格子；images 为待释放图片数组；
// i 为循环下标。
// 边界与易错点：主循环内可能多次 render，不能理解成每轮严格只画一次。delay_fps(60) 是限帧目标，不
// 保证任何机器都恒定 60 帧。
int main() {
    wchar_t path[32768]; DWORD n=GetModuleFileNameW(NULL,path,32768);  // 取得本程序 exe 的完整路径
    if(!n || n>=32768) return 1;  // 路径异常就直接失败退出
    wchar_t* slash=std::wcsrchr(path,L'\\'); if(!slash) return 1;  // 找到最后一个反斜杠，把文件名部分截掉，剩下目录
    *slash=0; if(!SetCurrentDirectoryW(path)) return 1;  // 把工作目录切到 exe 所在目录，后续的相对路径就都对了
    typedef BOOL (WINAPI *DpiFunction)();
    DpiFunction dpi=reinterpret_cast<DpiFunction>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDPIAware"));  // 取得系统函数地址；老系统上可能没有，所以要先判断
    if(dpi) dpi();  // 有就调用它
    setinitmode(INIT_ANIMATION); initgraph(1800,1350); setcaption(L"\u4e94\u5b50\u68cb"); // 五子棋
    setrendermode(RENDER_MANUAL);
    SetWindowLongPtrW(getHWnd(),GWL_STYLE,GetWindowLongPtrW(getHWnd(),GWL_STYLE)|WS_CLIPCHILDREN);
    // 首次调用定位：main.cpp:655 (loadImage)。
    background=loadImage("assets/backgrounds/bg_a.png");  // 背景图
    boardImage=loadImage("assets/board/board.png");  // 棋盘图
    blackImage=loadImage("assets/pieces/black.png"); whiteImage=loadImage("assets/pieces/white.png");  // 黑白棋子
    buttonImage=loadImage("assets/ui/button_320x80.png");  // 按钮底板素材
    bool loaded=background && boardImage && blackImage && whiteImage && buttonImage;  // loaded 为假表示上面有任何一张图没加载成功
    for(int i=0;i<90 && loaded && is_run();++i) {
        char file[80]; std::sprintf(file,"assets/menu/frames/frame_%03d.png",i);  // 90 帧的文件名是 frame_000.png 到 frame_089.png
        frames[i]=loadImage(file); loaded=frames[i]!=NULL;  // 任何一帧失败就整体判为失败
        if(i==0 && loaded) putimage(0,0,frames[0]);
        delay_ms(1);  // 每帧之间稍微歇一下，避免启动时界面完全没反应
    }
    editFont=CreateFontW(-32,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"\u6977\u4f53"); // 楷体；创建楷体字体，负高度表示按字号大小算
    nameEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
        1260,710,460,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);  // 创建子编辑框；字体在后续WM_SETFONT消息中设置。
    if(nameEdit) {
        SendMessageW(nameEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        SendMessageW(nameEdit,EM_SETLIMITTEXT,24,0);  // 编辑控件限制24个UTF-16代码单元，不是所有Unicode字符都严格按24个计。
    }
    loaded=loaded && nameEdit;  // 素材或输入框有一个没成功，整体就算失败
    ipEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
        720,420,600,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);  // 创建IPv4地址输入框；后面的EM_SETLIMITTEXT才设置15字符上限。
    portEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"5000",WS_CHILD|ES_AUTOHSCROLL|ES_NUMBER,  // 端口框默认填 5000
        720,590,300,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);
    if(ipEdit) {SendMessageW(ipEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);SendMessageW(ipEdit,EM_SETLIMITTEXT,15,0);}  // 同样把字体设上，并限制长度
    if(portEdit) {SendMessageW(portEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);SendMessageW(portEdit,EM_SETLIMITTEXT,5,0);}
    loaded=loaded && ipEdit && portEdit;  // 三个控件都创建成功才算通过
    // 首次调用定位：main.cpp:319 (gameWindowProc)。
    originalWindowProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(getHWnd(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(gameWindowProc)));
    // 首次调用定位：record_store.h:65 (recordDirectories)。
    recordDirectories();  // 确保存档需要的目录都存在
    ULONGLONG started=GetTickCount64();  // 记下启动时刻，菜单动画以它为基准
    // 首次调用定位：main.cpp:595 (render)。
    if(loaded && is_run()) render(started);  // 先画第一帧
    // 【阅读提示】主循环阅读顺序：窗口消息 -> 网络更新 -> 绘制/鼠标 -> AI更新 -> 复盘计时 -> 再绘制。
    while(loaded && running && is_run()) {
        MSG message;  // message保存Windows窗口消息；取消息时的PM_REMOVE会将它从队列移除。
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) running=false;  // 收到退出消息就结束主循环
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        // 首次调用定位：online_ui.h:465 (updateOnline)。
        updateOnline();  // 处理联网消息
        if(InterlockedExchange(&windowCloseRequested,0)) {
            if(page==GAME_PAGE && gameMode=="ONLINE") {
                if(archiveStage==0) {  // 联网对局中途关窗口，先走联网的退出与保存询问
                    // 首次调用定位：online_ui.h:175 (onlineAsk)。
                    if(netState.phase==N_PLAY && !netPending) {closeAfterOnline=true;onlineAsk(2);}
                    // 首次调用定位：main.cpp:113 (toast)。
                    else toast(L"\u8bf7\u5148\u5b8c\u6210\u5f53\u524d\u8be2\u95ee\uff0c\u518d\u9000\u51fa");  // 还有询问没处理完，先提示玩家，不要强行退出
                // 首次调用定位：online_ui.h:215 (onlineRequestHome)。
                } else {closeAfterOnline=true;if(archiveStage>=3) onlineRequestHome();}
            // 首次调用定位：online_ui.h:341 (onlineLobbyLeave)。
            } else if(page==NETWORK_PAGE && netStage==4) {closeAfterOnline=true;onlineLobbyLeave();}  // 在联网等待室里关窗口，走等待室离开流程
            else running=false;  // 其余页面直接退出程序
        }
        if(running && is_run()) render(started);  // 重绘一帧
        while(mousemsg()) {
            mouse_msg m=getmouse();
            if(m.is_wheel() && dialog==NO_DIALOG && (page==SAVE_LIST || page==REPLAY_LIST)) {
                listOffset=std::max(0,std::min(listOffset+(m.wheel<0?1:-1),std::max(0,static_cast<int>(entries.size())-8)));  // 滚轮上下改变滚动位置，并限制在合法范围内
                continue;  // 滚轮事件处理完就继续看下一个事件
            }
            if(!m.is_left()) continue;
            // 【阅读提示】按下记录目标，松开时再次命中同一按钮/交点才执行，避免拖动产生误操作。
            // 首次调用定位：main.cpp:158 (hitButton)。
            int hit=hitButton(m.x,m.y);
            if(m.is_down()) {
                pressed=hit;
                // 首次调用定位：main.cpp:71 (humanCanMove)；main.cpp:443 (boardCell)。
                pressedCell=humanCanMove()?boardCell(m.x,m.y):-1;  // 记住按下时按在了哪个交点
            }
            if(m.is_up()) {
                // 首次调用定位：main.cpp:331 (act)。
                if(hit && hit==pressed) act(hit);  // 点击有效，执行这个按钮的功能
                else if(!pressed && pressedCell>=0 && humanCanMove()) {
                    int cell=boardCell(m.x,m.y);  // 先计算松开时交点，下一条件再与pressedCell比较。
                    if(cell==pressedCell) {
                        // 首次调用定位：online_ui.h:164 (onlineMove)。
                        if(gameMode=="ONLINE") onlineMove(cell);  // 联网落子要发给服务端确认
                        // 首次调用定位：game_logic.h:79 (LocalGame::place)。
                        else if(game.place(cell/15,cell%15) && game.result) dialog=FINISHED_SAVE;  // 单机和人机直接落子；分出胜负就询问是否保存
                    }
                }
                pressed=0; pressedCell=-1;  // 一次点击处理完，清空按下状态
                if(running && is_run()) render(started);
            }  // 立即重绘，让界面马上反映变化
        }
        while(kbhit()) getkey();  // 丢弃EGE键盘缓冲；原生EDIT的文字输入已由前面的Windows消息派发处理。
        if(!running || !is_run()) break;
        // 首次调用定位：main.cpp:416 (updateAI)。
        updateAI();  // 让 AI 检查是否有后台结果
        // 首次调用定位：replay_logic.h:87 (ReplayPlayer::tick)。
        if(page==REPLAY_PAGE && dialog==NO_DIALOG) replayPlayer.tick(GetTickCount64());
        render(started); delay_fps(60);  // 最后重绘并以60FPS为目标限帧；实际帧率受绘图和本轮处理耗时影响。
    }
    // 【阅读提示】资源清理先停网络与AI，避免线程仍在访问稍后要释放的数据。
    // 首次调用定位：online_ui.h:67 (onlineStop)。
    onlineStop();
    // 首次调用定位：ai_engine.h:365 (AIWorker::shutdown)。
    aiWorker.shutdown();  // 停掉 AI 后台线程，等它真正结束
    if(originalWindowProc && is_run()) SetWindowLongPtrW(getHWnd(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalWindowProc));  // 把窗口处理函数换回原来的
    if(ipEdit) DestroyWindow(ipEdit);  // 销毁输入框控件
    if(portEdit) DestroyWindow(portEdit);
    if(nameEdit) DestroyWindow(nameEdit);
    if(editFont) DeleteObject(editFont);  // 释放字体对象
    for(int i=0;i<90;++i) if(frames[i]) delimage(frames[i]);  // 释放 90 帧动画占用的内存
    PIMAGE images[]={background,boardImage,blackImage,whiteImage,buttonImage};  // 释放其余素材
    for(int i=0;i<5;++i) if(images[i]) delimage(images[i]);
    closegraph();  // 关闭图形窗口
    if(!loaded) MessageBoxW(NULL,L"\u7d20\u6750\u6216\u8f93\u5165\u6846\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5assets\u76ee\u5f55\u3002",L"\u542f\u52a8\u5931\u8d25",MB_OK|MB_ICONERROR); // 素材或输入框加载失败，请检查assets目录。；启动失败
    return loaded?0:1;  // 加载成功返回 0，失败返回 1
}
