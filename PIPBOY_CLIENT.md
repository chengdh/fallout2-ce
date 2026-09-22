# Pip-Boy Client 实施计划

服务端（`src/pipboy_server.cc`，协议见 `PIPBOY.md`）已完成。本文档是**第二阶段**
的实施依据：独立 App，运行在下屏，界面来自 `rzx007/Pip-Boy`。

---

## 0. 三个必须先定死的结论

### 结论一：rzx007/Pip-Boy 的"数据层"不是游戏数据

克隆后实测 `apps/web/src`：

| 页面 | 实际内容 |
|---|---|
| `inv` | **待办清单**（`stores/inventory.ts` 的 `Task{name,description,weight,value,completed}`），默认任务 Stimpak/RadAway/Nuka-Cola 只是主题装饰 |
| `stat` | `level/maxHp/maxAp`，HP 由**笔记本电池百分比**驱动（`setFromBattery(batteryPercent)`） |
| `data` | 硬编码日志 + AI 聊天（`TerminalChat`）+ 黑客小游戏（`TerminalHacker`） |
| `map` | 静态 `lib/map-data.ts`（New Vegas 地点/路线） |
| `radio` | 电台播放器 |

它是一个 **Pip-Boy 皮肤的个人仪表盘**，不是游戏数据查看器。

**这反而是好消息**：我们本来就要换掉数据层，所以"手术"不需要迁就原有数据格式，
只需保留视觉层，数据面全部对接 `PIPBOY.md` 的快照。

### 结论二：WebView 里的 JavaScript 无法开裸 TCP

我们的协议是 TCP 帧。`WebView` 中的 JS 只能走 HTTP/WebSocket，**不能** `connect()`
一个 TCP socket。必须二选一：

| 方案 | 做法 | 评价 |
|---|---|---|
| **A · 原生桥接** | Kotlin 侧用 `Socket` 连 127.0.0.1:27000，`addJavascriptInterface` 把解出的 JSON 推给 JS | 服务端零改动，协议原样保留 |
| B · WebSocket | 服务端额外实现 WS 握手 + 帧封装（约 +200 行 C++） | 桌面浏览器可直接调试 UI，不必打包 APK |

**建议：主链路走 A，开发期用 B。**
具体做法——前端只依赖一个 `Transport` 接口，开发时注入 `WebSocketTransport`
（连 `tools/pipboy_mock_server.py` 的 WS 端口），打包时注入 `JsBridgeTransport`
（由 Kotlin 喂数据）。两种 transport 交付给上层的数据完全一致。

### 结论三：静态导出必须先删掉服务端路由

`next output: 'export'` 要求所有页面都是客户端组件。`(pipboy)` 目录**已经全部是
`"use client"`**，这一半没问题；问题在 `apps/web/src/app` 下其余目录：
`api/`（路由处理器）、`dashboard/`、`login/`、`forgot-password/` 都依赖
better-auth 与 drizzle/libsql，必须删除。

---

## 1. 静态导出手术步骤

```bash
git clone https://github.com/rzx007/Pip-Boy.git pipboy-client
cd pipboy-client
```

### 步骤 1 — 瘦身 workspace

删除（依赖 auth/db/Node 专有 API，浏览器与静态导出都用不到）：

```
apps/web/src/app/api            # 路由处理器
apps/web/src/app/dashboard      # 依赖 drizzle
apps/web/src/app/login
apps/web/src/app/forgot-password
packages/auth                   # better-auth
packages/db                     # drizzle + libsql
packages/env                    # 服务端环境变量校验
```

保留：`packages/config`（TS/tailwind 配置）、`apps/web/src/components`、`stores`、`lib`。

### 步骤 2 — 剥离依赖

`apps/web/package.json` 删除：
`@Pip-Boy/auth`、`@Pip-Boy/env`、`@ai-sdk/*`、`ai`、`better-auth`、`@libsql/client`、
`libsql`、`systeminformation`（Node 专有，浏览器端无法运行）、`shadcn`（CLI，非运行时）。

保留：`next`、`react`、`zustand`、`motion`、`@base-ui/react`、`lucide-react`、
`tailwindcss@4`、`use-sound`、`react-svg-pan-zoom`、`@panzoom/panzoom`、`sonner`、
`class-variance-authority`、`clsx`、`tailwind-merge`。

### 步骤 3 — 删除/替换的组件

| 文件 | 处理 |
|---|---|
| `components/pipboy/AuthProvider.tsx` | **删**，layout 里去掉包裹 |
| `PipSignInForm.tsx` / `PipSignUpForm.tsx` / `ForgotPasswordForm.tsx` | **删**（含 `components/` 下的 `sign-in-form.tsx`、`sign-up-form.tsx`、`user-menu.tsx`） |
| `TerminalChat.tsx` | **删**（AI SDK）；`data` 页改为展示 `Quests` / 任务日志 |
| `PipMap.tsx` + `lib/map-data.ts` | **保留结构，换数据**：把 `Location` 坐标源换成服务端 `Map.*`，或先留静态图占位 |
| 其余 CRT 视觉组件 | **原样保留** |

