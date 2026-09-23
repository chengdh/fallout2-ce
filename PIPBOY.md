# Pip-Boy Link Protocol (F4 framing + JSON payload)

`fallout2-ce` 作为**服务端**，Pip-Boy App 作为**客户端**，通过本机 TCP 通信。
本协议只定义"线上说什么"，不定义"谁连谁"——两端不绑定包名、不互相拉起。

设计取舍：沿用 Fallout 4 Pip-Boy 协议的**帧格式与消息语义**（`uint32 LE 长度 + uint8 type`、
"连接后推全量、之后推变化"），把载荷从 F4 的私有二进制换成 JSON。
代价：放弃直接对接官方 Bethesda App；收益：服务端从 ~1200 行降到 ~400 行，
且不需要实现 F4 的 id 分配器 / dirty 追踪 / remove 集合计算。

---

## 1. 传输层

| 项 | 值 | 说明 |
|---|---|---|
| 协议 | TCP | 有序、无丢包，省掉重传逻辑 |
| 默认地址 | `127.0.0.1:27000` | 仅本机；端口与地址可配置 |
| 并发 | **单客户端** | 沿用 F4 语义；第二个连接收到 `BUSY` 后断开 |
| 字节序 | Little-Endian | 与 F4 一致 |

> 绑定到 `0.0.0.0` 可让客户端跑在手机/PC 上，但会暴露局域网，默认不开。

## 2. 帧格式

```
+---------------+--------------+---------------------------+
| length:uint32 | type : uint8 | payload : length bytes    |
|  (LE)         |              |  (UTF-8 JSON, 无末尾 \0)   |
+---------------+--------------+---------------------------+
```

- `length` 只计 payload 字节数，**不含** 4 字节长度与 1 字节类型
- 单帧 payload 上限 **1 MiB**，超出视为协议错误并断开
- type 为 `0x00`(KEEPALIVE) 时 length 必须为 0，整帧恰好 5 字节，与 F4 一致

## 3. 消息类型

| type | 名称 | 方向 | 载荷 |
|------|------|------|------|
| `0x00` | KEEPALIVE | 双向 | 空（5 字节帧） |
| `0x01` | HELLO | C → S | JSON 握手请求 |
| `0x02` | WELCOME / BUSY | S → C | JSON 握手响应 |
| `0x03` | DATA_UPDATE | S → C | JSON **全量**快照 |
| `0x04` | DATA_DELTA | S → C | JSON **变化**字段 |
| `0x05` | COMMAND | C → S | JSON 指令（预留） |
| `0x06` | COMMAND_REPLY | S → C | JSON 指令回执（预留） |
| `0x07` | BYE | 双向 | 空 |

`0x00`/`0x02`/`0x03`/`0x05`/`0x06` 沿用 F4 编号语义；`0x01`/`0x04`/`0x07` 为本协议补充。

## 4. 握手

客户端连上后**必须**在 5 秒内发出 HELLO，否则服务端断开。

```jsonc
// C -> S  type 0x01
{ "magic": "PIPB", "protocol": 1, "client": "pipboy-droid/0.1.0", "caps": ["delta"] }
```

```jsonc
// S -> C  type 0x02  成功
{ "magic": "PIPB", "protocol": 1,
  "game": "fallout2-ce", "game_version": "1.3.0",
  "caps": ["delta", "update"], "sample_interval_ms": 250 }
```

```jsonc
// S -> C  type 0x02  已有人连接
{ "magic": "PIPB", "error": "busy", "reason": "another client is connected" }
```

```jsonc
// S -> C  type 0x02  版本不兼容
{ "magic": "PIPB", "error": "protocol_mismatch", "protocol": 1, "server_protocol": 1 }
```

规则：
- `magic` 必须为 `"PIPB"`，否则立即断开
- `protocol` 不一致 → 回 `protocol_mismatch` 并断开；客户端应提示"请升级 Pip-Boy"
- 握手成功后服务端立刻推一帧 `DATA_UPDATE`（全量）

## 5. 数据推送

服务端以 `sample_interval_ms`（默认 250ms）为周期采样游戏状态，并与上一次推送比较：

- 首次推送 → `DATA_UPDATE`（完整对象）
- 之后 → `DATA_DELTA`，只包含值发生变化的键；**若无变化则不推**（静默）
- 值为 `null` 表示该键被移除（当前 schema 不产生，保留语义以备扩展）

`DATA_DELTA` 是稀疏的，客户端应按键合并到自己维护的状态树上。

```jsonc
// S -> C  type 0x04
{ "PlayerInfo.CurrHP": 63, "Inventory.caps": 1420 }
```

> 键路径用 `.` 分隔，与全量快照的嵌套结构一一对应。

## 6. 心跳

- 服务端每 **2 秒**发一帧 KEEPALIVE
- 客户端**应**回一帧 KEEPALIVE（本协议不强制，便于浏览器/调试工具接入）
- 服务端在 **15 秒**内未收到该连接的任何字节 → 判定僵死并断开
- 客户端连续 **6 秒**未收到任何帧 → 视为连接丢失，回到"等待连接"状态并重连

## 7. 快照 Schema

顶层键名沿用 F4 协议，便于将来需要时映射回官方 App。

