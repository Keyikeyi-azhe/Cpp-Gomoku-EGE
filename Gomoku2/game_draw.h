// 【本文件阅读路线】绘图主线：render 选页面 -> drawGame 画公共棋盘/结果 -> 按复盘、联网、人机、单机选择操作区。
// 显示区：读取状态画页面，不在这里执行落子、保存或网络请求。
#ifndef GOMOKU_GAME_DRAW_H
#define GOMOKU_GAME_DRAW_H
#include "game_actions.h"
// 公共显示：所有模式都画背景、棋盘和已有棋子；只读 shown，不改变棋局。
void drawBoard(const LocalGame& shown, bool replay) {
    putimage(0,0,background);
    // 先铺背景，再叠棋盘
    putimage_withalpha(NULL,boardImage,BOARD_X,BOARD_Y,1080,1080,0,0,720,720,true);
    std::wstring title;
    if (replay) {
        title = L"\u68cb\u5c40\u590d\u76d8";
    } else if (gameMode == "ONLINE") {
        title = L"\u8054\u7f51\u5bf9\u5f08";
    } else if (gameMode == "HUMAN_AI") {
        title = L"\u4eba\u673a\u5bf9\u5f08";
    } else {
        title = L"\u5355\u673a\u5bf9\u5f08";
    }
    // 棋局复盘；人机对弈；单机对弈；标题：联网、复盘、人机、单机各有说法
    if(!shown.result) {
        text(150,95,title+L" \u00b7 \u9ed1\u5148\u767d\u540e",48);
    }     // · 黑先白后；棋局结束时顶部不再显示标题和先手说明
    for(size_t i=0;i<shown.moves.size();++i) {
        const Move& m=shown.moves[i];
        // 逐手画出棋子
        putimage_withalpha(NULL,m.stone==1?blackImage:whiteImage,FIRST_X+m.col*SPACING-33,
        FIRST_Y+m.row*SPACING-33,66,66,0,0,44,44,true);
        // 目标棋子图像66×66像素；读取源图的44×44区域，再居中放到交点。
    }
}
// 仅联网：实时对局画落子预览；实时对局和联网复盘标记最后一手。
void drawOnlineBoardExtras(const LocalGame& shown, bool replay) {
    // 【阅读提示】联网悬停预览只是半透明图像，不会写入cells/moves；正式棋子要等权威STATE。
    if(!replay && humanCanMove()) {
        int mx=-1,my=-1;
        mousepos(&mx,&my);
        int cell=boardCell(mx,my);
        if(cell>=0 && !shown.cells[cell/15][cell%15])      // 只有空位才显示预览
        {
            putimage_alphablend(NULL,game.turn==1?blackImage:whiteImage,FIRST_X+(cell%15)*SPACING-33,
            FIRST_Y+(cell/15)*SPACING-33,66,66,110,0,0,44,44,true,COLORTYPE_ARGB32);
        }      // 110 是半透明程度
    }
    if(!shown.moves.empty()) {
        const Move& last=shown.moves.back();
        setcolor(EGERGB(213,74,35));
        setlinewidth(3);
        // 最后一手用小红圈标出来
        circle(FIRST_X+last.col*SPACING,FIRST_Y+last.row*SPACING,7);
        setlinewidth(1);
    }
}
// 公共结果显示：未结束时不画横幅；获胜时圈出连成五颗的棋子。
void drawGameResult(const LocalGame& shown, bool replay) {
    int endReason = END_UNKNOWN;
    if (replay) {
        endReason = replayRecord.reason;
    } else if (gameMode == "ONLINE") {
        endReason = netState.reason;
    }
    // reason 来自复盘记录或联网状态
    if(shown.result) {
        setfillcolor(EGERGB(128,48,32));
        solidroundrect(100,65,1180,185,25);
        // 深红色横幅
        font(68);
        setcolor(EGERGB(255,231,151));
        // 金色大字
        const wchar_t* banner;
        if (shown.result==1) {
            banner = L"\u9ed1 \u65b9 \u83b7 \u80dc\uff01";
        } else {
            if (shown.result==2) {
                banner = L"\u767d \u65b9 \u83b7 \u80dc\uff01";
            } else {
                banner = L"\u672c \u5c40 \u5e73 \u5c40";
            }
        }
        // 黑 方 获 胜！；白 方 获 胜！；本 局 平 局
        if(endReason==END_RESIGN) {
            banner=shown.result==1?L"\u9ed1\u65b9\u83b7\u80dc\uff08\u5bf9\u65b9\u8ba4\u8f93\uff09":L"\u767d\u65b9\u83b7\u80dc\uff08\u5bf9\u65b9\u8ba4\u8f93\uff09";
        }
        outtextxy(640-textwidth(banner)/2,88,banner);
        // 文字水平居中
        // 【阅读提示】认输有胜者但未必五连，禁止把它画成连五；平局也没有获胜连线。
        if(endReason!=END_RESIGN && shown.result!=3 && !shown.moves.empty()) {
            Move last=shown.moves.back();
            int dirs[4][2]= {
                {
                    1,0
                }, {
                    0,1
                }, {
                    1,1
                }, {
                    1,-1
                }
            };
            // 方向增量按行/列解释：竖直、水平、两条斜线；line用于收集胜线上的棋子。
            setcolor(EGERGB(229,152,28));
            setlinewidth(4);
            // 金色，并加粗线宽
            for(int d=0;d<4;++d) {
                std::vector<Move> line;
                line.push_back(last);
                for(int sign=-1;sign<=1;sign+=2) {
                    // 往两头各延伸一次
                    int r=last.row+sign*dirs[d][0],c=last.col+sign*dirs[d][1];
                    // 从相邻一格开始
                    while(r>=0 && r<15 && c>=0 && c<15 && shown.cells[r][c]==last.stone) {
                        // 同色就继续往前
                        Move m= {
                            r,c,last.stone
                        };
                        line.push_back(m);
                        r+=sign*dirs[d][0];
                        c+=sign*dirs[d][1];
                        // 把这一颗收进连线里
                    }
                }
                if(line.size()>=5) {
                    for(size_t i=0;i<line.size();++i)      // 半径 35 的金圈套住每一颗棋子
                    {
                        circle(FIRST_X+line[i].col*SPACING,FIRST_Y+line[i].row*SPACING,35);
                    }
                }
            }
            setlinewidth(1);
            // 线宽还原
        }
    }
}
// 公共底板：在棋盘右侧画浅米色圆角矩形。
void drawSidebarBase() {
    setfillcolor(EGERGB(246,241,224));
    solidroundrect(1220,120,1760,1250,24);
}
// 仅复盘：播放、暂停、速度、前后步和退出询问。
void drawReplayPanel() {
    text(1270,155,L"\u68cb\u5c40\u590d\u76d8",44);
    // 棋局复盘；面板标题
    text(1250,220,replayName.size()>14?replayName.substr(0,14)+L"\u2026":replayName,30);
    // …；棋局名太长时截断并加省略号
    wchar_t step[80];
    std::swprintf(step,80,L"%u / %u",static_cast<unsigned>(replayPlayer.cursor),static_cast<unsigned>(replayPlayer.moves.size()));
    // 当前步数与总步数
    text(1370,275,step,36);
    // 这里只显示播放进度；下面依据started决定是否出现速度按钮。
    bool free=dialog==NO_DIALOG;
    if(!replayPlayer.started) {
        addButton(A_PLAY,1330,340,320,80,L"\u81ea\u52a8\u64ad\u653e",free && !replayPlayer.moves.empty());
    }     // 自动播放
    else {
        // 曾经开始过播放就显示速度按钮；正在播放与暂停状态都可看到。
        addButton(A_SPEED1,1260,340,220,75,replayPlayer.interval==3000?L"1x\uff08\u5f53\u524d\uff09":L"1x",free);
        // 1x（当前）；当前速度的按钮会加「（当前）」字样
        addButton(A_SPEED2,1500,340,220,75,replayPlayer.interval==1500?L"2x\uff08\u5f53\u524d\uff09":L"2x",free);
        // 2x（当前）
        addButton(replayPlayer.playing?A_PAUSE:A_RESUME,1330,440,320,80,      // 暂停或继续播放
        replayPlayer.playing?L"\u6682\u505c":L"\u7ee7\u7eed",free && replayPlayer.cursor<replayPlayer.moves.size());
        // 暂停；继续；播完了就不能再点继续
    }
    if(!replayPlayer.playing) {
        addButton(A_PREVIOUS,1260,560,220,75,L"\u4e0a\u4e00\u6b65",free && replayPlayer.cursor>0);
        // 上一步；上一步 / 下一步
        addButton(A_NEXT,1500,560,220,75,L"\u4e0b\u4e00\u6b65",free && replayPlayer.cursor<replayPlayer.moves.size());
        // 下一步；到头了按钮会自动变灰
    }
    if(replayPlayer.cursor==replayPlayer.moves.size()) {
        text(1290,665,L"\u64ad\u653e\u7ed3\u675f\uff0c\u53ef\u624b\u52a8\u56de\u770b",28);
    }     // 播放结束，可手动回看
    addButton(A_REPLAY_EXIT,1330,740,320,80,L"\u9000\u51fa",free);
    // 退出；退出复盘
    if(dialog==NO_DIALOG) {
        text(1250,860,matchTime(replayRecord.started),26);
        // 显示这一局的开始时间
        if(replayRecord.mode=="ONLINE") {
            text(1250,905,replayRecord.version<2?L"\u672c\u673a\u6267\u68cb\uff1a\u65e7\u8bb0\u5f55\u672a\u8bb0\u5f55":
            replayRecord.human==1?L"\u672c\u673a\u5f53\u65f6\u6267\u9ed1 \u00b7 \u623f\u4e3b":L"\u672c\u673a\u5f53\u65f6\u6267\u767d \u00b7 \u670b\u53cb",28);
        }
        text(1250,950,reasonText(replayRecord.reason),28);
        // 显示结束原因
        if(!replayRecord.game.result) {
            text(1250,995,L"\u672a\u5b8c\u590d\u76d8\uff0c\u4e0d\u53ef\u7ee7\u7eed\u5bf9\u5c40",28);
        }
    }
    if(dialog==REPLAY_EXIT) {
        text(1260,885,L"\u5df2\u6682\u505c\uff0c\u662f\u5426\u9000\u51fa\u590d\u76d8\uff1f",32);
        // 已暂停，是否退出复盘？；询问是否退出复盘
        addButton(A_YES,1260,1000,220,75,L"\u662f");
        addButton(A_NO,1500,1000,220,75,L"\u5426");
        // 是；否；是 / 否
    }
}
// 单机和人机共用：显示结果、轮到谁，或电脑思考中。
void drawLocalStatus() {
    const wchar_t* status;
    if (game.result==1) {
        status = L"\u9ed1\u65b9\u83b7\u80dc";
    } else {
        if (game.result==2) {
            status = L"\u767d\u65b9\u83b7\u80dc";
        } else {
            if (game.result==3) {
                status = L"\u672c\u5c40\u5e73\u5c40";
            } else {
                if (gameMode=="HUMAN_AI" && game.turn!=humanStone && dialog!=CHOOSE_COLOR) {
                    status = L"\u7535\u8111\u601d\u8003\u4e2d\u2026";
                } else {
                    if (game.turn==1) {
                        status = L"\u8f6e\u5230\u9ed1\u65b9\u843d\u5b50";
                    } else {
                        status = L"\u8f6e\u5230\u767d\u65b9\u843d\u5b50";
                    }
                }
            }
        }
    }
    // 电脑思考中…；轮到黑方落子；轮到白方落子；轮到黑方或白方
    text(1280,150,status,40);
    // 把状态文字画在右侧面板上
}
// 仅人机：显示难度和玩家执棋颜色。
void drawAIDetails() {
    std::wstring level;
    if (difficulty==0) {
        level = L"\u7b80\u5355";
    } else {
        if (difficulty==1) {
            level = L"\u4e00\u822c";
        } else {
            level = L"\u56f0\u96be";
        }
    }
    // 简单；一般；困难；三个难度名
    text(1260,202,level+(dialog==CHOOSE_COLOR?L" \u00b7 \u7b49\u5f85\u9009\u8272":humanStone==1?L" \u00b7 \u4f60\u6267\u9ed1":L" \u00b7 \u4f60\u6267\u767d"),25);
    // · 等待选色； · 你执黑； · 你执白
}
// 单机和人机共用：登记悔棋、重开、退出按钮及其可用状态。
void addLocalGameButtons() {
    bool free=dialog==NO_DIALOG && !game.result;
    addButton(A_UNDO,1290,250,400,80,gameMode=="HUMAN_AI"?L"\u6094\u68cb\uff08\u64a4\u56de\u4e00\u56de\u5408\uff09":L"\u6094\u68cb",     // 悔棋（撤回一回合）；悔棋；人机模式要撤回一回合，所以提示文字不同
    free && (gameMode=="HUMAN_AI"?lastHumanMove()>=0:!game.moves.empty()));
    // 人机模式下必须玩家真的下过子才允许悔棋
    addButton(A_RESTART,1330,350,320,80,L"\u91cd\u5f00",free);
    // 重开；重开
    addButton(A_EXIT_GAME,1330,450,320,80,L"\u9000\u51fa",dialog==NO_DIALOG);
    // 退出；退出对局
}
// 单机和人机共用询问；CHOOSE_COLOR 只在人机开局出现。
void drawLocalDialog() {
    if(dialog==CHOOSE_COLOR) {
        buttons.clear();
        // 选色询问只显示黑、白两个可操作按钮。
        setfillcolor(EGERGB(247,237,202));
        solidroundrect(380,440,1420,860,30);
        // 绘制实心米色圆角面板；本调用没有透明混合参数。
        text(590,490,L"\u8bf7\u9009\u62e9\u4f60\u6267\u9ed1\u8fd8\u662f\u6267\u767d",52);
        // 请选择你执黑还是执白；询问文字
        text(670,580,L"\u9ed1\u68cb\u5148\u4e0b\uff0c\u767d\u68cb\u540e\u4e0b",40);
        // 黑棋先下，白棋后下；补充说明谁先下
        addButton(A_BLACK,510,700,320,90,L"\u6211\u6267\u9ed1");
        addButton(A_WHITE,970,700,320,90,L"\u6211\u6267\u767d");
        // 我执黑；我执白；我执黑 / 我执白
    } else if(dialog!=NO_DIALOG) {
        text(1270,580,game.result?L"\u672c\u5c40\u5df2\u7ed3\u675f":L"\u68cb\u5c40\u63d0\u793a",40);
        // 本局已结束；棋局提示
        if(dialog==EXIT_SAVE) {
            text(1260,650,L"\u662f\u5426\u4fdd\u5b58\u5f53\u524d\u8fdb\u5ea6\uff1f");
        }     // 是否保存当前进度？
        if(dialog==FINISHED_SAVE) {
            text(1260,650,L"\u662f\u5426\u4fdd\u5b58\u672c\u5c40\u590d\u76d8\uff1f");
        }     // 是否保存本局复盘？
        if(dialog==AGAIN) {
            text(1260,650,L"\u8981\u4e0d\u8981\u518d\u6765\u4e00\u5c40\uff1f");
        }     // 要不要再来一局？
        if(dialog==RESTART_CONFIRM) {
            text(1260,650,L"\u8fdb\u5ea6\u4e0d\u4f1a\u88ab\u4fdd\u5b58\u3002");
            text(1260,695,L"\u662f\u5426\u91cd\u5f00\uff1f");
        }     // 进度不会被保存。；是否重开？
        if(dialog==NAME_SAVE) {
            // 命名框由原生控件实现，这里只画提示文字
            text(1260,650,L"\u8bf7\u4e3a\u672c\u68cb\u5c40\u547d\u540d\uff1a");
            text(1260,800,L"\u6700\u591a24\u5b57\uff0c\u652f\u6301\u4e2d\u6587",28);
            // 请为本棋局命名：；最多24字，支持中文；说明字数限制
            addButton(A_SAVE,1260,900,220,75,L"\u4fdd\u5b58");
            addButton(A_CANCEL,1500,900,220,75,L"\u53d6\u6d88");
            // 保存；取消；保存 / 取消
        } else {
            addButton(A_YES,1260,900,220,75,L"\u662f");
            addButton(A_NO,1500,900,220,75,L"\u5426");
            // 是；否；是 / 否
        }
    }
}
// 仅单机双人：状态、操作按钮、当前询问。
void drawLocalPanel() {
    drawLocalStatus();
    addLocalGameButtons();
    drawLocalDialog();
}
// 仅人机：比单机多显示难度与执棋颜色。
void drawAIPanel() {
    drawLocalStatus();
    drawAIDetails();
    addLocalGameButtons();
    drawLocalDialog();
}
// 阅读入口：公共棋盘 -> 公共结果 -> 公共底板 -> 按模式选择显示。
// 联网额外标记归入对应模式；drawOnlineSidebar 定义在 online_ui.h。
void drawGame() {
    bool replay = page == REPLAY_PAGE;
    const LocalGame* shown = &game;
    if (replay) {
        shown = &replayPlayer.board;
    }
    drawBoard(*shown, replay);
    drawGameResult(*shown, replay);
    drawSidebarBase();
    if (replay) {
        if (replayRecord.mode == "ONLINE") {
            drawOnlineBoardExtras(*shown, true);
        }
        drawReplayPanel();
    } else if (gameMode == "ONLINE") {
        drawOnlineBoardExtras(*shown, false);
        drawOnlineSidebar();
    } else if (gameMode == "HUMAN_AI") {
        drawAIPanel();
    } else {
        drawLocalPanel();
    }
}
// 仅主菜单、模式菜单、难度菜单：背景动画和四个选项。
void drawMenuPage(ULONGLONG startedAt) {
    int frame=static_cast<int>(((GetTickCount64()-startedAt)%3000)*30/1000);
    // 当前是第几帧(0 到 89)
    putimage(0,0,frames[frame]);
    // 画出这一帧动画
    int ys[]= {
        476,591,705,821
    };
    // 四个按钮的纵坐标
    int ids[]= {
        A_START,A_CONTINUE,A_REPLAY,A_QUIT
    };
    // 主菜单四个按钮的编号
    for(int i=0;i<4;++i) {
        // 依次添加四个按钮
        int id;
        if (page == MAIN_MENU) {
            id = ids[i];
        } else {
            if (i == 0) {
                id = A_LOCAL;
            }
            else if (i == 1) {
                id = A_AI;
            }
            else if (i == 2) {
                id = A_ONLINE;
            }
            else {
                id = A_BACK;
            }
        }
        const wchar_t* levels[]= {
            L"\u7b80\u5355",L"\u4e00\u822c",L"\u56f0\u96be",L"\u8fd4\u56de\u4e3b\u754c\u9762"
        };
        // 简单；一般；困难；返回主界面；难度菜单的四个按钮文字
        if(page==DIFFICULTY_PAGE) {
            if(i<3) {
                id=A_EASY+i;
            } else {
                id=A_BACK;
            }
        }      // 难度菜单前三个是难度，第四个是返回
        const wchar_t* label;
        if (page == MAIN_MENU) {
            label = MAIN_LABELS[i];
        }
        else if (page == DIFFICULTY_PAGE) {
            label = levels[i];
        }
        else {
            label = MODE_LABELS[i];
        }
        addButton(id,687,ys[i],441,95,label,id!=0,true);
        // 文字随页面变化：难度菜单 / 主菜单 / 模式菜单
    }
}
// 仅存档/复盘列表：最多八行记录，以及删除确认。
void drawRecordListPage() {
    putimage(0,0,background);
    // 列表页也要铺背景
    setfillcolor(EGERGB(246,241,224));
    solidroundrect(380,100,1420,1250,28);
    // 一个大的米色面板
    text(560,150,page==SAVE_LIST?L"\u7ee7\u7eed\u6e38\u620f \u00b7 \u672a\u5b8c\u6210\u68cb\u5c40":L"\u590d\u76d8 \u00b7 \u68cb\u5c40\u8bb0\u5f55",44);
    // 继续游戏 · 未完成棋局；复盘 · 已完成棋局；标题区分「继续游戏」和「复盘」
    text(510,220,L"\u68cb\u5c40\u540d\u79f0",28);
    text(1080,220,L"\u6a21\u5f0f",28);
    text(1250,220,L"\u64cd\u4f5c",28);
    // 棋局名称；模式；操作；三个列表表头
    for(int i=0;i<8 && listOffset+i<static_cast<int>(entries.size());++i) {
        int at=listOffset+i;
        const SavedEntry& entry=entries[at];
        // at是entries内真实下标，i才是当前页自上而下第几行。
        std::wstring label;
        if (entry.name.size()>18) {
            label = entry.name.substr(0,18)+L"\u2026";
        } else {
            label = entry.name;
        }
        // 名称超过18个宽字符代码单元时截断并加省略号。
        addButton(1000+at*2,410,290+i*95,640,80,label,dialog==NO_DIALOG);
        text(1080,316+i*95,entry.mode=="LOCAL_TWO"?L"\u5355\u673a":entry.mode=="HUMAN_AI"?L"\u4eba\u673a":     // 单机；人机；模式列
        entry.mode=="ONLINE"?L"\u8054\u7f51":L"\u635f\u574f",30);
        if(page==REPLAY_LIST) {
            text(1080,351+i*95,entry.result==1?L"\u9ed1\u80dc":entry.result==2?L"\u767d\u80dc":entry.result==3?L"\u5e73\u5c40":L"\u672a\u5b8c",22);
        }     // 联网；损坏；复盘列表额外显示胜负结果
        addButton(1001+at*2,1200,290+i*95,180,80,L"\u5220\u9664",dialog==NO_DIALOG);
        // 删除
    }
    if(entries.empty()) {
        text(730,500,L"\u6682\u65e0\u5b58\u6863",48);
    }     // 暂无存档；一条都没有时显示提示
    text(530,1070,L"\u53ef\u6eda\u52a8\u9f20\u6807\u6eda\u8f6e\u67e5\u770b\u5176\u4ed6\u68cb\u5c40",26);
    // 可滚动鼠标滚轮查看其他棋局；操作说明
    addButton(A_BACK,740,1140,320,80,L"\u9000\u51fa",dialog==NO_DIALOG);
    // 退出；返回
    if(dialog==DELETE_CONFIRM) {
        buttons.clear();
        setfillcolor(EGERGB(249,232,200));
        solidroundrect(450,430,1350,840,30);
        // 清除底层按钮，仅保留删除确认按钮，并绘制实心确认面板。
        text(650,475,L"\u662f\u5426\u6c38\u4e45\u5220\u9664\uff1f",52);
        // 是否永久删除？；询问文字
        std::wstring name=entries[deleteIndex].name;
        // 显示要删除的棋局名
        text(510,570,name.size()>24?name.substr(0,24)+L"\u2026":name,30);
        // …；名字太长同样截断
        text(560,630,L"\u5b58\u6863\u6587\u4ef6\u5c06\u88ab\u5220\u9664\uff0c\u65e0\u6cd5\u64a4\u9500\u3002",32);
        // 存档文件将被删除，无法撤销。；强调不可撤销
        addButton(A_YES,570,720,260,80,L"\u662f\uff0c\u6c38\u4e45\u5220\u9664");
        addButton(A_NO,970,720,260,80,L"\u5426");
        // 是，永久删除；否；是，永久删除 / 否
    }
}
// 公共临时提示：时间未过期才显示 notice，不修改对局。
void drawNotice() {
    if(GetTickCount64()<noticeUntil) {
        // 提示画在页面下方或右侧，避开棋盘
        int left=(page==GAME_PAGE || page==REPLAY_PAGE)?1235:520;
        // 提示的位置随页面不同而不同
        int top=(page==SAVE_LIST || page==REPLAY_LIST)?20:1100;
        // 在列表页显示在顶部，其他页面显示在下方
        setfillcolor(EGERGB(240,234,208));
        solidroundrect(left,top,left+520,top+100,20);
        // 提示底板
        text(left+20,top+32,notice,26);
        // 提示文字
    }
}
// 总绘图入口：先清空旧按钮，画当前页面，再统一画按钮和临时提示。
void render(ULONGLONG startedAt) {
    buttons.clear();
    if (page == MAIN_MENU || page == MODE_MENU || page == DIFFICULTY_PAGE) {
        drawMenuPage(startedAt);
    } else if (page == NETWORK_PAGE) {
        drawOnlinePage();
    } else if (page == GAME_PAGE || page == REPLAY_PAGE) {
        drawGame();
    } else {
        drawRecordListPage();
    }
    int x = -1;
    int y = -1;
    mousepos(&x, &y);
    drawButtons(x, y);
    drawNotice();
}
#endif
