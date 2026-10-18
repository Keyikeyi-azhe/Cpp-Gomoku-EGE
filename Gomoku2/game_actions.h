// 【本文件阅读路线】业务入口：act 分发按钮；单机/人机开局与保存、复盘操作、列表操作分组；联网操作交给 onlineAction。
// 业务动作区：按钮编号进来，修改page/dialog/game等状态。
// 阅读顺序：newGame -> 落子place -> beginName/saveGame -> mainMenu。
#ifndef GOMOKU_GAME_ACTIONS_H
#define GOMOKU_GAME_ACTIONS_H
#include "ui_common.h"
#include "online_ui.h"
// 排序规则单独命名，不在sort里写匿名函数。
bool newerEntryFirst(const SavedEntry& a, const SavedEntry& b) {
    return a.path > b.path;
}
// 列表一页最多八项；删除或滚动后都用同一处边界检查。
void limitListOffset() {
    int maximum = static_cast<int>(entries.size()) - 8;
    if (maximum < 0) {
        maximum = 0;
    }
    if (listOffset < 0) {
        listOffset = 0;
    }
    if (listOffset > maximum) {
        listOffset = maximum;
    }
}
// humanCanMove：此处是该业务函数的定义。
bool humanCanMove() {
    if (page != GAME_PAGE) {
        return false;
    }
    if (dialog != NO_DIALOG) {
        return false;
    }
    if (game.result != 0) {
        return false;
    }
    if (gameMode == "HUMAN_AI" && game.turn != humanStone) {
        return false;
    }
    if (gameMode == "ONLINE" && !onlineCanMove()) {
        return false;
    }
    return true;
}
// lastHumanMove：此处是该业务函数的定义。
int lastHumanMove() {
    for(int i=static_cast<int>(game.moves.size())-1;i>=0;--i)      // 只要有一手是自己的，就返回它的下标
    {
        if(game.moves[i].stone==humanStone) {
            return i;
        }
    }      // 倒序遇到的第一手属于玩家的棋，也就是玩家最近的一手。
    return -1;
}
// newGame：此处是该业务函数的定义。
void newGame(bool keepName=false) {
    cancelAI();
    hideName();
    game.reset();
    page=GAME_PAGE;
    if(!keepName) {
        currentName.clear();
        loadedPath.clear();
    }      // keepName为false才清名称和原残局路径；true用于保留身份的重开。
    if (gameMode == "HUMAN_AI") {
        dialog = CHOOSE_COLOR;
    } else {
        dialog = NO_DIALOG;
    }      // 人机模式还要先问玩家执黑还是执白
}
// openList：此处是该业务函数的定义。
void openList(bool isReplay,bool refresh=false) {
    entries.clear();
    // 每次都重建列表内容；是否保留滚动位置由下一行refresh决定。
    if(!refresh) {
        listOffset=0;
    }
    std::wstring dir;
    std::wstring pattern;
    if (isReplay) {
        dir = L"data\\replays\\";
        pattern = L"*.greplay";
    } else {
        dir = L"data\\unfinished\\";
        pattern = L"*.gsave";
    }
    WIN32_FIND_DATAW info;
    HANDLE find=FindFirstFileW((dir + pattern).c_str(),&info);
    // FindFirstFileW 按通配符找文件
    if(find!=INVALID_HANDLE_VALUE) {
        // 无匹配文件或查找失败时句柄无效；当前界面统一按无结果处理。
        do {
            if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) {
                continue;
            }
            SavedEntry entry;
            entry.path=dir+info.cFileName;
            // 路径存起来，删除时要用
            GameRecord record;
            entry.valid=readRecord(entry.path,isReplay,record);
            if (entry.valid) {
                entry.name = record.name;
                entry.mode = record.mode;
                entry.result = record.game.result;
            } else {
                entry.name = info.cFileName;
                entry.mode = "INVALID";
                entry.result = -1;
            }
            entries.push_back(entry);
        } while(FindNextFileW(find,&info));
        // 一直找到没有下一个文件为止
        FindClose(find);
        // 用完记得关闭查找句柄
    }
    // 语句说明：lambda比较路径，降序排列；文件名含时间信息，但这里没有读取文件修改时间。
    std::sort(entries.begin(),entries.end(),newerEntryFirst);
    limitListOffset();
    if(entries.empty() && !refresh) {
        toast(L"\u6682\u65e0\u5b58\u6863");
        return;
    }     // 暂无存档；列表为空就提示「暂无存档」，并停在原页面
    if (isReplay) {
        page=REPLAY_LIST;
    } else {
        page=SAVE_LIST;
    }
    dialog=NO_DIALOG;
}
// saveGame：此处是该业务函数的定义。
bool saveGame(std::wstring name) {
    size_t first=name.find_first_not_of(L" \t\r\n"),last=name.find_last_not_of(L" \t\r\n");
    if(first==std::wstring::npos) {
        toast(L"\u8bf7\u8f93\u5165\u68cb\u5c40\u540d\u79f0");
        return false;
    }     // 请输入棋局名称
    name=name.substr(first,last-first+1);
    // 去掉了首尾空白后的名字
    if(name.size()>24 || name.find_first_of(L"\r\n\t")!=std::wstring::npos) {
        toast(L"\u540d\u79f0\u965024\u5b57\uff0c\u4e0d\u80fd\u5305\u542b\u6362\u884c");
        return false;
    }     // 名称限24字，不能包含换行
    GameRecord record;
    record.game=game;
    record.mode=gameMode;
    record.difficulty=difficulty;
    record.human=humanStone;
    record.name=name;
    record.replay=game.result!=0;
    // 已结束的棋局存成复盘，未结束的存成残局
    std::wstring overwritePath;
    if (!record.replay) {
        overwritePath = loadedPath;
    }
    if(!writeRecord(record,overwritePath)) {
        toast(L"\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u76ee\u5f55\u540e\u91cd\u8bd5");
        return false;
    }     // 保存失败，请检查目录后重试
    currentName=name;
    if(record.replay && !loadedPath.empty()) {
        if(!DeleteFileW(loadedPath.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND)
        {
            toast(L"\u590d\u76d8\u5df2\u4fdd\u5b58\uff0c\u539f\u6b8b\u5c40\u8bf7\u624b\u52a8\u5220\u9664");
        }     // 复盘已保存，原残局请手动删除
        else {
            toast(L"\u590d\u76d8\u5df2\u4fdd\u5b58");
        }     // 复盘已保存；复盘已保存，原残局可以删掉了
        loadedPath.clear();
    } else {
        if(!record.replay) {
            loadedPath=record.path;
        }
        toast(L"\u68cb\u5c40\u5df2\u4fdd\u5b58");
    }     // 棋局已保存；残局存好了，记下路径，方便以后续局时覆盖
    return true;
}
// savedSuccessfully：此处是该业务函数的定义。
void savedSuccessfully() {
    hideName();
    if (game.result != 0) {
        dialog = AGAIN;
    } else {
        mainMenu();
    }
}
// beginName：此处是该业务函数的定义。
void beginName() {
    savingFinished=game.result!=0;
    if(!currentName.empty()) {
        if(saveGame(currentName)) {
            savedSuccessfully();
        }
        return;
    }
    dialog=NAME_SAVE;
    SetWindowTextW(nameEdit,L"");
    ShowWindow(nameEdit,SW_SHOW);
    SetFocus(nameEdit);
}
// loadEntry：此处是该业务函数的定义。
void loadEntry(int index) {
    if(index<0 || index>=static_cast<int>(entries.size())) {
        return;
    }
    GameRecord record;
    bool isReplay=page==REPLAY_LIST;
    if(!readRecord(entries[index].path,isReplay,record)) {
        toast(L"\u5b58\u6863\u635f\u574f\u6216\u65e0\u6cd5\u8bfb\u53d6");
        return;
    }     // 存档损坏或无法读取
    if(!isReplay && record.mode=="ONLINE") {
        toast(L"\u8054\u7f51\u68cb\u5c40\u4e0d\u80fd\u79bb\u7ebf\u7eed\u5c40");
        return;
    }     // 联网棋局不能离线续局
    cancelAI();
    hideName();
    dialog=NO_DIALOG;
    if(isReplay) {
        replayRecord=record;
        replayName=record.name;
        replayPlayer.open(record.game);
        page=REPLAY_PAGE;
    }
    else {
        // 恢复出这一局的所有信息
        game=record.game;
        gameMode=record.mode;
        difficulty=record.difficulty;
        humanStone=record.human;
        currentName=record.name;
        loadedPath=record.path;
        page=GAME_PAGE;
        // 记下名字和路径，后续保存时沿用
    }
}
// updateAI：此处是该业务函数的定义。
// 是否轮到电脑：把人机专用的条件集中放在这里。
bool isComputerTurn() {
    return page == GAME_PAGE && dialog == NO_DIALOG && gameMode == "HUMAN_AI"
    && game.result == 0 && game.turn != humanStone;
}
// 收回后台计算结果；旧对局结果或非人机页面的结果不能落到当前棋盘。
void receiveAIResult() {
    AIPoint point;
    int resultGeneration = 0;
    if (!aiWorker.take(point, resultGeneration)) {
        return;
    }
    if (resultGeneration != generation || !isComputerTurn()) {
        return;
    }
    if (game.place(point.row, point.col) && game.result != 0) {
        cancelAI();
        dialog = FINISHED_SAVE;
    }
}
// 仅人机且轮到电脑：没有现存线程时，开始计算下一手。
void startAIIfNeeded() {
    if (!isComputerTurn() || aiWorker.thread) {
        return;
    }
    if (!aiWorker.start(game, difficulty, generation)) {
        toast(L"AI\u7ebf\u7a0b\u542f\u52a8\u5931\u8d25\uff0c\u53ef\u91cd\u5f00\u6216\u9000\u51fa");
        dialog = RESTART_CONFIRM;
    }
}
// AI 更新入口：先收结果，再决定是否需要启动下一次计算。
void updateAI() {
    receiveAIResult();
    startAIIfNeeded();
}
// boardCell：此处是该业务函数的定义。
int boardCell(int x,int y) {
    int dx=x-FIRST_X, dy=y-FIRST_Y;
    if(dx < -30 || dy < -30 || dx > 14*SPACING+30 || dy > 14*SPACING+30) {
        return -1;
    }      // 棋盘外太远的点击直接丢弃
    int col=(dx+SPACING/2)/SPACING, row=(dy+SPACING/2)/SPACING;
    // 四舍五入吸附到最近的交点
    dx-=col*SPACING;
    dy-=row*SPACING;
    if(row<0 || row>=15 || col<0 || col>=15 || dx*dx+dy*dy>900) {
        return -1;
    }      // 换算偏移后判断是否真的足够近
    return row*15+col;
    // 把二维坐标压成一维编号返回
}
// 返回true表示这个组已经处理了按钮。
bool handleMenuAction(int id) {
    switch(id) {
        case A_START:
        page=MODE_MENU;
        break;
        // 开始游戏 → 进入模式菜单
        case A_CONTINUE:
        openList(false);
        break;
        case A_REPLAY:
        openList(true);
        break;
        case A_QUIT:
        running=false;
        break;
        case A_LOCAL:
        gameMode="LOCAL_TWO";
        newGame();
        break;
        // 单机双人
        case A_AI:
        page=DIFFICULTY_PAGE;
        break;
        case A_EASY:
        case A_MEDIUM:
        case A_HARD:
        if(id==A_EASY) {
            difficulty=0;
        } else if(id==A_MEDIUM) {
            difficulty=1;
        } else {
            difficulty=2;
        }
        gameMode="HUMAN_AI";
        newGame();
        break;
        // 选了难度后进入人机模式
        case A_BLACK:
        case A_WHITE:
        if(id==A_BLACK) {
            humanStone=1;
        } else {
            humanStone=2;
        }
        dialog=NO_DIALOG;
        break;
        // 记下玩家选的颜色并关闭询问
        case A_BACK:
        mainMenu();
        break;
        // 【阅读提示】人机悔棋以最近一次人类落子为界，一并撤掉后续电脑应答；不是固定撤两手。
        default: return false;
    }
    return true;
}
// 返回true表示这个组已经处理了按钮。
bool handleGameAction(int id) {
    switch(id) {
        case A_UNDO:
        cancelAI();
        if(gameMode=="HUMAN_AI") {
            int at=lastHumanMove();
            if(at>=0) {
                while(static_cast<int>(game.moves.size())>at) {
                    game.undo();
                }
            }
        } else {
            game.undo();
        }      // 单机模式直接撤最后一手
        break;
        case A_EXIT_GAME:
        cancelAI();
        if(game.result!=0) {
            dialog=AGAIN;
        } else {
            dialog=EXIT_SAVE;
        }
        break;
        // 棋局已结束就直接问「再来一局」，否则问是否保存
        case A_RESTART:
        if(!game.result) {
            cancelAI();
            dialog=RESTART_CONFIRM;
        }
        break;
        // 重开前先确认，而且只有棋局没结束时才需要
        // 【阅读提示】同一个A_YES按钮按dialog解释；先核对正在询问什么，再看该分支会保存、重开还是删除。
        default: return false;
    }
    return true;
}
// 返回true表示这个组已经处理了按钮。
bool handleDialogAction(int id) {
    switch(id) {
        case A_YES:
        if(dialog==DELETE_CONFIRM) {
            if(deleteIndex>=0 && deleteIndex<static_cast<int>(entries.size()) &&      // 删除成功后刷新列表并提示
            DeleteFileW(entries[deleteIndex].path.c_str())) {
                bool isReplay=page==REPLAY_LIST;
                openList(isReplay,true);
                toast(L"\u5df2\u6c38\u4e45\u5220\u9664");
                // 已永久删除
            } else {
                toast(L"\u5220\u9664\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u6587\u4ef6\u6743\u9650");
            }     // 删除失败会提示检查权限；失败原因也可能是文件占用、路径变化等。
        } else if(dialog==REPLAY_EXIT) {
            replayPlayer.pause(GetTickCount64());
            mainMenu();
        }
        else if(dialog==EXIT_SAVE || dialog==FINISHED_SAVE) {
            beginName();
        }      // 要保存就进入命名流程
        else if(dialog==RESTART_CONFIRM) {
            newGame(true);
        }      // 重开：保留名字
        else if(dialog==AGAIN) {
            newGame();
        }      // 再来一局：用全新的棋局
        break;
        case A_NO:
        if(dialog==DELETE_CONFIRM || dialog==REPLAY_EXIT) {
            dialog=NO_DIALOG;
        }      // 删除询问和退出复盘询问都是直接关闭弹窗
        else if(dialog==EXIT_SAVE || dialog==AGAIN) {
            mainMenu();
        }      // 退出对局或不再来一局，都回主菜单
        else if(dialog==RESTART_CONFIRM) {
            dialog=NO_DIALOG;
        }
        else if(dialog==FINISHED_SAVE) {
            dialog=AGAIN;
        }      // 本局已结束时取消保存，回到「再来一局」的询问
        break;
        default: return false;
    }
    return true;
}
// 返回true表示这个组已经处理了按钮。
bool handleSaveAction(int id) {
    switch(id) {
        case A_SAVE:
        {
            wchar_t name[128];
            GetWindowTextW(nameEdit,name,128);
            // 缓冲容量128个wchar_t（含末尾0）；控件此前已设置输入长度24。
            if(saveGame(name)) {
                savedSuccessfully();
            }
            break;
        }
        case A_CANCEL:
        hideName();
        if(savingFinished) {
            dialog=FINISHED_SAVE;
        } else {
            dialog=EXIT_SAVE;
        }
        break;
        default: return false;
    }
    return true;
}
// 返回true表示这个组已经处理了按钮。
bool handleReplayAction(int id) {
    switch(id) {
        case A_PLAY:
        case A_RESUME:
        replayPlayer.play(GetTickCount64());
        break;
        // PLAY与RESUME共用play；PAUSE在下一分支单独处理。
        case A_PAUSE:
        replayPlayer.pause(GetTickCount64());
        break;
        case A_SPEED1:
        replayPlayer.speed(3000,GetTickCount64());
        break;
        // 1x 速度：每手停 3 秒
        case A_SPEED2:
        replayPlayer.speed(1500,GetTickCount64());
        break;
        // 2x 速度：每手停 1.5 秒
        case A_PREVIOUS:
        if(replayPlayer.cursor) {
            replayPlayer.seek(replayPlayer.cursor-1);
        }
        break;
        // 单步回看
        case A_NEXT:
        if(replayPlayer.cursor<replayPlayer.moves.size()) {
            replayPlayer.seek(replayPlayer.cursor+1);
        }
        break;
        case A_REPLAY_EXIT:
        replayPlayer.pause(GetTickCount64());
        dialog=REPLAY_EXIT;
        break;
        // 退出复盘前先暂停，再弹询问
        default: return false;
    }
    return true;
}
// 动态列表按钮：偶数载入，奇数删除；编号规则保持与绘图一致。
bool handleListAction(int id) {
    if (id < 1000) {
        return false;
    }
    if (dialog != NO_DIALOG) {
        return true;
    }
    int index = (id - 1000) / 2;
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return true;
    }
    if ((id - 1000) % 2 == 1) {
        deleteIndex = index;
        dialog = DELETE_CONFIRM;
    } else {
        loadEntry(index);
    }
    return true;
}
// 所有按钮只从这个入口分发；先定位功能组，再进入具体case。
void act(int id) {
    if (id >= A_ONLINE && id <= A_NET_COPY) {
        onlineAction(id);
        return;
    }
    if (handleListAction(id)) {
        return;
    }
    if (handleMenuAction(id)) {
        return;
    }
    if (handleGameAction(id)) {
        return;
    }
    if (handleDialogAction(id)) {
        return;
    }
    if (handleSaveAction(id)) {
        return;
    }
    handleReplayAction(id);
}
#endif
