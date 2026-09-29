# Fallout 2 Community Edition（fork：pipboy-server）

> 英文版见 [README.en.md](README.en.md)。

> 本仓库是 **[alexbatalov/fallout2-ce](https://github.com/alexbatalov/fallout2-ce)** 的
> 个人改动分支（`pipboy-server`），在原版“辐射 2 社区重制版”引擎之上叠加了三大功能：
> **(a) Pip-Boy 链接服务器**、**(b) 原生手柄输入 + 覆盖层 + 屏幕键盘**、**(c) TrueType
> 字体后端**。其它引擎行为、玩法与上游一致。完整的上游安装/构建说明见上游 README；
> 本文档聚焦本分支的改动与使用方法。

上游原 README 的通用安装（Windows/Linux/macOS/Android/iOS 的掉落式可执行文件、
`fallout2.cfg` / `f2_res.ini` / `ddraw.ini` 配置）仍然适用，下面仅在“安装指南”中
给出**构建分支改动所需的要点**，以及本分支独有的配置项。

---

## 1. 项目背景

《辐射 2》社区重制版让游戏能相对无摩擦地跑在多个平台上。为了在双屏掌机上获得
“主机屏玩游戏、副屏跑 Pip-Boy 终端”的体验，本分支在引擎内新增了一个**本机 TCP 服务
器**，把游戏状态（S.P.E.C.I.A.L.、技能、物品、状态、地图、任务、perk 等）以协议帧推送给
配套的 [`pipboy-client`](https://github.com/chengdh/pipboy-client) 应用。

此外，原版以鼠标为核心的操作在触屏/手柄上极不友好，因此本分支还加入了：
- **原生 SDL 手柄输入**与 Fallout 风格的准星/光标覆盖层、屏幕键盘，使掌机可用手柄游玩；
- **TrueType 字体后端**（FreeType + iconv），以矢量字体替代内置位图字体，并支持中文等资源。

---

## 2. 参考的开源项目

- **[alexbatalov/fallout2-ce](https://github.com/alexbatalov/fallout2-ce)**：上游引擎，
  本分支的所有游戏逻辑与构建系统均在此基础上叠加。
- **SDL2 / SDL_GameController**：手柄输入与跨平台窗口/事件的基础（默认 vendored 构建）。
- **FreeType + iconv**：TrueType 字体渲染与 8 位编码（GBK）→ Unicode 转码。
- **[SDL_GameControllerDB](https://github.com/gabomdq/SDL_GameControllerDB)**：手柄映射
  数据库（`third_party/controller-db/gamecontrollerdb.txt`，随可执行文件一起拷贝）。
- **Zpix 像素字体**：手柄覆盖层在小字号下自绘中文时使用的 12×12 子集
  （`src/gamepad_cjk.h`，由 `tools/gen_gamepad_cjk.py` 从中文串生成）。
- **Fallout 4 伴侣应用协议**：Pip-Boy Link 的帧格式（长度前缀 + 类型字节 + 载荷）借鉴自
  F4 伴侣应用，但载荷改为 JSON（详见 `PIPBOY.md`）。

---

## 3. 安装指南

### 3.1 构建（含分支改动）

引擎使用 **CMake**（C++17），依赖默认 **vendored** 从 `third_party/` 构建
zlib / SDL2 / FreeType / iconv；非 vendored 时需系统提供 `SDL2 >= 2.26`。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- **平台**：Windows（`WIN32`）、Linux、macOS（通用 `x86_64;arm64`，部署目标 10.13）、
  **Android**（编译为 `SHARED` 库，供 `pipboy-client` 的 Android 壳打包）、
  **iOS**（arm64 bundle）。
- **手柄相关开关**：`FALLOUT_BUILD_CONTROLLER_TESTS`、`FALLOUT_CONTROLLER_SMOKE_DRIVER`
  （含 `tests/gamepad_test.cc` 冒烟测试）。
- **TrueType 字体**：需要 FreeType + iconv（已 vendored）。

> 仅想游玩（不编译）：从本仓库 **Release** 下载对应平台的构建（Windows
> `fallout2-ce-windows-<arch>.zip`、macOS `Fallout II Community Edition.dmg`、Linux
> `fallout2-ce-linux-<arch>.tar.gz`、Android `fallout2-ce-android.apk`、iOS
> `fallout2-ce-ios.ipa`），再按下方 **3.2 各平台安装与运行** 把原版资产与字体放入对应目录即可。
> **你必须拥有正版《辐射 2》才能游玩**（从 GOG / Steam / Epic 等合法渠道获取）。

### 3.1.1 自动构建（CI）
仓库内置多套 GitHub Actions：
- **`build-android.yml`**：任意分支 `push` / `pull_request` 即构建 Android APK（用
  `android-actions/setup-android` 安装与 `os/android/app/build.gradle` 完全匹配的
  NDK `23.2.8568313` / `build-tools;32.0.0` / `cmake;3.22.1`，产物为可下载 artifact）。
- **`ci-build.yml`**：仅在 `push` 到 `main` 时运行全平台构建（静态分析、格式检查、
  Android / iOS / Windows / Linux / macOS），其中 Android job 需自行配置 NDK 环境。
- **`build-and-release.yml`**：`push` 到 `main` / `pipboy-server` 时创建 GitHub Release
  并上传各平台产物（每次 push 都会生成一个带时间戳的 release）。

### 3.2 各平台安装与运行（原版资产 / 字体放置）

引擎在运行时把“工作目录”作为唯一数据根：所有 `master.dat`、`critter.dat`、`patch000.dat`、
`data/`、`fallout2.cfg`、`fonts/`、`gamecontrollerdb.txt` 都**相对于它**解析（Windows 下为
exe 所在目录；macOS / iOS / Android 由引擎启动时空 `chdir` 到对应目录；Linux 为启动时的
当前目录）。因此核心步骤对各平台完全一致：

1. 安装主程序（见下，各平台方式不同）；
2. 把正版《辐射 2》的 `master.dat`、`critter.dat`、`patch000.dat` 以及 `data/` 目录放到
   该平台的“游戏数据目录”（见下）；
3. 把本仓库 / Release 里的 `fonts/` 目录（含 `fonts/english/`、`fonts/chs/`）放到**同一目录**；
4. 首次启动后会在数据目录生成 `fallout2.cfg`，按需编辑（见 3.3 启用 Pip-Boy 服务）；
5. 可选：把 `third_party/controller-db/gamecontrollerdb.txt` 放到同一目录以启用手柄（见 4.1）。

下面分别说明每个平台的“主程序装哪、数据目录在哪、资产/字体放哪”。

#### Windows
- **主程序**：把 `fallout2-ce-windows-<arch>.zip` 解压到任意目录，得到 `fallout2-ce.exe`
  （已内置 SDL2 / FreeType / libiconv，单文件自包含，无需安装任何运行时）。
- **原版资产 / 字体**：把 `master.dat`、`critter.dat`、`patch000.dat`、`data/` 以及 `fonts/`
  **直接放在 `fallout2-ce.exe` 同一目录**（即解压目录）。
- 双击 `fallout2-ce.exe` 即可；所有路径都按 exe 所在目录解析。

#### macOS
- **主程序**：挂载 `Fallout II Community Edition.dmg`，把 `Fallout II Community Edition.app`
  拖入“应用程序”。引擎启动时会把工作目录切到
  `…/Fallout II Community Edition.app/Contents/MacOS/`，所以资产要放在这里。
- **原版资产 / 字体**：右键 `.app` → “显示包内容” → 进入 `Contents/MacOS/`，把
  `master.dat`、`critter.dat`、`patch000.dat`、`data/`、`fonts/` 都放进这个 `MacOS` 文件夹。
- 之后从“应用程序”正常启动 `.app`。

#### Linux
- **主程序**：把 `fallout2-ce-linux-<arch>.tar.gz` 解压到任意目录，得到可执行文件
  `fallout2-ce`（动态链接系统 SDL2 / zlib，需先装：`sudo apt install libsdl2-2.0-0 zlib1g`）。
- **数据目录 / 启动方式**：新建一个游戏目录（例如 `~/fallout2/`），把资产放进去，然后
  **在该目录内**启动 —— 因为 Linux 下工作目录 = 启动时的当前目录：
  ```bash
  mkdir -p ~/fallout2 && cd ~/fallout2
  # 把 master.dat critter.dat patch000.dat data/ fonts/ 放进 ~/fallout2/
  ~/path/to/fallout2-ce   # 从 ~/fallout2 当前目录运行
  ```
- **原版资产 / 字体**：全部放在启动时的当前目录（上面的 `~/fallout2/`）。

#### Android
- **主程序**：把 `fallout2-ce-android.apk` 通过 `adb install -r …` 或手机文件管理器侧载安装。
- **数据目录**：引擎启动时 `chdir` 到应用的外部存储目录
  `/sdcard/Android/data/com.alexbatalov.fallout2ce/files/`
  （Debug 包为 `…com.alexbatalov.fallout2ce.debug/files/`）。
- **原版资产 / 字体**：用 `adb push` 或手机文件管理器把 `master.dat`、`critter.dat`、
  `patch000.dat`、`data/`、`fonts/` 放进该 `files/` 目录，例如：
  ```bash
  adb push master.dat critter.dat patch000.dat \
    /sdcard/Android/data/com.alexbatalov.fallout2ce/files/
  adb push data fonts \
    /sdcard/Android/data/com.alexbatalov.fallout2ce/files/
  ```
- 启动 App 即进入游戏，无需 root。

#### iOS
- **主程序**：把 `fallout2-ce-ios.ipa` 通过侧载（AltStore / Sideloadly / 企业签名等）安装到设备。
- **数据目录**：引擎启动时 `chdir` 到本应用的 **Documents** 目录；通过“文件”共享
  （macOS Finder 的“文件共享” / iTunes / 第三方工具）把资产拷入。
- **原版资产 / 字体**：把 `master.dat`、`critter.dat`、`patch000.dat`、`data/`、`fonts/`
  放入该 App 的 Documents 目录。
- 启动 App 进入游戏。

### 3.3 开启 Pip-Boy 链接服务器

编辑游戏目录下的 `fallout2.cfg`，新增/确认 `[pipboy]` 段：

```ini
[pipboy]
enabled=1
bind_address=127.0.0.1
port=27000
sample_interval_ms=250
```

- `enabled`：是否默认开启服务（详见下方“注意事项”中关于 TEST BUILD 默认值的说明）。
- `bind_address`：`127.0.0.1` / `localhost` / 留空 = 仅绑回环；设为其它值会绑 `0.0.0.0`
  暴露到局域网（有安全风险）。
- `sample_interval_ms`：采样间隔，默认 250ms；首次全量、之后仅推变化键。

运行时也可直接按 **Ctrl+P** 即时开关（会弹出中文 toast 提示当前状态）。

---

## 4. 使用方法

### 4.1 手柄使用方法

本分支新增了基于 **SDL_GameController** 的原生手柄支持（说明见 `CONTROLLER.md`）。
手柄映射数据库随可执行文件一起发布（`gamecontrollerdb.txt`）。

**默认按键绑定**（`src/gamepad.cc` 的 `defaults()`）：

| 按键 | 功能 |
|---|---|
| A / Cross | 左键点击 / 拖拽 |
| B / Circle | 右键点击 / 切换光标模式 |
| X / Square | Enter（确认） |
| Y / Triangle | 打开 Actions 面板 |
| Back / Share / Minus | 控制面板 |
| Start | Esc（关闭/菜单） |
| LB（按住） | 奔跑 |
| RB | 打开背包（INVENTORY） |
| L3（左摇杆按下） | 切换“角色移动 / 指向点击”模式 |
| R3（右摇杆按下） | 屏幕键盘 |

另有 27 项动作映射把按键对应到原版屏幕热键，例如 PIP-BOY→`P`、INVENTORY→`I`、
SKILLDEX→`S`、AUTOMAP→`TAB`、SAVE→`F4` 等。

**移动与相机**：
- 左摇杆直接驱动角色移动（含部分偏转走、全偏跑、hex 步动画、相机跟随）。
- 右摇杆平滑平移世界相机，并优先于跟随。
- 世界输入**仅在玩家输入轮询时启用**，绝不动敌人 AI 或 UI。

**自定义**（`controller.ini`，优先读本地目录，否则读 SDL 偏好目录 `FalloutCE/`）：
可保存 `speed / deadzone / precision / scroll_speed / invert_scroll / swap_sticks /
labels / hints / borderless / direct_movement`，以及 9 个可重绑按键 `button_N=<action>`。
`Back` 键永远保留用于恢复默认。

**覆盖层本地化**：手柄准星/提示覆盖层跟随 `fallout2.cfg` 的 `[system] language`，
目前仅**英文与中文**做了翻译；中文使用内嵌的 Zpix 12×12 像素字子集自绘（小字号下
普通字体不可读）。

### 4.2 多语言配置指南与限制

**配置开关**：主语言由 `fallout2.cfg` 的 `[system] language` 控制（默认 `english`）。
该值同时驱动两件事：
1. 文本查找：`text\<language>\*.msg`；
2. 字体查找：`fonts/<language>/font.ini`。

Android 资源示例（`os/android/.../f2extra/fallout2.cfg`）中设为 `language=chs`。

**TrueType 字体后端（本分支新增）**：用 FreeType 从 `.ttf/.otf/.ttc` 渲染接口字体，
替代内置位图 `.aaf`。启动顺序为：先占用接口字体区间 `100..104`，失败才回退到内置 `.aaf`。
**没有运行时开关——是否存在 `font.ini` 即为开关。**

**字体查找顺序**：`fonts/<language>/font.ini` → `fonts/font.ini`（中文社区版共用的布局）
→ 都没有才用内置字体。随包仅有 `english` 与 `chs` 两套
（`src/fonts/english/font.ini`、`src/fonts/chs/font.ini`）。

**`font.ini` 主要字段**：`maxHeight / maxWidth / lineSpacing / heightOffset / wordSpacing /
letterSpacing / fileName / warpMode / encoding`。其中：
- `encoding`：游戏字符串的 **8 位编码**（如 `GBK`），用 iconv 转成 Unicode。
- `warpMode=1`：关闭“按词换行”、改为**按字换行**（CJK 无空格，必须按字断行）。
- 接口字体 ID 映射：`100→font0`（主菜单/标题）、`101→font1`（对话框/描述）、
  `102→font2`、`103→font3`（DONE/YES）、`104→font4`（标题）；`105` 未用。

**关键限制（GBK 与 UTF-8）**：
- 游戏的 `.msg` 文本是 **8 位编码字节**（chs 下为 GBK），**不是 UTF-8**。若 TrueType
  后端缺失/失效，内置 `.aaf` 单字节字体会把这些 GBK 字节逐字当 Latin 画出 → 经典“乱码”。
- 因此 `font.ini` 的 `encoding` **必须匹配** `.msg` 的实际编码（随包两份示例都写
  `encoding=GBK`，包括 english 那一份——因为引擎文本管线本身就是 8 位字节）。
- **UTF-8 只出现在 Pip-Boy 协议里**：服务端用 `gameTextToUtf8()`（iconv `GBK→UTF-8`）
  把游戏串转成合法 UTF-8 后再放入 JSON 发送，客户端无需再做转码。
- 其它已知限制：部分 UI 仍写死 12px 行高/像素值，改 `maxHeight/lineSpacing` 不会缩放
  这些界面（会重叠或留缝）；只有 `english`、`chs` 两套字体随包，新增语言需同时补
  `fonts/<language>/` 和 `text\<language>\`；`.aaf` 仍注册于 ID `0..99`，仅接口区间
  `100..104` 被接管，硬编码低 ID 的脚本字仍用位图字体、不被本地化。

---

## 5. 编程中对 AI 的使用

本分支的代码改动由人主导，AI（Claude，经由 CodeBuddy 编程助手）主要承担**文档与协议
梳理**工作：

- **协议文档 `PIPBOY.md`**：由 AI 起草并补全，定义了帧格式、消息类型、字段语义、
  协议版本机制与“可选字段缺失时客户端降级”的约定；配套说明见 `PIPBOY_CLIENT.md`。
- **数据映射梳理**：AI 梳理了 `src/pipboy_server.cc` 的 `collectSnapshot()` 中各数据类
  （S.P.E.C.I.A.L.、Derived、Conditions、Skills、Inventory、Armor、Map、Quests、Perks）
  的采集点与字段名，并写入文档，使客户端实现有确定依据。
- **调试工具**：`tools/pipboy_mock_server.py`、`pipboy_probe*.py` 等用于在无游戏时验证
  协议，由 AI 辅助编写。
- 对 **手柄子系统（`CONTROLLER.md`）** 与 **TrueType 字体（`FONTS.md`）**，AI 负责把既有
  实现文档化、归纳限制；这两项功能本身为 modified build 原有，非 AI 生成。

AI 产出的是文档、协议说明与辅助脚本；引擎逻辑、手柄映射与构建系统的最终实现仍由人完成
并验收。

---

## 注意事项（fork 特有）

- **`[pipboy] enabled` 的默认值**：当前 `settings.h` 中默认 `enabled=true`，这是
  **TEST BUILD** 设置（为了让无键盘的 Android 设备能自动启动服务）。正式发布前应改回
  `false`，改由 `fallout2.cfg` 或 Ctrl+P 显式开启（设计上应为 opt-in）。
- **服务器只接受单一客户端**：不要同时连接两个 `pipboy-client` 实例。
- **文档与代码偏差**：`PIPBOY.md` 写“15 秒无活动踢客户端”，而代码实际 recv 超时为 30 秒，
  以代码为准（后续会修正文档）。
- **许可**：源代码沿用上游的 [Sustainable Use License](LICENSE.md)。任何发布/再分发须
  遵守该许可证，且你必须拥有正版《辐射 2》才能游玩。
