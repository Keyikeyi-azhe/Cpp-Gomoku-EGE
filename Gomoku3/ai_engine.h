// 【建议阅读 06 / AI 与工作线程】ai_engine.h
// AIPoint/AIEntry -> AISearch::choose -> candidates/pointScore -> search -> put。
// 最后看 AIWorker，再回 main.cpp 的 cancelAI/updateAI；区分完成搜索与允许采用答案。
// 下一站：network_protocol.h；把联网先当成接收命令的普通状态机。
// 定位写法为“文件:行号 (符号名)”；Ctrl+G跳行或搜索完整函数名。行号对应本次重新注释后的文件。
// 源码仍为GBK/CP936；新增内容均为注释。已有PDF的旧行号会变化，对照时优先搜索函数名。
// 外部接口声明位置：标准库对应的头文件；Win32/Winsock系统头；EGE的graphics.h。
// 头文件保护：首次包含时定义下面的宏，再次包含会跳过本文件，避免同一编译单元重复定义。
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
struct AIPoint {int row,col; long long score;};
// 【类型 AIEntry】困难档置换表的一条缓存。key为含轮次的局面键；score为缓存值；depth为搜索深度；
// flag=0精确值/1下界/2上界；point为row*15+col最佳点。
struct AIEntry {
    unsigned long long key; long long score; int depth,flag,point;  // key局面键；score搜索结论；depth已搜索深度；flag=0精确/1下界/2上界；point压平落点。
    // 【函数 AIEntry::AIEntry】初始化一个未使用的置换表槽。
    // 执行顺序：key/score/flag=0，depth=-1，point=-1。
    // 参数/局部变量：depth=-1 使默认条目不能满足正常深度命中条件。
    // 类型定义：ai_engine.h:23 (AIEntry)。
    AIEntry():key(0),score(0),depth(-1),flag(0),point(-1) {}
};
// 【类型 AISearch】一次AI思考的搜索器，持有独立LocalGame副本。public以前的成员默认private；choose
// 是选点入口，completedDepth供观察完整完成的深度。它不调用图形API，也不直接修改客户端全局game。
class AISearch {
    // 类型定义：game_logic.h:20 (LocalGame)。
    LocalGame board;  // 自己那份棋盘副本，在上面随便试子不会影响真正的对局
    int level,limit;  // level 难度 0-2；limit 是普通候选点最多保留多少个
    volatile LONG* cancel;  // 指向有效取消标志的地址；通过Interlocked操作访问，volatile本身不是完整同步机制。
    ULONGLONG deadline;  // 本次思考的截止时刻(毫秒)
    bool interrupted;  // 一旦被打断就置真，后面所有循环看到它都尽快退出
    unsigned int randomState;  // 伪随机数的当前状态
    unsigned long long hash,zobrist[15][15][3];  // hash 是当前局面的指纹；zobrist 是每个「格子和颜色」对应的随机数
    // 类型定义：ai_engine.h:23 (AIEntry)。
    std::vector<AIEntry> table;  // 置换表本体，只有困难档才启用
    long long history[2][225];  // 颜色下标stone-1；落点下标r*15+c；发生剪枝时累加depth*depth，不是简单次数。
    static const long long WIN=1000000000000LL;  // 搜索胜分常量；用数量级区分终局胜负与普通启发估分。
    // 【函数 AISearch::stopped】返回搜索是否应停止，并把 interrupted 锁定为 true。
    // 执行顺序：原子读取 cancel；或比较当前毫秒与 deadline；任一成立就标记中断。
    // 参数/局部变量：InterlockedCompareExchange(cancel,0,0) 利用原子操作读取当前值。
    // 边界与易错点：volatile 本身不提供完整线程同步；这里依靠 Windows 原子操作及线程结束等待。cancel
    // 由调用方保证指向有效变量。
    // 从上层理解：ai_engine.h:126 (AISearch::candidates)；ai_engine.h:262 (AISearch::choose)；
    // ai_engine.h:186 (AISearch::search)。
    bool stopped() {
        if(InterlockedCompareExchange(cancel,0,0) || GetTickCount64()>=deadline) interrupted=true;  // 任一条件成立就标记为已打断
        return interrupted;
    }
    // 【函数 AISearch::random】返回下一次 32 位伪随机值。
    // 执行顺序：randomState 依次执行左移13、右移17、左移5后的异或更新。
    // 参数/局部变量：没有局部变量；每次调用改变 randomState。
    // 边界与易错点：固定种子使算法顺序可重复；AIWorker 实际用时钟种子，所以每盘选择可能不同。不是密码
    // 学随机数。
    // 从上层理解：ai_engine.h:241 (AISearch::AISearch)；ai_engine.h:262 (AISearch::choose)。
    unsigned int random() {
        randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;  // 三次移位异或，是 xorshift 的核心步骤
        return randomState;
    }
    // 【函数 AISearch::put】仅在搜索副本格子上试放/撤回，同时维护 hash。
    // 执行顺序：old 非零则异或掉旧键；写 board.cells；新 stone 非零则异或加入新键。
    // 参数/局部变量：r/c 目标位置；stone=0 表示撤子；old 是原来的颜色。
    // 边界与易错点：它不更新 turn、moves、result！递归通过参数 stone 管轮次，通过 wins 判试落子。不要
    // 把 put 当正式 place 使用。
    // 从上层理解：ai_engine.h:126 (AISearch::candidates)；ai_engine.h:262 (AISearch::choose)；
    // ai_engine.h:186 (AISearch::search)。
    void put(int r,int c,int stone) {
        int old=board.cells[r][c];  // 先看看这格原来是什么子
        if(old) hash^=zobrist[r][c][old];  // 如果有子，先把它的随机数异或掉，相当于抹掉这一格的影响
        board.cells[r][c]=stone;  // 放入新子
        if(stone) hash^=zobrist[r][c][stone];  // 再异或新子的随机数，加上它的影响；异或两次等于抵消，所以撤子同理
    }
    // 【函数 AISearch::pointScore】估算在 r、c 放 stone 的局部棋型分。
    // 执行顺序：每个方向取 -5..5 共11格；中心视为 X；同色X，空位点号，敌子/边界#；按从强到弱的模式只取
    // 该方向第一档命中分；四方向求和。
    // 参数/局部变量：dir 为方向；d 方向下标；sum 总分；line 11字符局部线；k 相对偏移；x/y 实际行列；
    // value 本方向得分。
    // 边界与易错点：这是启发式字符串识别，不是严格棋型证明。find 在整段线中匹配，不强制命中的棋型包含
    // 中心，因此附近已有棋型也可能影响评分。
    // 从上层理解：ai_engine.h:126 (AISearch::candidates)；ai_engine.h:168 (AISearch::evaluate)。
    long long pointScore(int r,int c,int stone) {
        static const int dir[4][2]={{1,0},{0,1},{1,1},{1,-1}};  // (行增量,列增量)：竖直、水平、右下斜线、左下斜线；负k负责另一端。
        long long sum=0;  // 四条线的分数累加在这里
        for(int d=0;d<4;++d) {  // 逐一处理四个方向
            std::string line;  // 这一条线的局部字符串
            for(int k=-5;k<=5;++k) {  // 从中心第 -5 格取到第 +5 格，共 11 格
                int x=r+k*dir[d][0],y=c+k*dir[d][1];  // 把相对偏移 k 换算成棋盘上的绝对坐标
                if(!k) line+='X';  // k 等于 0 就是中心点，也就是这一手的落点，记作 X
                else if(x<0 || x>=15 || y<0 || y>=15) line+='#';  // 越出棋盘边界的格子记作井号，表示「此路不通」
                else line+=board.cells[x][y]==stone?'X':board.cells[x][y]==0?'.':'#';  // 否则看那一格：是自己就记 X，是空就记点号，是对方就记井号
            }
            // 【阅读提示】每条11格线按从强到弱的else-if选一个分档，再将四方向相加。
            // 【阅读提示】find在整段内匹配，未强制棋型经过中心；因此这是启发式评分，不是严格棋型证明。
            long long value=0;
            if(line.find("XXXXX")!=std::string::npos) value=100000000;
            else if(line.find(".XXXX.")!=std::string::npos) value=1000000;
            else if(line.find("XXXX.")!=std::string::npos || line.find(".XXXX")!=std::string::npos ||
                    line.find("XXX.X")!=std::string::npos || line.find("XX.XX")!=std::string::npos ||
                    line.find("X.XXX")!=std::string::npos) value=100000;
            else if(line.find(".XXX.")!=std::string::npos || line.find(".XX.X.")!=std::string::npos ||
                    line.find(".X.XX.")!=std::string::npos) value=10000;
            else if(line.find("XXX.")!=std::string::npos || line.find(".XXX")!=std::string::npos ||
                    line.find("XX.X")!=std::string::npos || line.find("X.XX")!=std::string::npos) value=1000;
            else if(line.find(".XX.")!=std::string::npos || line.find(".X.X.")!=std::string::npos) value=100;
            else if(line.find("XX.")!=std::string::npos || line.find(".XX")!=std::string::npos) value=10;
            sum+=value;  // 把这条线的得分累加起来
        }
        return sum;  // 四条线的总分就是这一手的评分
    }
    // 【函数 AISearch::candidates】为 stone 生成按优先级筛选的候选，preferred 默认 -1。
    // 执行顺序：空盘只选中心；逐空格试自己能否赢、对方能否赢并恢复；普通点须两格邻域有子；算攻防分、历
    // 史奖励与优先点奖励；有直接赢点只返回 wins，否则有威胁只返回 blocks，否则排序截断 normal。
    // 参数/局部变量：wins/blocks/normal 为三类数组；occupied 是否非空盘；r/c 遍历格子；center 为中心点；
    // win/block 是试子的胜判定；p 当前候选；nearby/x/y 是邻域扫描；排序 a/b 为候选引用。
    // 边界与易错点：“有多个必须挡点”不代表一手能全挡。wins/blocks 不走普通排序/数量截断，其 p.score 初
    // 始为0。中途 stopped 可能返回未完成 normal，choose 会按 interrupted 走兜底。
    // 从上层理解：ai_engine.h:262 (AISearch::choose)；ai_engine.h:186 (AISearch::search)。
    // 类型定义：ai_engine.h:20 (AIPoint)。
    std::vector<AIPoint> candidates(int stone,int preferred=-1) {
        std::vector<AIPoint> wins,blocks,normal;  // 分三类收集：能直接赢的点、必须挡的点、普通候选点
        bool occupied=false;  // occupied 用来判断棋盘上是否已经有子
        for(int r=0;r<15;++r) for(int c=0;c<15;++c) if(board.cells[r][c]) occupied=true;
        if(!occupied) {AIPoint center={7,7,0};normal.push_back(center);return normal;}  // 空棋盘的第一手：没有参考，就直接下在正中央(7,7)
        for(int r=0;r<15;++r) {
            // 首次调用定位：ai_engine.h:53 (AISearch::stopped)。
            if(stopped()) return normal;
            for(int c=0;c<15;++c) if(!board.cells[r][c]) {  // 只考察空位
                // 首次调用定位：ai_engine.h:74 (AISearch::put)；game_logic.h:53 (LocalGame::wins)。
                put(r,c,stone);bool win=board.wins(r,c);put(r,c,3-stone);  // 先试自己能否赢，再把同一位置换成对方颜色；put同步更新hash。
                bool block=board.wins(r,c);put(r,c,0);  // 测试对方在该点能否赢，随后立刻撤回到空格。
                AIPoint p={r,c,0};  // 本候选初始score=0；普通候选稍后打分，wins/blocks保留此初值。
                if(win) wins.push_back(p);  // 能直接赢的点单独收集
                if(block) blocks.push_back(p);  // 必须防的点单独收集
                bool nearby=false;
                for(int x=std::max(0,r-2);x<=std::min(14,r+2) && !nearby;++x)  // 在这个点周围 5 格见方的范围里找有没有已有棋子
                    for(int y=std::max(0,c-2);y<=std::min(14,c+2);++y)
                        if(board.cells[x][y]) {nearby=true;break;}  // 找到一颗就认为这个点「有邻居」，可以留作候选
                if(!nearby) continue;  // 周围空荡荡的点直接跳过，节省计算量
                // 首次调用定位：ai_engine.h:88 (AISearch::pointScore)。
                // 语句说明：普通候选分=自己棋型*10+对方棋型*11；稍偏防守，直接成五仍优先。
                p.score=pointScore(r,c,stone)*10+pointScore(r,c,3-stone)*11;
                if(level==2) p.score+=history[stone-1][r*15+c];
                // 语句说明：把缓存最佳点优先排序；不是把这个加分当成真实获胜分。
                if(r*15+c==preferred) p.score+=WIN/2;
                normal.push_back(p);  // 这个点加入普通候选
            }
        }
        // 【阅读提示】返回优先级：直接赢 > 对方一步赢的堵点 > 普通近邻候选；堵点多不代表一手能全挡住。
        if(!wins.empty()) return wins;  // 有能一步取胜的点，其它一概不考虑，直接返回
        if(!blocks.empty()) return blocks;  // 没有取胜点，但有必须挡的点，也只考虑这些
        // 语句说明：lambda比较两个AIPoint的score，按从大到小排列；只对normal排序。
        std::sort(normal.begin(),normal.end(),[](const AIPoint& a,const AIPoint& b){return a.score>b.score;});
        if(static_cast<int>(normal.size())>limit) normal.resize(limit);  // 普通候选太多时截断，只保留评分最高的若干个
        return normal;
    }
    // 【函数 AISearch::evaluate】从 stone 的视角给当前整盘估分。
    // 执行顺序：遍历所有有子的格；自己的 pointScore 加分，对方减分，得到代数和。
    // 参数/局部变量：value 为累计值；r/c 遍历位置；s 为格上实际棋色。
    // 边界与易错点：同一条棋型可能被多个棋子重复贡献；这是设计中的启发估计，不能解释成精确获胜概率。
    // 从上层理解：ai_engine.h:186 (AISearch::search)。
    long long evaluate(int stone) {
        long long value=0;  // 累加的总分
        for(int r=0;r<15;++r) for(int c=0;c<15;++c) if(board.cells[r][c]) {
            // 首次调用定位：ai_engine.h:88 (AISearch::pointScore)。
            int s=board.cells[r][c];value+=(s==stone?1:-1)*pointScore(r,c,s);  // 自己的棋加正分，对方的棋加负分，代数和就是当前局势的优劣
        }
        return value;
    }
    // 【阅读提示】递归视角：本方价值 = - 对手价值；因此窗口同时从[alpha,beta]变为[-beta,-alpha]。
    // 【函数 AISearch::search】Negamax + Alpha-Beta：返回 stone 在剩余 depth 下的估计最佳分。
    // 执行顺序：先处理停止和深度0；困难档查置换表；生成候选；逐个 put、判胜或递归取负；无论子搜索是否
    // 中断先撤子；更新 best/alpha；alpha>=beta 则剪枝；记录界限类型后返回。
    // 参数/局部变量：stone 是当前搜索方；depth 是剩余层数；alpha/beta 是搜索窗口；ply 是离根步数；
    // oldAlpha/oldBeta 保存原窗口；key 合并盘面哈希和执棋方；entry 是表槽指针；preferred 是缓存点；
    // points 候选表；best/bestPoint 当前最好分与点；i/p 当前候选；score 为换回本方视角后的子树分。
    // 边界与易错点：返回0可能表示中断、无候选或中性估分，必须结合 interrupted；上层不会把中断一半的轮
    // 次当完整答案。
    // 从上层理解：ai_engine.h:262 (AISearch::choose)。
    long long search(int stone,int depth,long long alpha,long long beta,int ply) {
        // 首次调用定位：ai_engine.h:53 (AISearch::stopped)。
        if(stopped()) return 0;  // 一开始就检查是否该中止，避免白算
        // 首次调用定位：ai_engine.h:168 (AISearch::evaluate)。
        if(!depth) return evaluate(stone);  // 深度用尽，改用静态评分估计局势
        long long oldAlpha=alpha,oldBeta=beta;  // 先把进来的 alpha、beta 存一份，后面写置换表时要用到
        unsigned long long key=hash^(stone==1?0x9e3779b97f4a7c15ULL:0x123456789abcdef0ULL);  // 把「当前该谁下」也并进指纹；同一棋盘但轮次不同，是两个不同局面
        // 类型定义：ai_engine.h:23 (AIEntry)。
        AIEntry* entry=level==2?&table[key%table.size()]:NULL;  // 只有困难档查置换表；NULL 表示不启用
        int preferred=-1;  // 缓存记录里的最佳点；-1表示无推荐，不保证一定来自紧邻的上一轮。
        if(entry && entry->key==key) {
            preferred=entry->point;  // 只要完整key命中就可借最佳点排序；是否能采用缓存分数要再看depth。
            if(entry->depth>=depth) {  // 缓存搜索深度足够，才根据flag使用其分数或边界。
                if(entry->flag==0) return entry->score;  // 精确值：结论可以直接用，省掉整棵子树的搜索
                if(entry->flag==1) alpha=std::max(alpha,entry->score);  // 只知道「不低于这个分」，可以据此收紧 alpha
                else beta=std::min(beta,entry->score);  // 只知道「不高于这个分」，可以据此收紧 beta
                if(alpha>=beta) return entry->score;  // 收紧后如果区间已经无效，同样可以直接采用这个结论
            }
        }
        // 类型定义：ai_engine.h:20 (AIPoint)。
        // 首次调用定位：ai_engine.h:126 (AISearch::candidates)。
        std::vector<AIPoint> points=candidates(stone,preferred);
        if(interrupted || points.empty()) return 0;  // 被中途取消、或者根本没有可选点，返回 0 交给上层处理
        long long best=-WIN;int bestPoint=-1;  // best 记录目前最好的分数；bestPoint 记录对应的落点
        for(size_t i=0;i<points.size();++i) {
            if(stopped()) return 0;  // 每个候选点开始前检查一次是否该中止
            // 首次调用定位：ai_engine.h:74 (AISearch::put)。
            const AIPoint& p=points[i];put(p.row,p.col,stone);  // 把这一子放上棋盘开始推演
            // 首次调用定位：game_logic.h:53 (LocalGame::wins)；ai_engine.h:186 (AISearch::search)。
            // 语句说明：换对手视角后分数取负，窗口也反向；已成五则用WIN-ply偏好更早获胜。
            long long score=board.wins(p.row,p.col)?WIN-ply:-search(3-stone,depth-1,-beta,-alpha,ply+1);
            put(p.row,p.col,0); // 包括取消和超时出口都先撤回试落子。
            if(interrupted) return 0;  // 递归过程中被取消，立刻返回
            if(score>best) {best=score;bestPoint=p.row*15+p.col;}  // 这一步比目前最好的还好，就更新记录
            alpha=std::max(alpha,score);  // alpha 是「我至少能拿到这么多」，所以取较大者
            // 语句说明：窗口已足够证明这条分支无需继续；困难档增加该点的历史排序奖励。
            if(alpha>=beta) {if(level==2) history[stone-1][p.row*15+p.col]+=depth*depth;break;}
        }
        if(entry) {entry->key=key;entry->score=best;entry->depth=depth;entry->point=bestPoint;
            // 语句说明：依据进入本层时的原始窗口记录上界、下界或精确值。
            entry->flag=best<=oldAlpha?2:best>=oldBeta?1:0;}
        return best;  // 返回这一层的分数给上一层
    }
public:
    // 语句说明：公开的观测值：最后完整完成的一轮深度；中断轮次不计入。
    int completedDepth;
    // 【函数 AISearch::AISearch】为 source 创建一次独立搜索。
    // 执行顺序：复制棋盘；按 difficulty 配置上限和时间；清 history；生成 Zobrist 随机数并合成初始 hash；
    // 困难档分配表。
    // 参数/局部变量：source 为原棋盘只读引用；difficulty 为难度；stop 是取消标志地址；seed 默认1234567；
    // r/c/s 为格子与棋色循环。
    // 边界与易错点：困难预算2000毫秒，其他500毫秒；简单档不会进行迭代加深。初始化和所有子步骤也可能耗
    // 时，预算是协作式检查而非硬实时中断。
    // 从上层理解：ai_engine.h:325 (AIWorker::run)。
    // 类型定义：ai_engine.h:33 (AISearch)；game_logic.h:20 (LocalGame)。
    AISearch(const LocalGame& source,int difficulty,volatile LONG* stop,unsigned int seed=1234567):
        board(source),level(difficulty),limit(difficulty==2?16:12),cancel(stop),
        deadline(GetTickCount64()+(difficulty==2?2000:500)),interrupted(false),randomState(seed?seed:1),hash(0),completedDepth(0) {  // 各项成员的初值：难度决定候选上限和时间预算
        std::memset(history,0,sizeof(history));  // 历史启发表清零
        for(int r=0;r<15;++r) for(int c=0;c<15;++c) for(int s=1;s<=2;++s) {
            // 首次调用定位：ai_engine.h:63 (AISearch::random)。
            zobrist[r][c][s]=(static_cast<unsigned long long>(random())<<32)|random();  // 64 位随机数由两次 32 位随机数拼成
            if(board.cells[r][c]==s) hash^=zobrist[r][c][s];  // 如果盘面上已经有子，就把它们的贡献并入初始指纹
        }
        if(level==2) table.resize(65536);  // 困难档才分配置换表空间，65536 条
    }
    // 【函数 AISearch::choose】对外的选点入口，返回 AIPoint。
    // 执行顺序：终局/满盘返回无效点；扫描首个空点兜底；生成根候选；直接赢或只有一个点就返回；简单档近
    // 优随机；一般/困难逐深度完整搜索，完成才替换 fallback，超时保留上一轮结果。
    // 参数/局部变量：fallback 保底/已完成最佳点；r/c 首空扫描；root 根候选；immediate 能否直接赢；
    // count 简单档参与随机的前1..3个；depth 当前迭代深度；best 本轮最好分；next 本轮暂定最佳点；i/p 当
    // 前根点；value 根搜索分。
    // 边界与易错点：简单档仅在分数>0且至少为最高分90%的最多3点中随机；一般最多4层，困难最多8层，均不保
    // 证真的完成上限。returned score 是候选启发分，没有被赋为 value。
    // 从上层理解：ai_engine.h:325 (AIWorker::run)。
    // 类型定义：ai_engine.h:20 (AIPoint)。
    AIPoint choose() {
        AIPoint fallback={-1,-1,0};
        if(board.result) return fallback;  // 棋局已经结束，返回空着法
        for(int r=0;r<15 && fallback.row<0;++r) for(int c=0;c<15;++c)
            if(!board.cells[r][c]) {fallback.row=r;fallback.col=c;break;}  // 记下第一个空位作为兜底着法
        if(fallback.row<0) return fallback;  // 连一个空位都没有，棋盘下满了
        // 首次调用定位：ai_engine.h:126 (AISearch::candidates)。
        std::vector<AIPoint> root=candidates(board.turn);  // 生成根节点的候选着法列表
        if(root.empty()) return fallback;  // 没有候选点，只能用兜底着法
        fallback=root[0];if(interrupted) return fallback;  // 以返回候选的首点作兜底；只有完成的normal列表才经过评分排序。
        // 首次调用定位：ai_engine.h:74 (AISearch::put)；game_logic.h:53 (LocalGame::wins)。
        put(fallback.row,fallback.col,board.turn);bool immediate=board.wins(fallback.row,fallback.col);  // 试一下这个点能不能直接连成五子
        put(fallback.row,fallback.col,0);  // 试完还原
        if(immediate || root.size()==1) return fallback;  // 能一步取胜、或者只剩一个可选点，就不必再深搜了
        if(level==0) {
            int count=1;  // count 是要参与随机的候选个数，从 1 开始
            while(count<3 && count<static_cast<int>(root.size()) && root[count].score>0 &&  // 条件依次是：最多取 3 个、不能超出候选总数、分数必须为正、且不能比最高分差太多
                  root[count].score*10>=root[0].score*9) ++count;  // 满足就再多纳入一个候选
            // 首次调用定位：ai_engine.h:63 (AISearch::random)。
            return root[random()%count];  // 在这几个「好但略有差别」的点里随机选一个，让电脑走棋有变化
        }
        // 【阅读提示】迭代加深：每轮从1层往上加；只有遍历完全部根候选才提交next。
        // 【阅读提示】timeout/cancel中断当前轮时沿用此前完整轮次结果，不采用半轮比较。
        for(int depth=1;depth<=(level==2?8:4);++depth) {
            long long best=-WIN;AIPoint next=fallback;  // best重置为负胜分；next先沿用已有落点，本轮完整完成后才提交它。
            for(size_t i=0;i<root.size();++i) {
                // 首次调用定位：ai_engine.h:53 (AISearch::stopped)。
                if(stopped()) break;
                AIPoint p=root[i];put(p.row,p.col,board.turn);  // 把根节点这一手放上棋盘
                // 首次调用定位：ai_engine.h:186 (AISearch::search)。
                long long value=board.wins(p.row,p.col)?WIN:-search(3-board.turn,depth-1,-WIN,WIN,1);  // 根的各候选均用完整[-WIN,WIN]窗口搜索；根循环没有共享递增alpha。
                put(p.row,p.col,0);  // 试完还原
                if(interrupted) break;  // 被取消就中止本轮
                if(value>best) {best=value;next=p;}  // 找到更好的着法就更新
            }
            if(interrupted) break;  // 本轮被取消，那就继续用上一轮完成的结果，不更新
            fallback=next;completedDepth=depth;  // 本轮完整搜完，更新最佳着法和已完成深度
            for(size_t i=0;i<root.size();++i) if(root[i].row==next.row && root[i].col==next.col) {
                std::swap(root[0],root[i]);break;  // 把这个点换到列表首位
            }
            if(best>WIN/2) break;  // 当前受候选裁剪和评分约束的搜索已返回接近胜分，提前结束迭代。
        }
        return fallback;
    }
};

