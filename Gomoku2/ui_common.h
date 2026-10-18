// 【本文件阅读路线】所有页面共用：状态变量和 Button；addButton 登记 -> drawButtons 绘制 -> hitButton 判断点击。
// 客户端共享区：页面枚举、棋局对象、按钮数据和基础绘图函数。
// 本文件只保存大家共同使用的状态；业务动作在 game_actions.h。
#ifndef GOMOKU_UI_COMMON_H
#define GOMOKU_UI_COMMON_H
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
const int BOARD_X = 100, BOARD_Y = 200, FIRST_X = 178, FIRST_Y = 278, SPACING = 66;
// BOARD_X/Y图片左上角；FIRST_X/Y第一个交点；SPACING相邻交点像素距，勿混用。
const wchar_t* MAIN_LABELS[] = {
    L"\u5f00\u59cb\u6e38\u620f", L"\u7ee7\u7eed\u6e38\u620f", L"\u590d\u76d8", L"\u9000\u51fa"
};
// 开始游戏；继续游戏；复盘；退出
const wchar_t* MODE_LABELS[] = {
    L"\u5355\u673a\u5bf9\u5f08", L"\u4eba\u673a\u5bf9\u5f08", L"\u8054\u7f51\u5bf9\u5f08", L"\u7b97\u4e86\u4e0d\u73a9\u4e86"
};
// 单机对弈；人机对弈；联网对弈；算了不玩了
enum Page {
    MAIN_MENU, MODE_MENU, GAME_PAGE, SAVE_LIST, REPLAY_LIST, REPLAY_PAGE, DIFFICULTY_PAGE, NETWORK_PAGE
};
enum Dialog {
    NO_DIALOG, EXIT_SAVE, RESTART_CONFIRM, FINISHED_SAVE, NAME_SAVE, AGAIN, DELETE_CONFIRM, REPLAY_EXIT, CHOOSE_COLOR
};
enum Action {
    A_START=1, A_CONTINUE, A_REPLAY, A_QUIT, A_LOCAL, A_BACK,
    A_UNDO, A_EXIT_GAME, A_RESTART, A_YES, A_NO, A_SAVE, A_CANCEL, A_AI, A_EASY, A_MEDIUM, A_HARD,
    A_BLACK, A_WHITE, A_PLAY, A_SPEED1, A_SPEED2, A_PAUSE, A_RESUME,
    A_PREVIOUS, A_NEXT, A_REPLAY_EXIT
};
struct Button {
    int id, x, y, w, h;
    std::wstring text;
    bool enabled, menu;
};
struct SavedEntry {
    std::wstring name,path;
    std::string mode;
    bool valid;
    int result;
};
Page page = MAIN_MENU;
// 当前页面，决定render绘制哪一类界面。
Dialog dialog = NO_DIALOG;
LocalGame game;
// 本地/人机真实棋盘；联网模式下从收到的NetState.game复制更新。
ReplayPlayer replayPlayer;
// 复盘播放器对象，内部有独立的展示棋盘与完整历史。
GameRecord replayRecord;
// 当前复盘的完整记录元数据，供时间、结束原因和本机颜色等展示使用。
AIWorker aiWorker;
// 后台AI工作器；只在主线程采用结果时才改变全局game。
std::string gameMode="LOCAL_TWO";
// 当前模式：LOCAL_TWO同机双人，HUMAN_AI人机，ONLINE联网。
int difficulty=0, humanStone=1, generation=0, deleteIndex=-1;
// difficulty=0/1/2；humanStone=1/2；generation是AI有效任务版本；deleteIndex待删列表下标。
std::wstring currentName, loadedPath, replayName;
bool onlineCanMove();
std::vector<Button> buttons;
// 当前页面的按钮，每帧重建
std::vector<SavedEntry> entries;
// 存档/复盘列表的内容
PIMAGE frames[90] = {
}, background = NULL, boardImage = NULL;
// frames为90帧菜单动画；background为背景；boardImage为棋盘图。
PIMAGE blackImage = NULL, whiteImage = NULL, buttonImage = NULL;
HWND nameEdit = NULL;
HFONT editFont = NULL;
bool running = true, savingFinished = false;
// running 主循环开关；savingFinished 记住这局是否已结束
int pressed = 0, pressedCell = -1, listOffset = 0;
// 当前按下的按钮编号、按下的交点、列表滚动位置
std::wstring notice;
// 当前短提示文本；到期时刻单独存于noticeUntil。
ULONGLONG noticeUntil = 0;
// cancelAI：共享辅助函数。
void cancelAI() {
    ++generation;
    aiWorker.cancel();
}
// toast：共享辅助函数。
void toast(const std::wstring& text) {
    notice = text;
    noticeUntil = GetTickCount64() + 2000;
}
// font：共享辅助函数。
void font(int size) {
    LOGFONTW f;
    ZeroMemory(&f, sizeof(f));
    f.lfHeight = -size;
    f.lfQuality = ANTIALIASED_QUALITY;
    std::wcscpy(f.lfFaceName, L"\u6977\u4f53");
    setfont(&f);
    setbkmode(TRANSPARENT);
    // 楷体；老式的字符串拷贝写法，把「楷体」写进字体名
}
// text：共享辅助函数。
void text(int x, int y, const std::wstring& value, int size=32) {
    // 默认用 32 号字，颜色是深棕色
    font(size);
    setcolor(EGERGB(58,46,31));
    outtextxy(x,y,value.c_str());
    // outtextxy 把文字画在指定位置
}
// addButton：共享辅助函数。
void addButton(int id, int x, int y, int w, int h, const std::wstring& label,
bool enabled=true, bool menu=false) {
    Button b = {
        id,x,y,w,h,label,enabled,menu
    };
    buttons.push_back(b);
    // 用大括号一次性构造一个 Button 并追加到列表
}
// contains：共享辅助函数。
bool contains(const Button& b, int x, int y) {
    if (x < b.x || x >= b.x + b.w) {
        return false;
    }
    if (y < b.y || y >= b.y + b.h) {
        return false;
    }
    int radius = 21;
    int localX = x - b.x;
    int localY = y - b.y;
    int nearestX = localX;
    int nearestY = localY;
    if (nearestX > b.w - radius) {
        nearestX = b.w - radius;
    }
    if (nearestX < radius) {
        nearestX = radius;
    }
    if (nearestY > b.h - radius) {
        nearestY = b.h - radius;
    }
    if (nearestY < radius) {
        nearestY = radius;
    }
    int dx = localX - nearestX;
    int dy = localY - nearestY;
    return dx * dx + dy * dy <= radius * radius;
}
// hitButton：共享辅助函数。
int hitButton(int x,int y) {
    // 从buttons[0]向后找；首个可用命中项优先。
    for (size_t i=0;i<buttons.size();++i)
    {
        if (buttons[i].enabled && contains(buttons[i],x,y)) {
            return buttons[i].id;
        }
    }      // 只认「可点」的按钮；没点中返回 0
    return 0;
}
// drawButtons：共享辅助函数。
void drawButtons(int mouseX,int mouseY) {
    for (size_t i=0;i<buttons.size();++i) {
        const Button& b=buttons[i];
        if (!b.menu) {
            putimage_withalpha(NULL,buttonImage,b.x,b.y,b.w,b.h,0,0,320,80,true);
        }      // 菜单大按钮不贴素材底板，只用透明背景画文字
        if (b.enabled && pressed==b.id && contains(b,mouseX,mouseY)) {
            setfillcolor(EGERGB(220,220,220));
            solidroundrect(b.x,b.y,b.x+b.w,b.y+b.h,21);
        }
        if (b.menu) {
            font(48);
        }
        else {
            font(32);
        }
        if (b.enabled) {
            setcolor(EGERGB(58,46,31));
        }
        else {
            setcolor(EGERGB(125,125,115));
        }
        outtextxy(b.x+(b.w-textwidth(b.text.c_str()))/2,      // 让文字在按钮里居中显示
        b.y+(b.h-textheight(b.text.c_str()))/2,b.text.c_str());
        // 可点的是深色，不可点的是浅灰色，一眼能分辨
    }
}
// hideName：共享辅助函数。
void hideName() {
    ShowWindow(nameEdit,SW_HIDE);
    SetFocus(getHWnd());
}
// mainMenu：共享辅助函数。
void mainMenu() {
    cancelAI();
    hideName();
    page=MAIN_MENU;
    dialog=NO_DIALOG;
    pressed=0;
}
#endif
