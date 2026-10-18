// 【本文件阅读路线】输入主线：initializeProgram 初始化 -> processMouseMessages 处理输入 -> releaseProgramResources 清理。
// 输入与窗口区：接收系统消息、鼠标输入，再调用业务动作或绘图。
#ifndef GOMOKU_GAME_INPUT_H
#define GOMOKU_GAME_INPUT_H
#include "game_draw.h"
volatile LONG windowCloseRequested=0;
WNDPROC originalWindowProc=NULL;
bool windowCreated=false;
LRESULT CALLBACK gameWindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE) {
        InterlockedExchange(&windowCloseRequested,1);
        return 0;
    }      // 收到关闭消息时只记个标志，先不真的退出
    return CallWindowProcW(originalWindowProc,hwnd,msg,wp,lp);
    // 其它消息交回给原来的处理函数，保证窗口功能正常
}
PIMAGE loadImage(const char* path) {
    PIMAGE im=newimage();
    // 创建一个新的图像对象
    if(!im || getimage(im,path)!=0) {
        if(im) {
            delimage(im);
        } return NULL;
    }      // getimage 从文件读入图片
    return im;
}
// 启动资源放在这里，main只看是否准备成功。
// 所有模式：把工作目录设为 exe 所在目录，资源相对路径才可靠。
bool setProgramDirectory() {
    wchar_t path[32768];
    DWORD n=GetModuleFileNameW(NULL,path,32768);
    // 取得本程序 exe 的完整路径
    if(!n || n>=32768) {
        return false;
    }      // 路径异常就直接失败退出
    wchar_t* slash=std::wcsrchr(path,L'\\');
    if(!slash) {
        return false;
    }      // 找到最后一个反斜杠，把文件名部分截掉，剩下目录
    *slash=0;
    if(!SetCurrentDirectoryW(path)) {
        return false;
    }      // 把工作目录切到 exe 所在目录，后续的相对路径就都对了
    return true;
}
// 所有模式：设置缩放、创建图形窗口并设置手动绘制。
void createGameWindow() {
    typedef BOOL (WINAPI *DpiFunction)();
    DpiFunction dpi=reinterpret_cast<DpiFunction>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDPIAware"));
    // 取得系统函数地址；老系统上可能没有，所以要先判断
    if(dpi) {
        dpi();
    }      // 有就调用它
    setinitmode(INIT_ANIMATION);
    initgraph(1800,1350);
    windowCreated=true;
    setcaption(L"\u4e94\u5b50\u68cb");
    // 五子棋
    setrendermode(RENDER_MANUAL);
    SetWindowLongPtrW(getHWnd(),GWL_STYLE,GetWindowLongPtrW(getHWnd(),GWL_STYLE)|WS_CLIPCHILDREN);
}
// 所有模式：读取棋盘/棋子/按钮图片和九十帧菜单背景；失败返回 false。
bool loadGameImages() {
    background=loadImage("assets/backgrounds/bg_a.png");
    // 背景图
    boardImage=loadImage("assets/board/board.png");
    // 棋盘图
    blackImage=loadImage("assets/pieces/black.png");
    whiteImage=loadImage("assets/pieces/white.png");
    // 黑白棋子
    buttonImage=loadImage("assets/ui/button_320x80.png");
    // 按钮底板素材
    bool loaded=background && boardImage && blackImage && whiteImage && buttonImage;
    // loaded 为假表示上面有任何一张图没加载成功
    for(int i=0;i<90 && loaded && is_run();++i) {
        char file[80];
        std::sprintf(file,"assets/menu/frames/frame_%03d.png",i);
        // 90 帧的文件名是 frame_000.png 到 frame_089.png
        frames[i]=loadImage(file);
        loaded=frames[i]!=NULL;
        // 任何一帧失败就整体判为失败
        if(i==0 && loaded) {
            putimage(0,0,frames[0]);
        }
        delay_ms(1);
        // 每帧之间稍微歇一下，避免启动时界面完全没反应
    }
    return loaded;
}
// 创建隐藏的命名框及联网地址/端口框；相应流程再显示它们。
bool createInputControls() {
    editFont=CreateFontW(-32,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"\u6977\u4f53");
    // 楷体；创建楷体字体，负高度表示按字号大小算
    nameEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
    1260,710,460,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);
    // 创建子编辑框；字体在后续WM_SETFONT消息中设置。
    if(nameEdit) {
        SendMessageW(nameEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        SendMessageW(nameEdit,EM_SETLIMITTEXT,24,0);
        // 编辑控件限制24个UTF-16代码单元，不是所有Unicode字符都严格按24个计。
    }
    bool loaded = nameEdit != NULL;
    // 素材或输入框有一个没成功，整体就算失败
    ipEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
    720,420,600,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);
    // 创建IPv4地址输入框；后面的EM_SETLIMITTEXT才设置15字符上限。
    portEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"5000",WS_CHILD|ES_AUTOHSCROLL|ES_NUMBER,      // 端口框默认填 5000
    720,590,300,60,getHWnd(),NULL,GetModuleHandleW(NULL),NULL);
    if(ipEdit) {
        SendMessageW(ipEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        SendMessageW(ipEdit,EM_SETLIMITTEXT,15,0);
    }      // 同样把字体设上，并限制长度
    if(portEdit) {
        SendMessageW(portEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        SendMessageW(portEdit,EM_SETLIMITTEXT,5,0);
    }
    loaded=loaded && ipEdit && portEdit;
    // 三个控件都创建成功才算通过
    return loaded;
}
// 初始化入口：目录 -> 窗口 -> 图片 -> 输入框 -> 关闭回调和存档目录。
bool initializeProgram() {
    if (!setProgramDirectory()) {
        return false;
    }
    createGameWindow();
    bool imagesLoaded = loadGameImages();
    bool controlsCreated = createInputControls();
    originalWindowProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(getHWnd(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(gameWindowProc)));
    recordDirectories();
    // 确保存档需要的目录都存在
    return imagesLoaded && controlsCreated;
}
void processWindowMessages() {
    MSG message;
    // message保存Windows窗口消息；取消息时的PM_REMOVE会将它从队列移除。
    while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT) {
            running=false;
        }      // 收到退出消息就结束主循环
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
void handleWindowCloseRequest() {
    if(InterlockedExchange(&windowCloseRequested,0)==0) {
        return;
    }
    if(page==GAME_PAGE && gameMode=="ONLINE") {
        if(archiveStage==0) {
            // 联网对局中途关窗口，先走联网的退出与保存询问
            if(netState.phase==N_PLAY && !netPending) {
                closeAfterOnline=true;
                onlineAsk(2);
            }
            else {
                toast(L"\u8bf7\u5148\u5b8c\u6210\u5f53\u524d\u8be2\u95ee\uff0c\u518d\u9000\u51fa");
            }      // 还有询问没处理完，先提示玩家，不要强行退出
        } else {
            closeAfterOnline=true;
            if(archiveStage>=3) {
                onlineRequestHome();
            }
        }
    } else if(page==NETWORK_PAGE && netStage==4) {
        closeAfterOnline=true;
        onlineLobbyLeave();
    }      // 在联网等待室里关窗口，走等待室离开流程
    else {
        running=false;
    }      // 其余页面直接退出程序
}
// 仅列表页：滚轮改变第一条显示记录的位置。
void scrollRecordList(const mouse_msg& m) {
    if (m.wheel < 0) {
        ++listOffset;
    } else {
        --listOffset;
    }
    limitListOffset();
}
// 单机和人机：立即检查并落子，结束后询问是否保存。
void placeLocalStone(int cell) {
    bool placed = game.place(cell / 15, cell % 15);
    if (placed && game.result != 0) {
        dialog = FINISHED_SAVE;
    }
}
// 按下只记住目标；此时不执行按钮，也不落子。
void rememberMouseDown(const mouse_msg& m) {
    pressed = hitButton(m.x, m.y);
    pressedCell = -1;
    if (humanCanMove()) {
        pressedCell = boardCell(m.x, m.y);
    }
}
// 松开：仍是同一个按钮/交点才执行，防止拖动造成误操作。
void finishMouseClick(const mouse_msg& m) {
    int hit = hitButton(m.x, m.y);
    if (hit && hit == pressed) {
        act(hit);
    } else if (!pressed && pressedCell >= 0 && humanCanMove()) {
        int cell = boardCell(m.x, m.y);
        if (cell == pressedCell) {
            if (gameMode == "ONLINE") {
                onlineMove(cell);
                // 联网只发送请求，服务器确认后更新棋盘。
            } else {
                placeLocalStone(cell);
                // 单机/人机立即落子。
            }
        }
    }
    pressed = 0;
    pressedCell = -1;
}
// 输入入口：列表滚轮、左键按下、左键松开三种情况。
void processMouseMessages(ULONGLONG started) {
    while (mousemsg()) {
        mouse_msg m = getmouse();
        if (m.is_wheel() && dialog == NO_DIALOG && (page == SAVE_LIST || page == REPLAY_LIST)) {
            scrollRecordList(m);
            continue;
        }
        if (!m.is_left()) {
            continue;
        }
        if (m.is_down()) {
            rememberMouseDown(m);
        }
        if (m.is_up()) {
            finishMouseClick(m);
            if (running && is_run()) {
                render(started);
            }
        }
    }
}
void releaseProgramResources() {
    if(!windowCreated) {
        return;
    }
    onlineStop();
    aiWorker.shutdown();
    // 停掉 AI 后台线程，等它真正结束
    if(originalWindowProc && is_run()) {
        SetWindowLongPtrW(getHWnd(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalWindowProc));
    }      // 把窗口处理函数换回原来的
    if(ipEdit) {
        DestroyWindow(ipEdit);
    }      // 销毁输入框控件
    if(portEdit) {
        DestroyWindow(portEdit);
    }
    if(nameEdit) {
        DestroyWindow(nameEdit);
    }
    if(editFont) {
        DeleteObject(editFont);
    }      // 释放字体对象
    for(int i=0;i<90;++i) {
        if(frames[i]) {
            delimage(frames[i]);
        }
    }      // 释放 90 帧动画占用的内存
    PIMAGE images[]= {
        background,boardImage,blackImage,whiteImage,buttonImage
    };
    // 释放其余素材
    for(int i=0;i<5;++i) {
        if(images[i]) {
            delimage(images[i]);
        }
    }
    closegraph();
    // 关闭图形窗口
}
#endif
