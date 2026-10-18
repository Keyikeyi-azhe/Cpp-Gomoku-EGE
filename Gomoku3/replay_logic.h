// 【建议阅读 05 / 复盘】replay_logic.h
// 先看成员 cursor/board/moves，再读 open -> seek -> play/pause -> tick -> speed。
// 本对象复制原始历史，不修改原存档；最终结果可能来自认输，不能只靠棋盘重新判胜。
// 下一站：ai_engine.h；先了解 AI 在副本上试子，再看递归。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
#ifndef GOMOKU_REPLAY_LOGIC_H
#define GOMOKU_REPLAY_LOGIC_H
#include <windows.h>
#include "game_logic.h"
// 【类型 ReplayPlayer】复盘播放器。moves是完整历史副本，board是前cursor手对应的展示盘；计时只决定
// 何时推进，不修改原棋局。原始结果放在finalResult，到末尾才恢复，以支持认输等无五连结束。
struct ReplayPlayer {
    // 类型定义：game_logic.h:16 (Move)。
    std::vector<Move> moves;   // 这一局完整的落子历史副本
    // 类型定义：game_logic.h:20 (LocalGame)。
    LocalGame board;   // 当前播放到第几步时，应该显示出来的棋盘
    size_t cursor;   // 已经播放的手数0..moves.size，而非最后一手的下标。
    bool started, playing;   // started:是否点过播放；playing:此刻是否正在自动前进
    int interval, finalResult;   // interval每手间隔毫秒；finalResult保存源记录结果，到播放末尾再恢复。
    ULONGLONG deadline, remaining;   // deadline:下一次自动落子的时刻；remaining:暂停时还差多少毫秒
    // 【函数 ReplayPlayer::ReplayPlayer】初始化暂停的空播放器。
    // 执行顺序：cursor=0，started/playing=false，interval/remaining=3000，finalResult/deadline=0；
    // board 自动初始化。
    // 类型定义：replay_logic.h:15 (ReplayPlayer)。
    ReplayPlayer():cursor(0),started(false),playing(false),interval(3000),finalResult(0),deadline(0),remaining(3000) {}
    // 【函数 ReplayPlayer::open】从 source 取历史，准备从空盘观看。
    // 执行顺序：复制结果和历史；复位游标/播放标志/速度；清空展示盘；若历史为空，立刻显示 finalResult。
    // 参数/局部变量：source 是只读棋局引用。
    // 边界与易错点：零步认输也有终局结果，所以不能要求“至少落一子才显示结果”。
    // 从上层理解：main.cpp:289 (loadEntry)。
    // 类型定义：game_logic.h:20 (LocalGame)。
    void open(const LocalGame& source) {
        // 首次调用定位：game_logic.h:39 (LocalGame::reset)。
        finalResult=source.result; moves=source.moves;cursor=0;started=false;playing=false;interval=3000;remaining=3000;board.reset();if(moves.empty()) board.result=finalResult;
    }
    // 【函数 ReplayPlayer::seek】跳到 position 手，超过末尾则夹到总手数。
    // 执行顺序：清空 board，再重放前 cursor 手；到末尾才恢复 finalResult；remaining 设为 interval。
    // 参数/局部变量：position 是目标手数；i 为重放下标。
    // 边界与易错点：重放最多 225 手，简单可靠；不会删除 source 的历史。seek 自己不改 playing/deadline，
    // 上层只在暂停单步，tick 则会重新设 deadline。
    // 从上层理解：replay_logic.h:87 (ReplayPlayer::tick)；main.cpp:331 (act)。
    void seek(size_t position) {
        cursor=position>moves.size()?moves.size():position;   // ?: 是「三目运算符」：条件为真取问号后的值，为假取冒号后的值；这里是防止 position 超出范围
        // 首次调用定位：game_logic.h:39 (LocalGame::reset)；game_logic.h:79 (LocalGame::place)。
        board.reset();for(size_t i=0;i<cursor;++i) board.place(moves[i].row,moves[i].col);   // 以空盘重放前cursor手；最多225手，易于保持历史与盘面一致。
        if(cursor==moves.size()) board.result=finalResult;   // 如果已经播到最后一手，就把终局结果也放上棋盘，让界面显示出胜负
        remaining=interval;   // 倒计时复位，下一步仍要等满一个间隔
    }
    // 【函数 ReplayPlayer::pause】暂停并保留还需等待的时间。
    // 执行顺序：若正在播，remaining=max(deadline-now,0)；playing=false。
    // 参数/局部变量：now 为调用者传入的当前毫秒。
    // 从上层理解：replay_logic.h:74 (ReplayPlayer::speed)；main.cpp:331 (act)。
    void pause(ULONGLONG now) {
        if(playing) remaining=deadline>now?deadline-now:0;   // 只在「正在播放」时才计算剩余时间，避免把已暂停的状态算错
        playing=false;   // 置为未播放状态
    }
    // 【函数 ReplayPlayer::play】从剩余倒计时开始/继续自动播放。
    // 执行顺序：已到末尾就返回；否则 started/playing=true；deadline=now+remaining。
    // 参数/局部变量：now 为当前毫秒。
    // 从上层理解：replay_logic.h:74 (ReplayPlayer::speed)；main.cpp:331 (act)。
    void play(ULONGLONG now) {
        if(cursor>=moves.size()) return;   // 已经到末尾了，没有可播的内容，直接返回
        started=true;playing=true;deadline=now+remaining;   // 标记为「已开始播放」并进入播放状态；把下一次落子定在「现在 + 剩余时间」之后
    }
    // 【函数 ReplayPlayer::speed】改变一手间隔 milliseconds。
    // 执行顺序：记住 active；pause；把 remaining 按新旧 interval 比例缩放；写新 interval；原先在播放就
    // 恢复。
    // 参数/局部变量：active 保存原 playing；now 当前毫秒。
    // 边界与易错点：调用者只传 3000 或 1500；函数本身没有检查零或负间隔。
    // 从上层理解：main.cpp:331 (act)。
    void speed(int milliseconds,ULONGLONG now) {
        // 首次调用定位：replay_logic.h:56 (ReplayPlayer::pause)。
        bool active=playing;pause(now);   // 先记住原状态并暂停，改完速度后再决定要不要恢复播放
        remaining=remaining*milliseconds/interval;interval=milliseconds;   // 按剩余时间比例换算；调用方保证milliseconds>0、原interval>0。
        // 首次调用定位：replay_logic.h:64 (ReplayPlayer::play)。
        if(active) play(now);   // 之前正在播放的话，用新速度接着播
    }
    // 【函数 ReplayPlayer::tick】主循环每帧调用，按时自动前进一步。
    // 执行顺序：仅在 playing 且 now>=deadline 且未到末尾时 seek(cursor+1)；下次 deadline=now+interval；
    // 到末尾停止。
    // 参数/局部变量：now 当前毫秒。
    // 边界与易错点：每次 tick 最多一手；卡顿后不会 while 循环补播所有错过的手。
    // 从上层理解：main.cpp:669 (main)。
    void tick(ULONGLONG now) {
        if(playing && now>=deadline && cursor<moves.size()) {
            // 首次调用定位：replay_logic.h:45 (ReplayPlayer::seek)。
            seek(cursor+1);deadline=now+interval;   // 一次tick最多推进一手；即使本帧迟到，也不会连续补播所有错过的手。
            if(cursor==moves.size()) playing=false;   // 播到最后一手了，自动停止播放
        }
    }
};
#endif