### 步骤 4 — 开启静态导出

`apps/web/next.config.ts`：

```ts
const nextConfig: NextConfig = {
  output: "export",          // 产出纯静态 out/
  images: { unoptimized: true },  // 静态导出不能用 next/image 优化
  trailingSlash: true,       // WebView 用 file:// 打开时路径更稳
};
```

构建：`pnpm build` → `apps/web/out/`。

### 步骤 5 — 布局尺寸适配下屏

AYN Thor 下屏实测 **1240×1025 px / 538×444 dp / 369dpi，宽高比 1.21**（近正方形）。
rzx007 的布局按常规宽屏设计，需要：

- 根容器固定 `1240×1025`，`overflow: hidden`
- 底部 **55px** 是系统栏，不要放可点控件
- 字号不小于 `12sp`（369dpi 下 12px CSS ≈ 物理 2.1mm，可读）

---

## 2. 数据接入

### 前端状态树

新增 `stores/link.ts`，把服务端扁平快照还原成嵌套对象后写入 zustand：

```ts
type LinkState = {
  status: "connecting" | "online" | "offline";
  snapshot: Record<string, unknown>;   // 由 "A.B.C" 展平成嵌套对象
  applyDelta: (delta: Record<string, unknown>) => void;
  applyUpdate: (full: Record<string, unknown>) => void;
};
```

键路径 → 嵌套对象的还原规则：`{"PlayerInfo.CurrHP": 63}` → `{PlayerInfo:{CurrHP:63}}`。
`null` 视为删除该键。

### 字段映射（当前 schema → 五个页面）

| 页面 | 数据 |
|---|---|
| `stat` | `Special.*`、`Derived.*`、`Conditions.*`、`PlayerInfo.CurrHP/MaxHP/CurrAP/MaxAP`、`PlayerInfo.XPLevel/XPProgressPct` |
| `inv` | `Inventory.items[]`（name/count/weight）、`Inventory.caps`、`Inventory.weapon/armor`、`PlayerInfo.CurrWeight/MaxWeight` |
| `map` | `Map.Name/City/Elevation/IsWorldmap` |
| `data` | `Quests[]`（服务端待实现）+ `PlayerInfo` 时间/日期 |
| `radio` | 暂无数据源，保留 UI 作为占位 |

### 传输抽象

```ts
export interface Transport {
  start(onFull: (o: object) => void, onDelta: (o: object) => void, onStatus: (s: string) => void): void;
  stop(): void;
}
```

- `WebSocketTransport` —— 开发用，连 mock server 的 WS 端口
- `JsBridgeTransport` —— 打包用，监听 `window.PipBoyLink` 注入

### 客户端独立可运行（硬要求）

没有服务端时 App 必须正常启动、显示"等待连接"，并 2s 指数退避重试。
游戏退出后自动回到等待态。**不依赖游戏包名、不被游戏拉起。**

---

## 3. Android 壳

- `minSdk 24`、`targetSdk 34`，`INTERNET` 权限
- `Activity` 内 `WebView`，`WebViewAssetLoader` 以 `https://appassets.androidplatform.net/`
  提供 `out/` 静态资源（避免 `file://` 的 CORS 限制）
- Kotlin 侧 `Socket` 连 `127.0.0.1:27000`，解析 `uint32 LE + uint8` 帧，
  JSON 交给 `evaluateJavascript` / `addJavascriptInterface`
- 副屏投递：`DisplayManager` 取 `FLAG_PRESENTATION` 的 display，
  用 `ActivityOptions.setLaunchDisplayId()` 或 `Presentation` 启动
- 生命周期：游戏在后台时断开 socket 并停止重试，避免空转耗电

---

## 4. 许可证

- 游戏侧：Sustainable Use License v1.0（承袭 FOR:CE），需声明"已修改"
- 客户端：rzx007 仓库 **README 标 MIT 但未附 LICENSE 文件** —— 公开发布前必须向作者确认，
  或改用明确授权的 UI 来源（如 MIT 的 `thapelomagqazana/pipboy-web-app`）
- 两端是独立程序，TCP 通信不构成衍生关系，许可证不互相传染

---

## 5. 执行顺序

1. mock server 起 WS 网关（开发数据源）
2. 前端瘦身 → 静态导出跑通 → 桌面浏览器连 mock 验证五页渲染
3. 替换 `stores/` 数据层（待办/电池 → 游戏快照）
4. 下屏尺寸适配（1240×1025）
5. Android 壳 + Kotlin TCP 桥
6. 真机：上屏游戏、下屏 Pip-Boy
