// 【本文件阅读路线】程序入口：初始化 -> 循环处理输入/联网/AI/复盘并绘图 -> 清理资源。函数内的分支条件决定何时执行。
// Gomoku2 初学者入口：每一轮按固定顺序处理输入、业务更新和绘图。
// 先读本文件，再沿函数名进入 game_input.h / game_actions.h / game_logic.h。
#include "game_input.h"
int main() {
    bool loaded = initializeProgram();
    ULONGLONG started = GetTickCount64();
    if (loaded && is_run()) {
        render(started);
    }
    while (loaded && running && is_run()) {
        processWindowMessages();
        updateOnline();
        handleWindowCloseRequest();
        if (running && is_run()) {
            render(started);
        }
        processMouseMessages(started);
        while (kbhit()) {
            getkey();
        }
        if (!running || !is_run()) {
            break;
        }
        updateAI();
        if (page == REPLAY_PAGE && dialog == NO_DIALOG) {
            replayPlayer.tick(GetTickCount64());
        }
        render(started);
        delay_fps(60);
    }
    releaseProgramResources();
    if (!loaded && windowCreated) {
        MessageBoxW(NULL,L"\u7d20\u6750\u6216\u8f93\u5165\u6846\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5assets\u76ee\u5f55\u3002",L"\u542f\u52a8\u5931\u8d25",MB_OK|MB_ICONERROR);
    }
    if (loaded) {
        return 0;
    }
    return 1;
}