// 【类型 AIWorker】搜索线程的管理者。主线程先填snapshot，再启动run；后台只写answer，主线程确认线程
// 结束后通过take取走。generation让上层判断结果是否属于当前棋局。
struct AIWorker {
    HANDLE thread;volatile LONG cancelled;  // 线程句柄；cancelled 是给搜索看的取消标志
    // 类型定义：game_logic.h:20 (LocalGame)；ai_engine.h:20 (AIPoint)。
    // 语句说明：snapshot棋盘副本；difficulty难度；generation任务版本；answer线程计算结果。
    LocalGame snapshot;int difficulty,generation;AIPoint answer;
    // 【函数 AIWorker::AIWorker】建立尚未启动的工作器。
    // 执行顺序：thread=NULL，cancelled=0，difficulty/generation=0；snapshot 自动初始化。
    // 参数/局部变量：answer 未显式初始化，但只在成功创建的线程结束并写出结果后由 take 使用。
    // 类型定义：ai_engine.h:310 (AIWorker)。
    AIWorker():thread(NULL),cancelled(0),difficulty(0),generation(0) {}  // 初始没有线程，标志清零
    // 【函数 AIWorker::run】CreateThread 调用的静态入口，返回 DWORD 0。
    // 执行顺序：把 context 转为 self；用快照、难度、取消地址、时钟种子建 AISearch search；choose 结果
    // 写入 answer。
    // 参数/局部变量：context 是传入的 this；self 为 AIWorker*；search 为本次搜索对象。
    // 从上层理解：ai_engine.h:343 (AIWorker::start)。
    static DWORD WINAPI run(void* context) {
        // 类型定义：ai_engine.h:310 (AIWorker)。
        AIWorker* self=static_cast<AIWorker*>(context);  // 取出 AIWorker 自身
        // 类型定义：ai_engine.h:33 (AISearch)。
        // 首次调用定位：ai_engine.h:241 (AISearch::AISearch)。
        AISearch search(self->snapshot,self->difficulty,&self->cancelled,static_cast<unsigned int>(GetTickCount64()));  // 把snapshot复制给AISearch；取消标志是共享地址，随机种子取当前时钟。
        // 首次调用定位：ai_engine.h:262 (AISearch::choose)。
        self->answer=search.choose();return 0;  // 开始搜索，结果写进 answer
    }
    // 【函数 AIWorker::cancel】发出协作式取消请求。
    // 执行顺序：InterlockedExchange 把 cancelled 设1；不强制杀线程，也不立即关闭句柄。
    // 从上层理解：ai_engine.h:365 (AIWorker::shutdown)；main.cpp:81 (cancelAI)。
    void cancel() {InterlockedExchange(&cancelled,1);}  // 请求取消：把标志置 1，搜索循环很快就会发现
    // 【函数 AIWorker::start】为 game 启动 level 难度、gen 版本的任务；返回创建是否成功。
    // 执行顺序：已有 thread 就失败；复制 game；存参数；原子清取消标志；CreateThread(run,this)。
    // 参数/局部变量：game 是输入只读引用；level/gen 为任务参数。
    // 从上层理解：main.cpp:416 (updateAI)。
    // 类型定义：game_logic.h:20 (LocalGame)。
    bool start(const LocalGame& game,int level,int gen) {
        if(thread) return false;  // 句柄未回收前拒绝重用；线程可能已结束但take尚未领取结果。
        snapshot=game;difficulty=level;generation=gen;InterlockedExchange(&cancelled,0);  // 复制一份当前棋盘作为快照，并清空取消标志
        // 首次调用定位：ai_engine.h:325 (AIWorker::run)。
        thread=CreateThread(NULL,0,run,this,0,NULL);return thread!=NULL;  // 创建线程；返回句柄表示成功
    }
    // 【函数 AIWorker::take】尝试取结果，经 point、gen 输出；未完成返回 false。
    // 执行顺序：WaitForSingleObject(thread,0) 非阻塞检查；确认完成后复制 answer/generation；关句柄并置
    // NULL。
    // 参数/局部变量：point/gen 是输出引用。
    // 边界与易错点：线程已经完成也仍可能是被取消或旧棋局的结果；接受与否由 updateAI 的额外条件决定。
    // 从上层理解：main.cpp:416 (updateAI)。
    // 类型定义：ai_engine.h:20 (AIPoint)。
    bool take(AIPoint& point,int& gen) {
        if(!thread || WaitForSingleObject(thread,0)!=WAIT_OBJECT_0) return false;  // 线程还没结束就返回假，表示结果尚未就绪
        point=answer;gen=generation;CloseHandle(thread);thread=NULL;return true;  // 结果已就绪：取走落点、取走代号，并回收线程
    }
    // 【函数 AIWorker::shutdown】程序退出时彻底收尾。
    // 执行顺序：cancel；若有线程则无限等待实际结束，再关句柄清NULL。
    // 参数/局部变量：等待期间不会继续该调用者的正常工作，适合退出清理。
    // 从上层理解：main.cpp:669 (main)。
    // 首次调用定位：ai_engine.h:337 (AIWorker::cancel)。
    void shutdown() {cancel();if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=NULL;}}
};
#endif
