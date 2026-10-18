# Graph Report - Gomoku2  (2026-10-12)

## Corpus Check
- Corpus is ~12,012 words - fits in a single context window. You may not need a graph.

## Summary
- 429 nodes · 768 edges · 15 communities
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 89 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- AI搜索 / 
- 共享状态与按钮 / 
- 联网规则 / 
- 网络传输 / 
- 联网界面 / 
- 复盘控制 / 棋盘规则
- 页面绘制 / 联网界面
- 共享状态与按钮
- 窗口与鼠标输入 / 
- 游戏动作 / 共享状态与按钮
- 棋谱存储 / 
- 联网界面
- 共享状态与按钮
- 对局元信息
- 联网规则

## God Nodes (most connected - your core abstractions)
1. `AISearch` - 28 edges
2. `Action` - 28 edges
3. `NetState` - 27 edges
4. `LocalGame` - 23 edges
5. `NetClient` - 22 edges
6. `NetAction` - 22 edges
7. `onlineAction()` - 21 edges
8. `ReplayPlayer` - 21 edges
9. `GameRecord` - 19 edges
10. `AIWorker` - 17 edges

## Surprising Connections (you probably didn't know these)
- `humanCanMove()` --calls--> `onlineCanMove()`  [INFERRED]
  game_actions.h → online_ui.h
- `openList()` --calls--> `readRecord()`  [INFERRED]
  game_actions.h → record_store.h
- `saveGame()` --calls--> `writeRecord()`  [INFERRED]
  game_actions.h → record_store.h
- `loadEntry()` --calls--> `readRecord()`  [INFERRED]
  game_actions.h → record_store.h
- `act()` --calls--> `onlineAction()`  [INFERRED]
  game_actions.h → online_ui.h

## Import Cycles
- None detected.

## Communities (15 total, 0 thin omitted)

### Community 0 - "AI搜索 / "
Cohesion: 0.05
Nodes (32): AIEntry, depth, flag, key, point, score, AIPoint, col (+24 more)

### Community 1 - "共享状态与按钮 / "
Cohesion: 0.06
Nodes (31): newerEntryFirst(), Button, enabled, h, id, menu, text, w (+23 more)

### Community 2 - "联网规则 / "
Cohesion: 0.06
Nodes (29): validResult(), netDecode(), netEncode(), NetPhase, N_CLOSED, N_FINISH, N_LOBBY, N_PEER (+21 more)

### Community 3 - "网络传输 / "
Cohesion: 0.08
Nodes (16): NetClient, address, hello, incoming, mutex, outgoing, port, stopFlag (+8 more)

### Community 4 - "联网界面 / "
Cohesion: 0.17
Nodes (30): handleWindowCloseRequest(), applyOnlineStateMessage(), drawOnlineArchivePanel(), drawOnlinePlayPanel(), drawOnlineSidebar(), drawOnlineStatus(), localAddresses(), onlineAction() (+22 more)

### Community 5 - "复盘控制 / 棋盘规则"
Cohesion: 0.08
Nodes (19): LocalGame, cells, moves, result, turn, Move, col, row (+11 more)

### Community 6 - "页面绘制 / 联网界面"
Cohesion: 0.14
Nodes (25): addLocalGameButtons(), drawAIDetails(), drawAIPanel(), drawBoard(), drawGame(), drawGameResult(), drawLocalDialog(), drawLocalPanel() (+17 more)

### Community 7 - "共享状态与按钮"
Cohesion: 0.07
Nodes (28): Action, A_AI, A_BACK, A_BLACK, A_CANCEL, A_CONTINUE, A_EASY, A_EXIT_GAME (+20 more)

### Community 8 - "窗口与鼠标输入 / "
Cohesion: 0.12
Nodes (17): boardCell(), humanCanMove(), createGameWindow(), createInputControls(), finishMouseClick(), gameWindowProc(), initializeProgram(), loadGameImages() (+9 more)

### Community 9 - "游戏动作 / 共享状态与按钮"
Cohesion: 0.20
Nodes (23): act(), beginName(), handleDialogAction(), handleGameAction(), handleListAction(), handleMenuAction(), handleReplayAction(), handleSaveAction() (+15 more)

### Community 10 - "棋谱存储 / "
Cohesion: 0.15
Nodes (19): encodeRecord(), fromUtf8(), GameRecord, difficulty, game, human, mode, name (+11 more)

### Community 11 - "联网界面"
Cohesion: 0.09
Nodes (22): NetAction, A_NET_AGAIN, A_NET_BACK, A_NET_CANCEL, A_NET_CONNECT, A_NET_COPY, A_NET_EXIT, A_NET_HOME (+14 more)

### Community 12 - "共享状态与按钮"
Cohesion: 0.22
Nodes (9): Page, DIFFICULTY_PAGE, GAME_PAGE, MAIN_MENU, MODE_MENU, NETWORK_PAGE, REPLAY_LIST, REPLAY_PAGE (+1 more)

### Community 13 - "对局元信息"
Cohesion: 0.25
Nodes (8): EndReason, END_DISCONNECT, END_DRAW, END_FIVE, END_QUIT, END_RESIGN, END_RESTART, END_UNKNOWN

### Community 14 - "联网规则"
Cohesion: 0.25
Nodes (8): LeaveReason, LEAVE_CANCEL, LEAVE_DECLINE, LEAVE_DROP, LEAVE_HOST, LEAVE_LOBBY, LEAVE_NONE, LEAVE_QUIT

## Knowledge Gaps
- **182 isolated node(s):** `row`, `col`, `score`, `key`, `score` (+177 more)
  These have ≤1 connection - possible missing edges. (Counts symbols only; 218 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `GameRecord` connect `棋谱存储 / ` to `复盘控制 / 棋盘规则`?**
  _High betweenness centrality (0.003) - this node is a cross-community bridge._
- **What connects `row`, `col`, `score` to the rest of the system?**
  _182 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `AI搜索 / ` be split into smaller, more focused modules?**
  _Cohesion score 0.05380852550663871 - nodes in this community are weakly interconnected._
- **Why does `NetState` connect `联网规则 / ` to `复盘控制 / 棋盘规则`?**
  _High betweenness centrality (0.002) - this node is a cross-community bridge._
- **Should `共享状态与按钮 / ` be split into smaller, more focused modules?**
  _Cohesion score 0.05858585858585859 - nodes in this community are weakly interconnected._
- **Should `联网规则 / ` be split into smaller, more focused modules?**
  _Cohesion score 0.0613107822410148 - nodes in this community are weakly interconnected._
- **Should `网络传输 / ` be split into smaller, more focused modules?**
  _Cohesion score 0.07539118065433854 - nodes in this community are weakly interconnected._