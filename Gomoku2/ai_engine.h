// 【本文件阅读路线】仅人机底层：AIWorker 管理计算线程，AIEngine 评分和搜索。界面入口是 game_actions.h 的 updateAI。
// 【建议阅读 06 / AI 与工作线程】ai_engine.h
// AIPoint/AIEntry -> AISearch::choose -> candidates/pointScore -> search -> put。
// 最后看 AIWorker，再回 main.cpp 的 cancelAI/updateAI；区分完成搜索与允许采用答案。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一一次编译处理的源文件及其包含的头文件重复定义。
#ifndef GOMOKU_AI_ENGINE_H
#define GOMOKU_AI_ENGINE_H
#include <windows.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdlib>
#include "game_logic.h"
// 【类型 AIPoint】AI候选落点。row/col为0..14，非法答案常用-1；score是启发排序分，choose返回时未必
// 是最终递归分数。
struct AIPoint {
    int row,col;
    long long score;
};
inline bool higherPointScoreFirst(const AIPoint& a, const AIPoint& b) {
    return a.score > b.score;
}
// 【类型 AIEntry】困难档已计算局面的记录表的一条缓存。key为含轮次的局面键；score为缓存值；depth为搜索深度；
// flag=0精确值/1下界/2上界；point为row*15+col最佳点。
struct AIEntry {
    unsigned long long key;
    long long score;
    int depth,flag,point;
    // key局面键；score搜索结论；depth已搜索深度；flag=0精确/1下界/2上界；point压平落点。
    // 【函数 AIEntry::AIEntry】初始化一个未使用的已计算局面的记录表槽。
    // 处理步骤：key/score/flag=0，depth=-1，point=-1。
    // 变量含义：depth=-1 使默认条目不能满足正常深度命中条件。
    AIEntry():key(0),score(0),depth(-1),flag(0),point(-1) {
    }
};
// 【类型 AISearch】一次AI思考的搜索器，持有独立LocalGame副本。public以前的成员默认private；choose
// 是选点入口，completedDepth供观察完整完成的深度。它不调用图形API，也不直接修改客户端全局game。
class AISearch {
    LocalGame board;
    // 自己那份棋盘副本，在上面随便试子不会影响真正的对局
    int level,limit;
    // level 难度 0-2；limit 是普通候选点最多保留多少个
    volatile LONG* cancel;
    // 指向有效取消标志的地址；通过Interlocked操作访问，volatile本身不是完整同步机制。
    ULONGLONG deadline;
    // 本次思考的截止时刻(毫秒)
    bool interrupted;
    // 一旦被打断就置真，后面所有循环看到它都尽快退出
    unsigned int randomState;
    // 由计算公式生成的随机数的当前状态
    unsigned long long hash,zobrist[15][15][3];
    // hash 是当前局面的识别码；zobrist 是每个「格子和颜色」对应的随机数
    std::vector<AIEntry> table;
    // 已计算局面的记录表本体，只有困难档才启用
    long long history[2][225];
    // 颜色下标stone-1；落点下标r*15+c；发生跳过不值得继续计算的路线时累加depth*depth，不是简单次数。
    static const long long WIN=1000000000000LL;
    // 检查取消标志和时间：用户取消或超过时间预算，就把 interrupted 设为 true。
    // cancel 指向取消标志；deadline 是截止时间。InterlockedCompareExchange 用来安全读取其他线程可能修改的标志。
    //
    //
    //
    //
    bool stopped() {
        if(InterlockedCompareExchange(cancel,0,0) || GetTickCount64()>=deadline) {
            interrupted=true;
        }      // 任一条件成立就标记为已打断
        return interrupted;
    }
    // 按固定公式更新 randomState，得到一个随机数；简单难度用它从几个接近的好位置中选一个。
    //
    //
    //
    //
    unsigned int random() {
        randomState^=randomState<<13;
        randomState^=randomState>>17;
        randomState^=randomState<<5;
        // 三次移位异或，是 xorshift 的核心步骤
        return randomState;
    }
    // 在 AI 自己的棋盘副本上试着放棋子；stone=0 表示撤掉。真实游戏棋盘不会改变。
    // 同时更新 hash（棋盘识别码），方便困难难度查找算过的局面；不修改 turn、moves、result。
    //
    //
    //
    void put(int r,int c,int stone) {
        int old=board.cells[r][c];
        // 先看看这格原来是什么子
        if(old) {
            hash^=zobrist[r][c][old];
        }      // 如果有子，先把它的随机数异或掉，相当于抹掉这一格的影响
        board.cells[r][c]=stone;
        // 放入新子
        if(stone) {
            hash^=zobrist[r][c][stone];
        }      // 再异或新子的随机数，加上它的影响；异或两次等于抵消，所以撤子同理
    }
    // 假设在 (r,c) 放 stone 色棋子，检查横、竖、两条斜线，每条线看前后各5格。
    // X 表示同色棋子，. 表示空格，# 表示对方或边界。每条线取匹配到的最强棋形分，再把4条线相加。
    // 五连1亿、活四100万、冲四10万、活三1万、眠三1000、活二100、普通二连10；这是经验分，不是胜率。
    //
    //
    //
    //
    long long pointScore(int r,int c,int stone) {
        static const int dir[4][2]= {
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
        // (行增量,列增量)：竖直、水平、右下斜线、左下斜线；负k负责另一端。
        long long sum=0;
        // 四条线的分数累加在这里
        for(int d=0;d<4;++d) {
            // 逐一处理四个方向
            std::string line;
            // 这一条线的局部字符串
            for(int k=-5;k<=5;++k) {
                // 从中心第 -5 格取到第 +5 格，共 11 格
                int x=r+k*dir[d][0],y=c+k*dir[d][1];
                // 把相对偏移 k 换算成棋盘上的绝对坐标
                if(!k) {
                    line+='X';
                }      // k 等于 0 就是中心点，也就是这一手的落点，记作 X
                else if(x<0 || x>=15 || y<0 || y>=15) {
                    line+='#';
                }      // 越出棋盘边界的格子记作井号，表示「此路不通」
                else {
                    line+=board.cells[x][y]==stone?'X':board.cells[x][y]==0?'.':'#';
                }      // 否则看那一格：是自己就记 X，是空就记点号，是对方就记井号
            }
            // 【阅读提示】每条11格线按从强到弱的else-if选一个分档，再将四方向相加。
            // 【阅读提示】find在整段内匹配，未强制棋型经过中心；因此这是按棋形和经验打分的评分，不是严格棋型证明。
            long long value=0;
            if(line.find("XXXXX")!=std::string::npos) {
                value=100000000;
            }
            else if(line.find(".XXXX.")!=std::string::npos) {
                value=1000000;
            }
            else if(line.find("XXXX.")!=std::string::npos || line.find(".XXXX")!=std::string::npos ||
            line.find("XXX.X")!=std::string::npos || line.find("XX.XX")!=std::string::npos ||
            line.find("X.XXX")!=std::string::npos) {
                value=100000;
            }
            else if(line.find(".XXX.")!=std::string::npos || line.find(".XX.X.")!=std::string::npos ||
            line.find(".X.XX.")!=std::string::npos) {
                value=10000;
            }
            else if(line.find("XXX.")!=std::string::npos || line.find(".XXX")!=std::string::npos ||
            line.find("XX.X")!=std::string::npos || line.find("X.XX")!=std::string::npos) {
                value=1000;
            }
            else if(line.find(".XX.")!=std::string::npos || line.find(".X.X.")!=std::string::npos) {
                value=100;
            }
            else if(line.find("XX.")!=std::string::npos || line.find(".XX")!=std::string::npos) {
                value=10;
            }
            sum+=value;
            // 把这条线的得分累加起来
        }
        return sum;
        // 四条线的总分就是这一手的评分
    }
    // 选出当前值得试下的位置：能直接赢的位置优先；否则优先堵住对方下一手能赢的位置。
    // 普通位置必须在已有棋子周围2格以内，按 自己棋形分*10+对方棋形分*11 排序。
    // 普通候选点简单/中等最多12个，困难最多16个；直接赢和必须堵的位置不受这个数量限制。空棋盘选中心(7,7)。
    // preferred 是以前计算推荐的位置编号，-1表示没有推荐；困难还用 history 调整试下顺序。
    //
    //
    //
    std::vector<AIPoint> candidates(int stone,int preferred=-1) {
        std::vector<AIPoint> wins,blocks,normal;
        // 分三类收集：能直接赢的点、必须挡的点、普通候选点
        bool occupied=false;
        // occupied 用来判断棋盘上是否已经有子
        for(int r=0;r<15;++r) {
            for(int c=0;c<15;++c) {
                if(board.cells[r][c]) {
                    occupied=true;
                }
            }
        }
        if(!occupied) {
            AIPoint center= {
                7,7,0
            };
            normal.push_back(center);
            return normal;
        }      // 空棋盘的第一手：没有参考，就直接下在正中央(7,7)
        for(int r=0;r<15;++r) {
            if(stopped()) {
                return normal;
            }
            for(int c=0;c<15;++c) {
                if(!board.cells[r][c]) {
                    // 只考察空位
                    put(r,c,stone);
                    bool win=board.wins(r,c);
                    put(r,c,3-stone);
                    // 先试自己能否赢，再把同一位置换成对方颜色；put同步更新hash。
                    bool block=board.wins(r,c);
                    put(r,c,0);
                    // 测试对方在该点能否赢，随后立刻撤回到空格。
                    AIPoint p= {
                        r,c,0
                    };
                    // 本候选初始score=0；普通候选稍后打分，wins/blocks保留此初值。
                    if(win) {
                        wins.push_back(p);
                    }      // 能直接赢的点单独收集
                    if(block) {
                        blocks.push_back(p);
                    }      // 必须防的点单独收集
                    bool nearby=false;
                    for(int x=std::max(0,r-2);x<=std::min(14,r+2) && !nearby;++x)      // 在这个点周围 5 格见方的范围里找有没有已有棋子
                    {
                        for(int y=std::max(0,c-2);y<=std::min(14,c+2);++y)
                        {
                            if(board.cells[x][y]) {
                                nearby=true;
                                break;
                            }
                        }
                    }      // 找到一颗就认为这个点「有邻居」，可以留作候选
                    if(!nearby) {
                        continue;
                    }      // 周围空荡荡的点直接跳过，节省计算量
                    // 语句说明：普通候选分=自己棋型*10+对方棋型*11；稍偏防守，直接成五仍优先。
                    p.score=pointScore(r,c,stone)*10+pointScore(r,c,3-stone)*11;
                    if(level==2) {
                        p.score+=history[stone-1][r*15+c];
                    }
                    // 语句说明：把缓存最佳点优先排序；不是把这个加分当成真实获胜分。
                    if(r*15+c==preferred) {
                        p.score+=WIN/2;
                    }
                    normal.push_back(p);
                    // 这个点加入普通候选
                }
            }
        }
        // 【阅读提示】返回优先级：直接赢 > 对方一步赢的堵点 > 普通近邻候选；堵点多不代表一手能全挡住。
        if(!wins.empty()) {
            return wins;
        }      // 有能一步取胜的点，其它一概不考虑，直接返回
        if(!blocks.empty()) {
            return blocks;
        }      // 没有取胜点，但有必须挡的点，也只考虑这些
        // 语句说明：lambda比较两个AIPoint的score，按从大到小排列；只对normal排序。
        std::sort(normal.begin(),normal.end(),higherPointScoreFirst);
        if(static_cast<int>(normal.size())>limit) {
            normal.resize(limit);
        }      // 普通候选太多时截断，只保留评分最高的若干个
        return normal;
    }
    // 搜索到最后一层时，遍历棋盘，计算 自己已有棋子的棋形分总和-对方的棋形分总和。
    // 同一棋形可能被多颗棋子重复计分，所以它只是比较局面好坏的经验分。stone 是本次评分站在哪一方看。
    //
    //
    long long evaluate(int stone) {
        long long value=0;
        // 累加的总分
        for(int r=0;r<15;++r) {
            for(int c=0;c<15;++c) {
                if(board.cells[r][c]) {
                    int s=board.cells[r][c];
                    value+=(s==stone?1:-1)*pointScore(r,c,s);
                    // 自己的棋加正分，对方的棋加负分，代数和就是当前局势的优劣
                }
            }
        }
        return value;
    }
    // 在棋盘副本上轮流模拟双方下棋：试放一颗→判断五连→继续算对方的应对→撤掉这一颗。
    // depth 是还要模拟几层；一层代表落一颗。depth=0 时调用 evaluate 给局面打分。
    // 对方的好局面对自己是坏局面，所以递归返回值前取负号；3-stone 在黑1和白2之间切换。
    // alpha 是本层已找到的较好分数；beta 是上层给的停止界限。alpha>=beta 时，这条路线不必再算。
    // ply 是从开始搜索起已经走了几层。获胜分 WIN-ply 让更快获胜的路线分数更高。困难会记录算过的局面，减少重复计算。
    //
    //
    //
    //
    long long search(int stone,int depth,long long alpha,long long beta,int ply) {
        if(stopped()) {
            return 0;
        }      // 一开始就检查是否该中止，避免白算
        if(!depth) {
            return evaluate(stone);
        }      // 深度用尽，改用静态评分估计局势
        long long oldAlpha=alpha,oldBeta=beta;
        // 先把进来的 alpha、beta 存一份，后面写已计算局面的记录表时要用到
        unsigned long long key=hash^(stone==1?0x9e3779b97f4a7c15ULL:0x123456789abcdef0ULL);
        // 把「当前该谁下」也并进识别码；同一棋盘但轮次不同，是两个不同局面
        AIEntry* entry=level==2?&table[key%table.size()]:NULL;
        // 只有困难档查已计算局面的记录表；NULL 表示不启用
        int preferred=-1;
        // 缓存记录里的最佳点；-1表示无推荐，不保证一定来自紧邻的上一轮。
        if(entry && entry->key==key) {
            preferred=entry->point;
            // 只要完整key命中就可借最佳点排序；是否能采用缓存分数要再看depth。
            if(entry->depth>=depth) {
                // 缓存搜索深度足够，才根据flag使用其分数或边界。
                if(entry->flag==0) {
                    return entry->score;
                }      // 精确值：结论可以直接用，省掉整棵子树的搜索
                if(entry->flag==1) {
                    alpha=std::max(alpha,entry->score);
                }      // 只知道「不低于这个分」，可以据此收紧 alpha
                else {
                    beta=std::min(beta,entry->score);
                }      // 只知道「不高于这个分」，可以据此收紧 beta
                if(alpha>=beta) {
                    return entry->score;
                }      // 收紧后如果区间已经无效，同样可以直接采用这个结论
            }
        }
        std::vector<AIPoint> points=candidates(stone,preferred);
        if(interrupted || points.empty()) {
            return 0;
        }      // 被中途取消、或者根本没有可选点，返回 0 交给上层处理
        long long best=-WIN;
        int bestPoint=-1;
        // best 记录目前最好的分数；bestPoint 记录对应的落点
        for(size_t i=0;i<points.size();++i) {
            if(stopped()) {
                return 0;
            }      // 每个候选点开始前检查一次是否该中止
            const AIPoint& p=points[i];
            put(p.row,p.col,stone);
            // 把这一子放上棋盘开始推演
            // 语句说明：换对手视角后分数取负，窗口也反向；已成五则用WIN-ply偏好更早获胜。
            long long score=board.wins(p.row,p.col)?WIN-ply:-search(3-stone,depth-1,-beta,-alpha,ply+1);
            put(p.row,p.col,0);
            // 包括取消和超时出口都先撤回试落子。
            if(interrupted) {
                return 0;
            }      // 递归过程中被取消，立刻返回
            if(score>best) {
                best=score;
                bestPoint=p.row*15+p.col;
            }      // 这一步比目前最好的还好，就更新记录
            alpha=std::max(alpha,score);
            // alpha 是「我至少能拿到这么多」，所以取较大者
            // 语句说明：窗口已足够证明这条分支无需继续；困难档增加该点的历史排序奖励。
            if(alpha>=beta) {
                if(level==2) {
                    history[stone-1][p.row*15+p.col]+=depth*depth;
                }
                break;
            }
        }
        if(entry) {
            entry->key=key;
            entry->score=best;
            entry->depth=depth;
            entry->point=bestPoint;
            // 语句说明：依据进入本层时的原始窗口记录上界、下界或精确值。
            entry->flag=best<=oldAlpha?2:best>=oldBeta?1:0;
        }
        return best;
        // 返回这一层的分数给上一层
    }
    public:
    // 语句说明：公开的观测值：最后完整完成的一轮深度；中断轮次不计入。
    int completedDepth;
    // 建立一次AI思考任务：复制真实棋盘，并设置难度、候选数量和截止时间。
    // difficulty=0简单、1中等、2困难。普通候选上限12/12/16，时间预算500/500/2000毫秒。
    // history 先清零；给每个位置和颜色生成识别数字，用它们算出棋盘识别码。困难另准备65536条局面记录。
    //
    //
    //
    //
    AISearch(const LocalGame& source,int difficulty,volatile LONG* stop,unsigned int seed=1234567):
    board(source),level(difficulty),limit(difficulty==2?16:12),cancel(stop),
    deadline(GetTickCount64()+(difficulty==2?2000:500)),interrupted(false),randomState(seed?seed:1),hash(0),completedDepth(0) {
        // 各项成员的初值：难度决定候选上限和时间预算
        std::memset(history,0,sizeof(history));
        // 历史启发表清零
        for(int r=0;r<15;++r) {
            for(int c=0;c<15;++c) {
                for(int s=1;s<=2;++s) {
                    zobrist[r][c][s]=(static_cast<unsigned long long>(random())<<32)|random();
                    // 64 位随机数由两次 32 位随机数拼成
                    if(board.cells[r][c]==s) {
                        hash^=zobrist[r][c][s];
                    }      // 如果盘面上已经有子，就把它们的贡献并入初始识别码
                }
            }
        }
        if(level==2) {
            table.resize(65536);
        }      // 困难档才分配已计算局面的记录表空间，65536 条
    }
    // AI选位置的总入口：先准备备用空位，再调用 candidates 选出值得考虑的位置。
    // 直接能赢或只剩一个候选位置时立即返回。简单难度从最多3个达到最高分90%的正分候选位置中随机选。
    // 中等从1层算到最多4层，困难从1层算到最多8层；每层用 search 比较所有候选位置。
    // 时间不足或被取消时，使用上一次完整算完的结果；completedDepth 记录完整算完的层数。
    //
    //
    //
    //
    AIPoint choose() {
        AIPoint fallback= {
            -1,-1,0
        };
        if(board.result) {
            return fallback;
        }      // 棋局已经结束，返回空着法
        for(int r=0;r<15 && fallback.row<0;++r) {
            for(int c=0;c<15;++c)
            {
                if(!board.cells[r][c]) {
                    fallback.row=r;
                    fallback.col=c;
                    break;
                }
            }
        }      // 记下第一个空位作为备用着法
        if(fallback.row<0) {
            return fallback;
        }      // 连一个空位都没有，棋盘下满了
        std::vector<AIPoint> root=candidates(board.turn);
        // 生成电脑当前准备落子的这一层的候选着法列表
        if(root.empty()) {
            return fallback;
        }      // 没有候选点，只能用备用着法
        fallback=root[0];
        if(interrupted) {
            return fallback;
        }      // 以返回候选的首点作备用；只有完成的normal列表才经过评分排序。
        put(fallback.row,fallback.col,board.turn);
        bool immediate=board.wins(fallback.row,fallback.col);
        // 试一下这个点能不能直接连成五子
        put(fallback.row,fallback.col,0);
        // 试完还原
        if(immediate || root.size()==1) {
            return fallback;
        }      // 能一步取胜、或者只剩一个可选点，就不必再深搜了
        if(level==0) {
            int count=1;
            // count 是要参与随机的候选个数，从 1 开始
            while(count<3 && count<static_cast<int>(root.size()) && root[count].score>0 &&      // 条件依次是：最多取 3 个、不能超出候选总数、分数必须为正、且不能比最高分差太多
            root[count].score*10>=root[0].score*9) {
                ++count;
            }      // 满足就再多纳入一个候选
            return root[random()%count];
            // 在这几个「好但略有差别」的点里随机选一个，让电脑走棋有变化
        }
        // 【阅读提示】先算1层，再逐层增加深度：每轮从1层往上加；只有遍历完全部根候选才提交next。
        // 【阅读提示】timeout/cancel中断当前轮时沿用此前完整轮次结果，不采用半轮比较。
        for(int depth=1;depth<=(level==2?8:4);++depth) {
            long long best=-WIN;
            AIPoint next=fallback;
            // best重置为负胜分；next先沿用已有落点，本轮完整完成后才提交它。
            for(size_t i=0;i<root.size();++i) {
                if(stopped()) {
                    break;
                }
                AIPoint p=root[i];
                put(p.row,p.col,board.turn);
                // 把电脑当前准备落子的这一层这一手放上棋盘
                long long value=board.wins(p.row,p.col)?WIN:-search(3-board.turn,depth-1,-WIN,WIN,1);
                // 根的各候选均用完整[-WIN,WIN]窗口搜索；根循环没有共享递增alpha。
                put(p.row,p.col,0);
                // 试完还原
                if(interrupted) {
                    break;
                }      // 被取消就中止本轮
                if(value>best) {
                    best=value;
                    next=p;
                }      // 找到更好的着法就更新
            }
            if(interrupted) {
                break;
            }      // 本轮被取消，那就继续用上一轮完成的结果，不更新
            fallback=next;
            completedDepth=depth;
            // 本轮完整搜完，更新最佳着法和已完成深度
            for(size_t i=0;i<root.size();++i) {
                if(root[i].row==next.row && root[i].col==next.col) {
                    std::swap(root[0],root[i]);
                    break;
                    // 把这个点换到列表首位
                }
            }
            if(best>WIN/2) {
                break;
            }      // 当前受候选裁剪和评分约束的搜索已返回接近胜分，提前结束迭代。
        }
        return fallback;
    }
};
// 【类型 AIWorker】搜索线程的管理者。主线程先填snapshot，再启动run；后台只写answer，主线程确认线程
// 结束后通过take取走。generation让上层判断结果是否属于当前棋局。
struct AIWorker {
    HANDLE thread;
    volatile LONG cancelled;
    // 线程句柄；cancelled 是给搜索看的取消标志
    // 语句说明：snapshot棋盘副本；difficulty难度；generation任务版本；answer线程计算结果。
    LocalGame snapshot;
    int difficulty,generation;
    AIPoint answer;
    // 【函数 AIWorker::AIWorker】建立尚未启动的工作器。
    // 处理步骤：thread=NULL，cancelled=0，difficulty/generation=0；snapshot 自动初始化。
    // 变量含义：answer 未显式初始化，但只在成功创建的线程结束并写出结果后由 take 使用。
    AIWorker():thread(NULL),cancelled(0),difficulty(0),generation(0) {
    }      // 初始没有线程，标志清零
    // 【函数 AIWorker::run】CreateThread 调用的静态入口，返回 DWORD 0。
    // 处理步骤：把 context 转为 self；用当时状态的副本、难度、取消地址、时钟种子建 AISearch search；choose 结果
    // 写入 answer。
    // 变量含义：context 是传入的 this；self 为 AIWorker*；search 为本次搜索对象。
    static DWORD WINAPI run(void* context) {
        AIWorker* self=static_cast<AIWorker*>(context);
        // 取出 AIWorker 自身
        AISearch search(self->snapshot,self->difficulty,&self->cancelled,static_cast<unsigned int>(GetTickCount64()));
        // 把snapshot复制给AISearch；取消标志是共享地址，随机种子取当前时钟。
        self->answer=search.choose();
        return 0;
        // 开始搜索，结果写进 answer
    }
    // 【函数 AIWorker::cancel】发出定期检查是否应该停止的取消请求。
    // 处理步骤：InterlockedExchange 把 cancelled 设1；不强制杀线程，也不立即关闭句柄。
    void cancel() {
        InterlockedExchange(&cancelled,1);
    }      // 请求取消：把标志置 1，搜索循环很快就会发现
    // 【函数 AIWorker::start】为 game 启动 level 难度、gen 版本的任务；返回创建是否成功。
    // 处理步骤：已有 thread 就失败；复制 game；存参数；原子清取消标志；CreateThread(run,this)。
    // 变量含义：game 是输入只读引用；level/gen 为任务参数。
    bool start(const LocalGame& game,int level,int gen) {
        if(thread) {
            return false;
        }      // 句柄未回收前拒绝重用；线程可能已结束但take尚未领取结果。
        snapshot=game;
        difficulty=level;
        generation=gen;
        InterlockedExchange(&cancelled,0);
        // 复制一份当前棋盘作为当时状态的副本，并清空取消标志
        thread=CreateThread(NULL,0,run,this,0,NULL);
        return thread!=NULL;
        // 创建线程；返回句柄表示成功
    }
    // 【函数 AIWorker::take】尝试取结果，经 point、gen 输出；未完成返回 false。
    // 处理步骤：WaitForSingleObject(thread,0) 非阻塞检查；确认完成后复制 answer/generation；关句柄并置
    // NULL。
    // 变量含义：point/gen 是输出引用。
    // 注意：线程已经完成也仍可能是被取消或旧棋局的结果；接受与否由 updateAI 的额外条件决定。
    bool take(AIPoint& point,int& gen) {
        if(!thread || WaitForSingleObject(thread,0)!=WAIT_OBJECT_0) {
            return false;
        }      // 线程还没结束就返回假，表示结果尚未就绪
        point=answer;
        gen=generation;
        CloseHandle(thread);
        thread=NULL;
        return true;
        // 结果已就绪：取走落点、取走代号，并回收线程
    }
    // 【函数 AIWorker::shutdown】程序退出时彻底收尾。
    // 处理步骤：cancel；若有线程则无限等待实际结束，再关句柄清NULL。
    // 变量含义：等待期间不会继续该调用者的正常工作，适合退出清理。
    void shutdown() {
        cancel();
        if(thread) {
            WaitForSingleObject(thread,INFINITE);
            CloseHandle(thread);
            thread=NULL;
        }
    }
};
#endif