> **线上格式是扁平的**：`DATA_UPDATE` 与 `DATA_DELTA` 一律是
> `{"dotted.path": value}` 的**一层 map**，值保持原生 JSON 类型（数字就是数字，不是字符串）。
> 下面的嵌套结构只是同一份数据的**逻辑视图**，便于阅读；
> 客户端把 dotted 路径展开成树即可还原。
> 扁平让服务端的 diff 退化成 map 比较，无需 id 分配器与 dirty 追踪。

```jsonc
{
  "PlayerInfo": {
    "PlayerName": "Chosen One",
    "CurrHP": 63, "MaxHP": 78,          // int
    "CurrAP": 9,  "MaxAP": 9,           // 当前地图战斗时有效
    "CurrWeight": 128, "MaxWeight": 250,// pounds
    "Caps": 1420,                        // 瓶盖
    "XPLevel": 7, "XPProgressPct": 42.5, // float 0..100
    "Karma": 120, "Reputation": 0,
    "DateMonth": 6, "Day": 14, "Year": 2241,
    "TimeHour": 21,                      // 0..23
    "IsSneaking": false
  },
  "Special": {                           // SPECIAL 七维
    "Strength": 6, "Perception": 7, "Endurance": 5,
    "Charisma": 4, "Intelligence": 8, "Agility": 7, "Luck": 5
  },
  "Derived": {                           // 派生属性
    "ArmorClass": 12, "ActionPoints": 9, "CarryWeight": 250,
    "MeleeDamage": 3, "Sequence": 12, "HealingRate": 2,
    "CriticalChance": 8, "DamageThreshold": 3,
    "DamageResistance": 10, "RadiationResistance": 22, "PoisonResistance": 30
  },
  "Conditions": {                        // 部位/状态，F4 Stats 的对应项
    "HitPoints": 63, "Poison": 0, "Radiation": 12,
    "IsDead": false, "IsCrippled": false, "IsEncumbered": false
  },
  "Skills": {                            // 技能名 -> 数值
    "Small Guns": 95, "Big Guns": 40, "Energy Weapons": 55,
    "Unarmed": 30, "Melee Weapons": 45, "Throwing": 60,
    "First Aid": 70, "Doctor": 35, "Sneak": 50, "Lockpick": 65,
    "Steal": 25, "Traps": 20, "Science": 80, "Repair": 75,
    "Speech": 90, "Barter": 55, "Gambling": 15, "Outdoorsman": 40
  },
  "Inventory": {
    "caps": 1420,                        // 冗余于 PlayerInfo.Caps，F4 兼容
    "items": [                            // 玩家背包条目（平铺，不做 F4 分类树）
      { "name": "10mm Pistol", "count": 1, "weight": 3, "pid": 8 },
      { "name": "10mm AP",     "count": 96, "weight": 0, "pid": 9 }
    ],
    "weapon":  "10mm Pistol",             // 当前手持（右手）
    "armor":   "Leather Armor"
  },
  "Map": {
    "Name": "The Den",        // 当前地图显示名，worldmap 上为 "World Map"
    "City": "The Den",
    "Elevation": 0,
    "IsWorldmap": false,
    "PlayerX": 87,            // 玩家 200x200 六边形网格坐标（tile % 200）
    "PlayerY": 123,           // （tile / 200）；worldmap 上为 -1
    "Automap": "AAAA...",     // AUTOMAP.DB 解码位图，base64：10000 字节，
                              // 每字节一格（0 空/1 墙/2 布景），按行优先
                              // x + y*200；无 automap 条目时为 ""
    "WorldX": 173,            // 队伍世界坐标
    "WorldY": 122,
    "WorldW": 4900,           // 世界地图总尺寸（世界坐标）
    "WorldH": 4500,
    "Cities": [               // 仅含 KNOWN/VISITED 的城镇
      { "name": "The Den", "x": 294, "y": 181, "state": 2 }
    ]
  },
  "Perks": {                             // 已拥有的 Perk（rank>0）列表（JSON 数组字符串）
    "list": [
      { "name": "Awareness", "rank": 1, "description": "..." }
    ]
  },
  "Quests": {                            // 当前可见任务（gvar>=displayThreshold，JSON 数组字符串）
    "list": [
      { "id": 9, "name": "Arroyo", "description": "Kill the Evil Plants", "done": false }
    ]
  },
  "Server": {
    "UptimeSec": 123,
    "SampleIntervalMs": 250,
    "ConnectedClients": 1
  }
}
```

## 8. 版本演进

`protocol` 当前为 `1`。规则：
- **只增字段**：新增键不影响旧客户端（旧端忽略未知键）
- **不改语义**：已有键的类型/含义变更 → `protocol` 必须 +1
- 客户端遇到更高的 `server_protocol` → 提示升级，不要尝试解析

## 9. 安全边界

- 默认只绑 `127.0.0.1`，本机进程可见
- 无认证：本机环回场景下不做鉴权；一旦改为 `0.0.0.0` 必须视为暴露面
- 服务端默认**关闭**，未在设置里开启则不 `bind()`、不建线程、零开销
- 指令通道（type `0x05`）默认只接受只读指令；写操作（用物品/丢弃等）不在 v1 范围
