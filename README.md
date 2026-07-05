# 三炮十五兵求解器

三炮十五兵求解器是一个 C++20 编写的完整表库项目，包含规则建模、WDL 表库、MTD 表库、本地 HTTP 后端和 Web UI，用于分析三炮十五兵任意合法局面的胜负结果与最优着法。

本项目是本地运行工具，不是在线服务。表库、后端和 Web UI 都运行在用户自己的机器上。

## 目录

- [项目简介](#项目简介)
- [游戏规则](#游戏规则)
- [功能特性](#功能特性)
- [快速开始](#快速开始)
- [准备表库](#准备表库)
- [编译](#编译)
- [运行本地 UI](#运行本地-ui)
- [表库说明](#表库说明)
- [MTD 推荐说明](#mtd-推荐说明)
- [CLI 用法](#cli-用法)
- [本地 API 示例](#本地-api-示例)
- [Web UI](#web-ui)
- [项目结构](#项目结构)
- [验证与测试](#验证与测试)
- [Release 说明](#release-说明)
- [许可证](#许可证)

## 项目简介

三炮十五兵是一个 5×5 棋盘上的双人零和游戏：

- 炮方有 3 枚炮。
- 兵方最多有 15 枚兵。
- 任意合法局面都可以通过表库判定为炮胜、兵胜或和棋。

本项目提供从规则引擎到本地交互界面的完整工具链，可以查询：

- 当前局面的胜 / 和 / 负结果：`CannonWin`、`SoldierWin`、`Draw`。
- 推荐下一步。
- 所有合法着法的评价与分组。
- WDL 表库中的胜 / 和 / 负结果。
- MTD 表库中的材料目标与保证步数。
- 本地 Web UI 中的棋盘交互、推荐提示、合法着法分析、双方先走比较和路线探索。

## 游戏规则

棋盘为 5×5，格号按从左到右、从上到下编号：

```text
0  1  2  3  4
5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
```

初始局面：

```text
SSSSS/SSSSS/SSSSS/...../.CCC. c
```

记号含义：

- `S` 表示兵。
- `C` 表示炮。
- `.` 表示空点。
- `c` 表示炮方走。
- `s` 表示兵方走。

行棋规则：

- 炮可以向上、下、左、右移动一格到空点。
- 炮可以隔一个空点跳吃同方向的兵，即 `C . S`，落到兵所在格并移除该兵。
- 兵可以向上、下、左、右移动一格到空点。
- 兵不能吃子。
- 兵数少于 4 时，炮方胜。
- 炮方无合法着法且兵数不少于 4 时，兵方胜。
- 其它非终局局面由完整表库判定为炮胜、兵胜或和棋。

## 功能特性

- C++20 规则引擎与合法着法生成。
- 分层 WDL 表库，保存 Win/Draw/Loss 结果。
- MTD 表库，保存材料目标与保证步数。
- packed12 MTD 编码。
- mmap 表库读取。
- 本地 HTTP 后端，提供查询、推荐、双方先走比较和路线探索 API。
- TypeScript Web UI，支持棋盘交互、编辑局面、推荐提示和合法着法分析。
- MTD-aware 最优着法推荐。
- 合法着法分组与推荐排序。
- WDL 路线探索。
- 表库 inspect / verify 工具。
- 浏览器文件模式读取 WDL `.s15res` 表库。
- 本地后端模式加载完整 WDL + MTD 表库。

## 快速开始

克隆仓库：

```powershell
git clone https://github.com/stardogworld/sanpao15.git
cd sanpao15
```

构建 Web UI：

```powershell
npm --prefix ui install
npm --prefix ui run build
```

构建 C++：

```powershell
cmake -S . -B build-mtd-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-mtd-release --config Release
```

如果使用 Visual Studio 多配置生成器，生成的可执行文件通常位于：

```text
build-mtd-release\Release\sanpao15_cli.exe
```

如果使用 Ninja 或单配置生成器，生成的可执行文件通常位于：

```text
build-mtd-release\sanpao15_cli.exe
```

下文命令使用：

```text
.\build-mtd-release\sanpao15_cli.exe
```

作为示例。若你的构建器输出到 `Release` 子目录，请相应替换路径。

## 准备表库

完整 WDL / MTD 表库不包含在 Git 仓库和默认 GitHub Release 中。

要使用完整推荐，需要准备：

```text
build\prod-layers            # WDL .s15res 表库目录
build\mtd-lowmem-k6-t16      # MTD .s15mtd 表库目录
```

如果没有表库，仍然可以编译项目和启动前端，但完整推荐、MTD-aware 排序和表库查询功能无法给出完整结果。

完整表库体积较大：

- WDL `.s15res`：约 4.37 GiB。
- MTD `.s15mtd`：约 26.25 GiB。
- 合计：约 30.62 GiB。

请不要把 `.s15res`、`.s15mtd`、`build/`、`build-mtd-release/`、`ui/dist/` 或 `node_modules/` 提交到 Git。

## 编译

本项目根目录提供 `CMakeLists.txt`，要求：

- CMake 3.20+
- 支持 C++20 的编译器

常用构建命令：

```powershell
cmake -S . -B build-mtd-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-mtd-release --config Release
```

根目录 `package.json` 也提供了便捷脚本：

```powershell
npm run configure
npm run build
npm run test
npm run ui:build
```

注意：根目录 npm 脚本默认使用 `build/` 目录；如果使用这些脚本，请把本文示例中的 `build-mtd-release` 路径相应替换为 `build`。

UI 是 Vite + TypeScript 项目，位于 `ui/`：

```powershell
npm --prefix ui install
npm --prefix ui run build
npm --prefix ui run dev
```

## 运行本地 UI

完整 WDL + MTD 推荐模式：

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --serve-ui `
  --tablebase-dir build\prod-layers `
  --mtd-dir build\mtd-lowmem-k6-t16 `
  --mtd-store mmap `
  --host 127.0.0.1 `
  --port 8787 `
  --ui-dir ui\dist `
  --open
```

参数说明：

- `--tablebase-dir` 指向 WDL `.s15res` 表库目录。
- `--mtd-dir` 指向 MTD `.s15mtd` 表库目录。
- `--mtd-store mmap` 使用内存映射读取 MTD 表库。
- `--ui-dir` 指向 Web UI 构建产物目录。
- `--open` 启动后用默认浏览器打开本地地址。

如果 `8787` 被占用或被 Windows 保留，可以换成其它端口，例如：

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --serve-ui `
  --tablebase-dir build\prod-layers `
  --mtd-dir build\mtd-lowmem-k6-t16 `
  --mtd-store mmap `
  --host 127.0.0.1 `
  --port 18787 `
  --ui-dir ui\dist `
  --open
```

可用下面的命令查看 Windows TCP 保留端口：

```powershell
netsh interface ipv4 show excludedportrange protocol=tcp
```

WDL-only 后端模式：

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --serve-ui `
  --tablebase-dir build\prod-layers `
  --host 127.0.0.1 `
  --port 8787 `
  --ui-dir ui\dist `
  --open
```

浏览器文件模式只支持 WDL `.s15res` 查询；要使用 MTD-aware 推荐，请使用本地后端并传入 `--mtd-dir`。

## 表库说明

WDL 是 Win/Draw/Loss 表库，保存每个合法局面的胜 / 和 / 负结果：

- `CannonWin`：炮方胜。
- `SoldierWin`：兵方胜。
- `Draw`：和棋。

MTD 是 Material Target Distance 表库，保存更细的推荐依据：

- `materialTarget`：材料目标。
- `guaranteeDistance`：达到目标或胜利的保证步数。

MTD 用于在同一 WDL 结果层级内继续排序着法，不会跨越胜负层级改变 WDL 的首要选择。

完整表库文件很大，不随 Git 仓库和普通 GitHub Release 分发：

- WDL `.s15res`：约 4.37 GiB。
- MTD `.s15mtd`：约 26.25 GiB。
- 合计：约 30.62 GiB。

## MTD 推荐说明

WDL 只能说明某个局面是炮胜、兵胜或和棋。MTD 进一步回答：

- 在炮胜局面中，炮方多久能保证胜利。
- 在兵胜局面中，兵方多久能保证围死炮。
- 在和棋局面中，兵方最优能保住多少兵，炮方最多还能吃多少兵。

推荐规则：

- 先按 WDL 结果选择，不跨越胜负层级。
- 同一 WDL 层级内，再按 MTD 材料目标和保证步数排序。
- MTD 不完整时，UI 和 API 不能假装使用完整 MTD 推荐，会回退到 WDL-only 推荐。

README 中的“保证步数”对应程序内部的 ply，即单方一次行动计一步。

## CLI 用法

### 启动本地 UI

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --serve-ui `
  --tablebase-dir build\prod-layers `
  --mtd-dir build\mtd-lowmem-k6-t16 `
  --mtd-store mmap `
  --ui-dir ui\dist
```

### 查询 WDL 表库

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --query-tablebase build\prod-layers `
  --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" `
  --moves `
  --json
```

### 查询 MTD 表库

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --query-mtd build\mtd-lowmem-k6-t16 `
  --wdl-dir build\prod-layers `
  --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" `
  --moves `
  --json
```

### inspect MTD

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --inspect-mtd build\mtd-lowmem-k6-t16\layer-15.s15mtd
```

### verify MTD

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --verify-mtd-layer 15 `
  --wdl-dir build\prod-layers `
  --mtd-dir build\mtd-lowmem-k6-t16 `
  --sample 10000 `
  --threads 16 `
  --lower-mtd-store mmap `
  --wdl-store mmap
```

### inspect / validate WDL

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --inspect-res build\prod-layers\layer-15.s15res

.\build-mtd-release\sanpao15_cli.exe `
  --validate-res build\prod-layers\layer-15.s15res
```

### 路线探索

```powershell
.\build-mtd-release\sanpao15_cli.exe `
  --explore-tablebase build\prod-layers `
  --position "SSSSS/SSSSS/SSSSS/...../.CCC. c" `
  --max-plies 100 `
  --json
```

项目也包含完整表库生成命令，例如 `--solve-layer-range`、`--solve-mtd-range`。生成完整表库耗时长，占用磁盘和内存较大；普通用户不需要立刻重新生成完整表库。GitHub Release 默认不包含完整表库。

## 本地 API 示例

启动 `--serve-ui` 后，可以直接访问本地 HTTP API。

状态接口：

```powershell
Invoke-RestMethod http://127.0.0.1:8787/api/status |
  ConvertTo-Json -Depth 20
```

推荐接口：

```powershell
Invoke-RestMethod `
  -Uri http://127.0.0.1:8787/api/recommend `
  -Method Post `
  -ContentType "application/json" `
  -Body '{"position":"SSSSS/SSSSS/SSSSS/...../.CCC. c"}' |
  ConvertTo-Json -Depth 20
```

主要接口：

- `GET /api/status`
- `POST /api/query`
- `POST /api/recommend`
- `POST /api/compare-sides`
- `POST /api/explore`

其中 `/api/explore` 是 WDL 路线探索，不代表 MTD 最优路线。

## Web UI

Web UI 位于 `ui/`，使用 TypeScript 和 Vite 构建。

主要能力：

- 棋盘交互与局面编辑。
- 当前局面胜负状态。
- 推荐下一步。
- MTD-aware 推荐提示。
- 合法着法分组。
- 双方先走比较。
- WDL 路线探索。
- 表库状态与高级信息。

开发模式：

```powershell
npm --prefix ui run dev
```

构建静态资源：

```powershell
npm --prefix ui run build
```

开发服务器只启动前端；要使用完整 WDL + MTD 推荐，请运行本地 C++ 后端。

## 项目结构

```text
.
├── backend/              # 本地 HTTP 后端
├── cli/                  # sanpao15_cli 命令行入口
├── core/                 # 规则、局面、记法和合法着法生成
├── docs/                 # 设计说明和表库文档
├── solver/               # dense index、WDL、MTD、验证和 mmap 支持
├── tests/                # C++ 测试
├── third_party/          # 第三方头文件依赖
├── ui/                   # TypeScript Web UI
├── CMakeLists.txt        # C++ 构建配置
├── package.json          # 根目录便捷脚本
└── README.md
```

## 验证与测试

UI 构建：

```powershell
npm --prefix ui run build
```

C++ 测试：

```powershell
ctest --test-dir build-mtd-release --output-on-failure
```

如果使用 Visual Studio 多配置生成器，并且测试可执行文件在 `Release` 配置下构建，可以先运行：

```powershell
cmake --build build-mtd-release --config Release --target sanpao15_tests
ctest --test-dir build-mtd-release --output-on-failure -C Release
```

表库文件可以用 CLI 的 inspect / verify 命令检查：

```powershell
.\build-mtd-release\sanpao15_cli.exe --inspect-res build\prod-layers\layer-15.s15res
.\build-mtd-release\sanpao15_cli.exe --inspect-mtd build\mtd-lowmem-k6-t16\layer-15.s15mtd
```

## Release 说明

`v1.0.0` 是项目第一个正式版本。GitHub Release 默认只包含源码和说明，不包含完整表库文件。

完整表库体积较大：

- WDL 表库约 4.37 GiB。
- MTD 表库约 26.25 GiB。
- 合计约 30.62 GiB。

如果将来提供预生成表库，应单独说明下载方式、校验和、生成版本和兼容的 ruleset hash。

## 许可证

当前仓库尚未包含独立 `LICENSE` 文件。正式开源分发前建议补充许可证文件。

第三方依赖说明见 [docs/third-party.md](docs/third-party.md)。
