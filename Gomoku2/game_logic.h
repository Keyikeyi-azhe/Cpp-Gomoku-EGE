// 【本文件阅读路线】所有模式共用的规则层：reset 清盘 -> place 落子 -> wins 判胜 -> undo 悔棋。没有绘图或网络收发。
// 【建议阅读 01 / 棋盘规则】game_logic.h
// 从 Move、LocalGame 开始，再读 reset -> place -> wins -> undo。
// 这是单机、人机、复盘和网络共用的规则层；不负责图形、文件或线程。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一一次编译处理的源文件及其包含的头文件重复定义。
#ifndef GOMOKU_GAME_LOGIC_H
#define GOMOKU_GAME_LOGIC_H
#include <vector>
#include <cstring>
// 【类型 Move】一手落子的记录，不是棋盘。row/col是0..14行列，stone是1黑或2白；按时间顺序存入
// LocalGame::moves。
struct Move {
    int row, col, stone;
};
// 【类型 LocalGame】一盘棋的当前状态与规则操作。cells便于按格查询，moves保留落子顺序；正式落子/悔
// 棋同时维护二者。turn是执棋方，不是步数；result是结束结果，不是布尔胜负。
struct LocalGame {
    int cells[15][15];
    // 当前盘面：cells[row][col]，row向下、col向右；0空、1黑、2白。
    int turn, result;
    // turn=1/2；result=0未完、1黑胜、2白胜、3平局。终局分支不翻转turn。
    std::vector<Move> moves;
    // 落子历史，按先后顺序记下每一手；悔棋和复盘都要靠它
    // 【函数 LocalGame::LocalGame】默认构造空棋局。
    // 处理步骤：直接调用 reset；所有需要空棋盘的地方复用同一初始化逻辑。
    LocalGame() {
        reset();
    }      // 构造函数：这个对象一被创建就自动执行一次，把棋盘摆成开局状态
    // 【函数 LocalGame::reset】无参数、无返回值；恢复空棋盘。
    // 处理步骤：两层for循环把cells的每格置零；清空 moves；turn=1，result=0。
    // row和col分别遍历15行、15列。
    // 注意：只清空 cells 而不清 moves，会使屏幕绘制、手数与存档互相矛盾。
    void reset() {
        for (int row = 0; row < 15; ++row) {
            for (int col = 0; col < 15; ++col) {
                cells[row][col] = 0;
            }
        }     // 两层循环依次清空15×15个格子
        moves.clear();
        turn = 1;
        result = 0;
        // 清空历史；轮到黑棋(1)先下；result=0 表示对局尚未结束
    }
    // 【函数 LocalGame::wins】输入最后落点 row、col；返回这一点所在的线上是否有至少五颗同色子。
    // 处理步骤：先取当前格颜色；空格返回 false；对四个方向逐一从中心向两端走，遇到边界、空位或异色停止；
    // 累计>=5 返回 true。
    // 变量含义：directions[4][2] 是方向增量；d 是方向下标；stone 是本格颜色；count 从 1 算自身；
    // sign=-1/+1 是两端；r、c 是扫描中的坐标。
    // 注意：调用者必须先保证row/col在0..14；本函数入口不检查越界。方向(1,0)竖直、(0,1)水平；
    // >=5允许长连，无禁手规则。
    bool wins(int row, int col) const {
        const int directions[4][2] = {
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
        // 每项是(行增量,列增量)：竖直、水平、右下斜线、左下斜线；sign同时扫描反向。
        int stone = cells[row][col];
        // 取出这一格的颜色，后面用它判断同色
        if (!stone) {
            return false;
        }         // 这一格是空的，当然谈不上赢
        for (int d = 0; d < 4; ++d) {
            // 依次检查四个方向
            int count = 1;
            // 先算上刚落的这一子自己，所以从 1 开始数
            for (int sign = -1; sign <= 1; sign += 2) {
                int r = row + sign * directions[d][0];
                // 从紧邻的那一格开始
                int c = col + sign * directions[d][1];
                while (r >= 0 && r < 15 && c >= 0 && c < 15 && cells[r][c] == stone) {
                    ++count;
                    r += sign * directions[d][0];
                    c += sign * directions[d][1];
                    // 同色棋子数加一，坐标前进一步
                }
            }
            if (count >= 5) {
                return true;
            }     // 这条线上同色棋子达到 5 颗及以上，判定获胜
        }
        return false;
        // 四个方向都没凑够，本子不构成胜负
    }
    // 【函数 LocalGame::place】输入 row、col；true 表示落子成功，false 表示状态没有被正常落子流程更新。
    // 处理步骤：拒绝已结束、越界、占用；写 cells；构造 move 并追加历史；先判胜，再判满盘，最后才交换
    // turn。
    // 变量含义：move 是当前这一步 {row,col,turn}；225=15×15。
    // 注意：胜利与满盘同时发生时，先判胜。wins 不会改 result，place 才负责落地终局状态。
    bool place(int row, int col) {
        if (result || row < 0 || row >= 15 || col < 0 || col >= 15 || cells[row][col]) {
            return false;
        }     // ||从左到右短路；越界时不会继续访问cells[row][col]。
        cells[row][col] = turn;
        // 把当前该下的人的颜色写进棋盘
        Move move;
        move.row=row;
        move.col=col;
        move.stone=turn;
        moves.push_back(move);
        // 先把本手颜色存入历史；此时turn尚未切换。Move类型见上方定义。
        if (wins(row, col)) {
            result = turn;
        }            // 这手凑成五连，落子方获胜
        else if (moves.size() == 225) {
            result = 3;
        }     // 否则如果 225 格全下满还没人赢，判平局(3)
        else {
            if(turn==1) {
                turn=2;
            } else {
                turn=1;
            }
        }                         // 都没发生，就把下子权交给对方：1 变 2，2 变 1
        // 语句说明：true只表示成功落子；是否结束需另外检查result。
        return true;
    }
    // 【函数 LocalGame::undo】撤掉最后一手；终局或无历史时返回 false。
    // 处理步骤：复制 moves.back；pop_back 删除历史；将原落点置 0；turn 恢复为这手的颜色。
    // 变量含义：move 是被撤回的一手。
    // 注意：不支持撤销已经结束的棋局。AI 回合悔棋由上层多次调用这里实现。
    bool undo() {
        if (result || moves.empty()) {
            return false;
        }     // empty() 判断历史是否为空
        Move move = moves.back();
        moves.pop_back();
        // back() 取出最后一手，pop_back() 把它从历史中删掉
        cells[move.row][move.col] = 0;
        turn = move.stone;
        // 清格并恢复到被撤销一手的执棋方；终局在入口已拒绝，故此处不需重算result。
        return true;
    }
};
#endif
