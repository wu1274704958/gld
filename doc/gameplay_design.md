# AoE Gameplay 系统设计

本文描述 `gld` 当前 AoE gameplay 层的设计与实现边界，覆盖单位定义、固定时间步、逻辑地图、寻路与局部避障、索敌与战斗、Squad/Formation、Projectile、生命周期、动画驱动及 AoE2 表现层桥接。

文档以当前代码为准。标为“当前限制”或“扩展点”的内容尚未实现，不应当作现有能力使用。

主要代码位置：

- Gameplay 公共接口：`aoe/include/aoe/AoeGameplay.hpp`
- Gameplay 实现：`aoe/src/AoeGameplay.cpp`
- Map 公共接口：`aoe/include/aoe/AoeMap.hpp`
- Map 实现：`aoe/src/AoeMap.cpp`
- AoE2 表现桥接：`aoe2_gameplay/include/aoe2_gameplay/Aoe2GameplayBridge.hpp`
- AoE2 表现桥接实现：`aoe2_gameplay/src/Aoe2GameplayBridge.cpp`
- 单位配置：`res/aoe_units/*.json`
- Map 配置示例：`res/aoe_maps/example.json`

## 1. 设计目标

Gameplay 层负责决定游戏事实，主要目标如下：

1. gameplay 状态不依赖渲染对象是否存在，也不依赖某一张动画是否成功加载；
2. 移动、索敌、攻击判定、伤害、死亡和回收都在固定时间步中执行，结果可重复；
3. gameplay unit 与 render unit 分离，一个 gameplay unit 可以实例化并驱动一个具体表现后端；
4. Map、寻路、局部转向、索敌、阵型和 Projectile 逻辑通过静态 Registry 接口扩展；
5. 大量单位场景避免逐查询全量动态障碍、逐单位临时分配及逐 primitive 渲染对象；
6. Entity 复用后，旧命令、旧 Projectile 和旧 Squad member 引用不能误命中新实例。

当前系统不是完整 RTS 仿真框架。它暂时不包含玩家选框、战争迷雾、建造、资源经济、网络同步、确定性跨平台浮点协议、全局 flow field 或完整 ORCA/RVO。

## 2. 模块边界与数据所有权

Gameplay 与表现层的关系如下：

```text
Unit JSON / Map JSON
        │
        ▼
┌─────────────────────────────────────────────┐
│ AoE Gameplay                               │
│                                            │
│ definition / hp / team / collider / state  │
│ command / target / path / movement / combat│
│ projectile / lifecycle / squad / formation │
└───────────────────┬─────────────────────────┘
                    │ 只同步状态、朝向、时间和位置
                    ▼
┌─────────────────────────────────────────────┐
│ Presentation Bridge                        │
│ gameplay entity ↔ render child entity      │
└───────────────────┬─────────────────────────┘
                    ▼
┌─────────────────────────────────────────────┐
│ AoE2 Render                                │
│ SLD appearance / animation / batch / sprite│
└─────────────────────────────────────────────┘
```

关键约束：

- `AoePosition`、`AoeHealth`、`AoeActionState`、`AoeAttackOrder` 等是 gameplay 真值；
- AoE2 Sprite entity 是 gameplay entity 的表现 child，不保存战斗真值；
- 动画播放结束不会反向通知 gameplay “可以结算攻击”；攻击释放、冷却、死亡和回收由固定 tick 决定；
- 表现资源暂未加载成功时，gameplay 仍可继续推进；
- render child 丢失时，桥接层可以重新创建，不改变 gameplay unit 身份；
- DAT/SLD metadata 属于表现资源，通用 gameplay 层不直接依赖 AoE2 模块。

## 3. 坐标、碰撞与时间约定

### 3.1 Gameplay 坐标

- `AoePosition::value` 使用二维逻辑地图坐标 `(x, y)`；
- Z 不存入单位位置，地形高度通过 `AoeLogicMap::sample_height()` 独立采样；
- Projectile 使用 `glm::vec3`，其中 X/Y 是逻辑地面轴，Z 是相对地面的高度；
- 等距投影仅存在于 example/表现层，不进入通用寻路或 Gizmo API。

### 3.2 碰撞体

单位碰撞体为竖直椭圆柱：

```cpp
struct AoeCollider {
    float radius_x;
    float radius_y;
    float height;
};
```

二维距离判定不使用简单的“中心距离减固定半径”，而是使用椭圆在目标方向上的 support radius：

```text
surface_gap = center_distance
            - support_radius(unit_a, direction_to_b)
            - support_radius(unit_b, direction_to_a)
```

因此攻击距离、靠近目标的停止距离以及 Projectile 横向碰撞会考虑双方真实碰撞体。`surface_gap <= attack.range` 才表示可以攻击；碰撞体重叠时 gap 可以为负数。

### 3.3 固定时间步

默认配置：

```cpp
AoeGameplaySettings {
    fixed_dt = 1.0 / 30.0;
    max_catchup_ticks = 8;
}
```

每个渲染帧把 `Time::dt` 累加到 `AoeGameplayClock::accumulator`，最多追赶 8 个 gameplay tick。若仍然落后，超出部分计入 `dropped_seconds`，只保留不足一个 fixed tick 的余数，防止“死亡螺旋”。

所有以秒配置的 gameplay 动作时刻都会向上换算为固定 tick。攻击 release、攻击动画逻辑结束、cooldown、death 和 disappear 的权威时间都来自该时钟。

## 4. 单位定义与实例

### 4.1 静态定义

`AoeUnitDefinition` 保存共享、静态的单位模板：

- `id`、`tags`、`level`；
- `max_hp`；
- 分 class 的 `armor`；
- 椭圆柱 `collision`；
- `movement.speed`；
- `target_acquisition.strategy/radius/disengage_radius`；
- 可选 `attack`；
- `lifecycle`；
- `presentation` 资源和语义动画映射。

当前 JSON 使用 `schema_version: 2`、`kind: aoe_gameplay_unit`。以下为精简示例：

```json
{
  "schema_version": 2,
  "kind": "aoe_gameplay_unit",
  "id": "archer",
  "tags": ["archer", "ranged"],
  "level": 1,
  "max_hp": 30.0,
  "armor": [{"class_id": 3, "amount": 0.0}],
  "collision": {"radius_x": 0.2, "radius_y": 0.2, "height": 2.0},
  "movement": {"speed": 0.96},
  "target_acquisition": {
    "strategy_id": "nearest_enemy",
    "radius": 6.0
  },
  "lifecycle": {
    "death_duration_seconds": 1.0,
    "disappear_duration_seconds": 1.0
  },
  "attack": {
    "mode": "projectile",
    "damage": [{"class_id": 3, "amount": 4.0}],
    "range": 4.0,
    "release_seconds": 0.5,
    "animation_duration_seconds": 1.0,
    "cooldown_seconds": 2.0,
    "critical_chance": 0.1,
    "critical_multiplier": 2.0,
    "projectile_id": "arrow",
    "projectile_launch_offset": {"x": 0.0, "y": 0.5, "z": 1.5}
  },
  "presentation": {
    "backend": "aoe2",
    "resource_id": "u_arc_archer",
    "default_player_color": 1,
    "animations": {
      "idle": "idleA",
      "moving": "walkA",
      "attack": "attackA",
      "critical_attack": "attackB",
      "death": "deathA",
      "disappear": "decayA"
    }
  }
}
```

配置验证包括有限浮点、非负尺寸与时间、攻击时间顺序，以及 projectile 攻击必须提供 `projectile_id`。当前要求：

```text
release_seconds <= animation_duration_seconds <= cooldown_seconds
```

### 4.2 运行时实例

一个活动单位通常包含：

| 范畴 | 主要组件 | 含义 |
| --- | --- | --- |
| 身份 | `AoeGameplayIdentity` | `instance_id` 与单位私有 RNG 状态 |
| 定义 | `AoeUnitDefinitionRef` | 指向共享定义资产 |
| 属性 | `AoeHealth`、`AoeLevel`、`AoeTeam` | 实例可变属性 |
| 空间 | `AoePosition`、`AoeCollider`、`AoeFacing` | 位置、碰撞和朝向 |
| 移动 | `AoeMovement`、`AoeLocomotionState` | 名义速度、实际速度、累计路程和避障连续性状态 |
| 行为 | `AoeActionState` | Idle/Moving/Attacking/Dying/Disappearing |
| 表现参数 | `AoePresentationOptions` | 玩家色和 render layers |

`entt::entity` 可能在对象池中复用，所以所有跨 tick 目标引用都使用：

```cpp
struct AoeUnitTarget {
    entt::entity entity;
    std::uint64_t instance_id;
};
```

只有 entity 和 instance ID 同时匹配，且目标仍存活、未进入回收、具备必要 gameplay 组件时，引用才有效。这一规则用于攻击命令、Squad member、engagement approach 和 Projectile target。

## 5. 固定 Tick 流水线

一个 gameplay tick 的顺序固定为：

```text
1. capture_position_history_tick
2. squad_spawn_resolution_tick
3. aoe_map_static_obstacle_system
4. aoe_dynamic_obstacle_index_system
5. squad_command_tick
6. command_tick
7. squad_membership_cleanup_tick
8. squad_traffic_tick
9. squad_control_tick
10. attack_move_acquisition_tick
11. navigation_tick
12. movement_intent_tick
13. local_avoidance_intent_tick
14. global_motion_planner_tick
15. global_motion_safety_tick
16. movement_tick
17. combat_tick
18. aoe_projectile_tick
19. lifecycle_tick
```

这个顺序有几项重要含义：

- 动态障碍索引每个 tick 只构建一次，后面的寻路与移动共享；
- 命令先转换为 order/goal，再由导航和移动消费；
- AttackMove 在导航前索敌，因此进入发现范围的目标可在同 tick 推迟原路线并开始追击；
- 索敌读取的是上一 fixed tick 留下的 `stalled_ticks`，因此第一次实际停滞后的下一 tick 可以响应射程内替代目标；
- navigation 只维护路径，movement intent、局部避障和全局运动阶段依次生成并约束本 tick 的最终速度；
- movement 先更新位置，combat 再用最新位置检查攻击距离；
- Projectile 在攻击 release 产生后同 tick 注册，但从下一个 tick 开始飞行；
- lifecycle 最后推进死亡与消失状态，PostUpdate 再做实际池化。

`Stage::First` 会清除上一渲染帧的 action/navigation events；同一渲染帧内多个追赶 tick 产生的事件会一起保留到本帧消费者读取。

## 6. 逻辑 Map

### 6.1 Map 数据

`AoeLogicMap` 是有限矩形网格，不依赖渲染。定义包括：

- `origin`：地图最小逻辑坐标；
- `tile_size`；
- `width/height`：tile 数量；
- `(width + 1) * (height + 1)` 个顶点高度；
- 静态障碍列表。

Map JSON 当前为 schema 1：

```json
{
  "schema_version": 1,
  "kind": "aoe_logic_map",
  "id": "example",
  "origin": {"x": 0.0, "y": 0.0},
  "tile_size": 1.0,
  "width": 1,
  "height": 1,
  "heights": [0.0, 0.0, 0.0, 0.0],
  "static_obstacles": []
}
```

实际 `heights` 长度必须严格等于 `(width + 1) * (height + 1)`。

### 6.2 高度

`sample_height(position)` 在四个相邻顶点之间做双线性插值。越界点返回 `std::nullopt`。

当前高度的用途是：

- 给 gameplay 和表现层提供统一地面高度查询；
- 为后续高地攻击加成、坡度和地形表现预留数据。

当前寻路没有把坡度、落差或地形移动成本纳入代价，战斗也还没有应用高地加成。

### 6.3 静态障碍

当前支持：

- 轴对齐矩形 `Aabb`；
- 圆形 `Circle`。

静态障碍可以来自 Map JSON，也可以由带有 `AoeMapStaticObstacle + AoePosition` 的运行时 entity 注册，例如后续 building。组件位置或形状变化时更新记录，entity 消失时移除记录。

每次静态障碍变化都会递增 `static_revision()`。路径保存自己规划时的 revision，地图变化后可以触发失效和重算。

`AoeLogicMap` 为静态障碍建立按 cell 分桶的索引，并提供：

- 点/带 clearance 的边界检查；
- cell 是否可通行；
- 线段移动的 `static_safe_fraction`；
- 增删改静态障碍。

### 6.4 动态障碍

所有活动 gameplay unit 都是动态障碍。`AoeDynamicObstacleIndex` 每个 fixed tick 从以下数据重建：

- `AoePosition`；
- `AoeCollider`；
- `AoeGameplayIdentity`；
- 可选 `AoeLocomotionState::velocity`；
- 可选 Squad 身份。

`AoePooledUnit` 和 `AoeRecyclePending` 不进入索引。

索引采用两阶段 count/prefix/fill 的扁平 cell storage：

1. 收集 entry 和它覆盖的 cell membership；
2. 计算每个 cell 的连续区间；
3. 一次填充紧凑 `members_`；
4. query 通过 generation mark 去重跨多个 cell 的同一单位。

这样可以避免每次 query 建立集合，也避免所有单位之间做 O(N²) 碰撞扫描。`AoeDynamicObstacleDiagnostics` 记录 rebuild、索引单位数、membership、query 和候选数量。

## 7. 寻路与局部转向

### 7.1 分层设计

移动分为三层：

```text
Order / Squad slot / Attack approach
                │
                ▼
      Global pathfinder
      生成稀疏 waypoints
                │
                ▼
      Local steering
      选择连续 target velocity
                │
                ▼
      Swept collision clamp
      最终防穿透并提交 AoePosition
```

全局寻路解决“从哪一侧绕过大障碍”，局部转向解决短时动态冲突，最终 swept collision 是防止穿透的权威保护。局部 steering 不能替代全局路径，也不能绕过最终碰撞检查。

### 7.2 Pathfinder 静态接口

路径策略注册到 `AoePathfinderRegistry`：

```cpp
struct MyPathfinder {
    static AoePathResult find(EcsWorld& world,
                              const AoePathRequest& request);
};

auto& registry = world.resource_or_add<AoePathfinderRegistry>();
registry.bind<MyPathfinder>("my_pathfinder");
world.resource<AoeNavigationSettings>().unit_pathfinder_id = "my_pathfinder";
```

`AoePathRequest` 包含起点、终点、椭圆 clearance、subject、Squad、可忽略的动态目标，以及是否纳入动态障碍。返回状态为 `Ready/NoPath/InvalidStart/InvalidGoal`。

内置策略：

- `direct`：直接返回目标 waypoint；
- `grid_astar`：在 `AoeLogicMap` 上执行八邻域 A*。

若 world 没有有效 `AoeLogicMap`，导航有意退化为 direct 行为，方便独立 gameplay 测试和简单 example。

### 7.3 Grid A*

当前 `grid_astar`：

- 使用八邻域；
- 对角移动时防止 corner cutting；
- 用单位 X/Y clearance 检查 cell；
- 可按请求检查动态障碍；
- 找到 raw path 后，用无碰撞 line-of-sight 合并多余 waypoint；
- 返回规划时的 map static revision。

`AoeNavigationPath` 记录 waypoint、当前索引、requested goal、map revision、request sequence、最后重算 tick、blocked ticks、no-path 以及是否包含动态障碍。

持续没有前进会产生 `Blocked` navigation event。达到阈值后清空路径，并要求下一次重算纳入动态障碍。重算具有 cooldown 和按实例错开的确定性节奏，避免整队同一帧集中重算。静态 NoPath 会等待地图 revision 变化后再尝试。

### 7.4 局部 Steering 静态接口

局部策略注册到 `AoeSteeringRegistry`：

```cpp
struct MySteering {
    static AoeSteeringResult steer(const AoeSteeringContext& context);
};

auto& registry = world.resource_or_add<AoeSteeringRegistry>();
registry.bind<MySteering>("my_steering");
world.resource<AoeNavigationSettings>().steering_strategy_id = "my_steering";
```

`AoeSteeringContext` 接收最终 gameplay 坐标、当前位置与速度、preferred velocity、goal、最大速度、单位半径、预测时间、邻居 span、Map 和上一帧偏好避让侧。接口无虚函数，也不要求为策略实例化 entity。

内置 `local_default` 的核心行为：

1. 从动态网格保留最多 8 个最相关邻居；
2. 无威胁时走快速直行路径；
3. 有威胁时比较直行和左右侧向候选；
4. 非紧急威胁结果按错开的两 tick cadence 复用；
5. 即将碰撞时立即重新计算；
6. 保持一段时间的避让侧，并要求足够收益才换边；
7. 对速度应用最大加速度和最大转向角速度；
8. 朝向必须在新 sector 连续稳定若干 fixed ticks 才提交，减少贴图方向闪烁。

最后从当前位置到 candidate position 分别计算静态和动态 `safe_fraction`，按最小值缩短位移。实际位移写入：

- `AoePosition`；
- `AoeLocomotionState::velocity`；
- `actual_speed`；
- `distance_travelled`。

`AoeCrowdSteeringScratch` 是 world resource，缓存 arrived 与 neighbor 数组容量，避免 movement tick 中反复分配。

### 7.5 同 Squad 的碰撞规则

- 正常保持 Formation 移动时，同一 Squad 成员互相忽略动态碰撞，避免队形自己堵死；
- `Engaging` 阶段不再互相忽略，已经占据近战/远程攻击位置的成员会成为其他成员的真实障碍；
- 每个成员拥有自己的 slot path，可从障碍两侧拆分绕行；
- Squad anchor 仅是虚拟队形参考，不强制所有成员原地等待落后者。

## 8. 命令、索敌与攻击机制

### 8.1 命令队列

单位命令通过 `AoeGameplayCommands` 排队，在下一次 fixed tick 统一转换为 ECS order：

- `AttackTarget`；
- `AttackMove`；
- `MoveTo`；
- `Stop`；
- `SetFacing`；
- `SetHealth`；
- `SetLevel`。

常用入口：

```cpp
request_aoe_attack(world, attacker, target);
request_aoe_attack_move(world, unit, destination);
request_aoe_move(world, unit, destination);
request_aoe_stop(world, unit);
```

对 Squad member 直接下达带 order 的单位命令时，该成员会先脱离 Squad，避免小队控制器下一 tick 覆盖用户命令。

### 8.2 索敌策略

索敌策略使用枚举到实现类型的静态模板映射：

```cpp
enum class AoeTargetAcquisitionType : std::uint8_t {
    NearestEnemy
};

struct NearestEnemyAcquisitionStrategy {
    static std::optional<AoeUnitTarget> select(
        const EcsWorld& world,
        const AoeTargetAcquisitionContext& context);
};

template<>
struct AoeTargetAcquisitionBinding<
    AoeTargetAcquisitionType::NearestEnemy> {
    using type = NearestEnemyAcquisitionStrategy;
};
```

单位 JSON 仍用 `target_acquisition.strategy_id` 序列化，加载时转换为枚举；默认是 `nearest_enemy`。运行时不再维护字符串 registry。

内置 `nearest_enemy`：

- 排除自己、同队、死亡、回收中和 pooled 单位；
- 支持一组 `excluded` 目标，用于不可达目标切换时临时排除当前目标；
- 使用双方椭圆碰撞面之间的 gap，而不是中心距离；
- gap 相同时以 instance ID、entity ID 做稳定决胜，保证结果可重复。

独立单位使用活动单位视图搜索；Squad 有有效逻辑地图时复用动态障碍空间索引构建一次共享候选集合，没有地图时回退到活动单位视图。

### 8.3 AttackTarget 与 AttackMove

两者语义不同：

`AttackTarget`：

- 指定一个明确目标；
- 目标不在攻击距离时，生成追击目标和导航路径；
- 到达由双方碰撞体和攻击 range 决定的位置后停止并朝向目标；
- 目标有效且未被新命令替换时，cooldown 结束后持续攻击。

单单位 `AttackMove`：

- 保留原始移动终点；
- 每个 tick 在导航前检查敌人；
- 使用 `target_acquisition.radius` 发现敌人，`attack.range` 只决定何时能够攻击；
- 锁定后不会因为出现更近敌人或尚未进入武器射程而切换；
- 例外是追击发生第一次停滞后（`AoeLocomotionState::stalled_ticks > 0`），如果旧目标仍在武器射程外，可以立即切换到一个已经进入自身真实武器射程的敌人；
- 这个机会索敌使用碰撞面 gap 和单位自己的索敌策略；没有射程内替代目标时不清理旧目标、路径或不可达累计，继续原来的重寻路/不可达逻辑；
- 机会目标死亡后按普通 AttackMove 重新索敌，不保存或恢复一个“挂起的旧目标”；
- 当前目标死亡、失效、离开 `disengage_radius` 或确认不可达后重新选择最近目标；
- 到达终点且无目标时结束命令。

`AttackMove` 保留终点并主动追击发现范围内的目标；`AttackTarget` 则只处理明确指定的目标，目标失效后不会自动串联附近敌人。

### 8.4 朝向

SLD direction 0 的屏幕基准方向是 `(+1, 0)`。Gameplay X/Y 向量先通过 2:1 等距基变换到屏幕方向：

```text
screen_x = x - y
screen_y = (x + y) * 0.5
```

然后从屏幕 `(+1, 0)` 计算角度并落入最近 direction sector。移动使用稳定 tick 滞回，进入攻击前则直接朝向目标，确保攻击贴图与目标一致。

## 9. 战斗与 Projectile

### 9.1 攻击时间线

一次攻击由 `AoeActionState` 的固定 tick 字段驱动：

```text
AttackStarted
    │
    ├── release_tick ── AttackReleased ── 近战伤害或生成 Projectile
    │
    ├── finish_tick  ── AttackFinished，动作回到 Idle
    │
    └── ready_tick   ── 允许开始下一次攻击
```

`finish_tick` 可以早于 `ready_tick`，因此单位可以在攻击动画逻辑结束后保持 Idle，直到 cooldown 到期再开始下一次攻击。只要 `AoeAttackOrder` 仍有效，系统会自动持续攻击，不需要表现动画发送重复事件。

攻击开始时从单位私有确定性 RNG 采样暴击，并把结果保存在本次 action state 中。表现层可以据此选择 `critical_attack`；缺失时回退到普通 `attack`。

### 9.2 伤害与护甲

伤害和护甲都按 `class_id` 配对。每个伤害 class 计算：

```text
class_damage = max(0, attack_amount - armor_of_same_class)
total = sum(class_damage)
if critical: total *= critical_multiplier
if attack payload contains any positive amount: total = max(1, total)
```

因此只要攻击 payload 至少有一个正数，即使护甲完全覆盖，也会造成最少 1 点总伤害。当前 `level` 已保存到实例，但尚未自动参与伤害公式。

### 9.3 Projectile Registry

Projectile 逻辑使用静态配置接口：

```cpp
struct MyProjectile {
    static entt::entity spawn(
        EcsWorld& world,
        const AoeProjectileSpawnContext& context);
};

registry.bind<MyProjectile>("my_projectile");
```

单位 JSON 中 `projectile_id` 映射到具体逻辑。内置 `arrow` 配置当前为：

- speed：7；
- arc height：0.65；
- collision radius：0.05；
- maximum lifetime：5 秒。

攻击 release 时，根据单位 facing 旋转可选 `projectile_launch_offset`。没有 offset 时使用单位高度的 75% 作为默认 Z。箭矢每个 tick 向目标当前位置推进，Z 使用起点到目标碰撞体中心的插值叠加抛物线弧高。

命中使用从 `previous_position` 到新位置的 swept test：横向是扩大了 Projectile 半径的椭圆，纵向检查目标碰撞柱高度。这可以防止高速 Projectile 一帧穿过目标。

Projectile 保存带 instance ID 的目标引用。目标失效、被池化、已死亡、超时或逻辑 ID 未注册时产生明确 miss/failure event，不会攻击复用同一 entity ID 的新单位。

## 10. Squad 与 Formation

### 10.1 Squad 是独立 Entity

Squad entity 保存：

- `AoeSquadMembers`：pending 与 active member；
- `AoeSquadSpawnState`：请求数、成功数、失败数及错误；
- `AoeSquadFormation`：类型、spacing、forward、slots；
- `AoeSquadOrder`；
- `AoeSquadState`；
- `AoeSquadCombatSettings`；
- Squad 自己的 `AoePosition`，作为虚拟 anchor。

`spawn_aoe_gameplay_squad()` 接受 composition 列表并异步等待各单位定义资产。Gameplay Squad 的 Ready 不依赖表现资源是否加载成功；AoE2 render child 由表现桥接层独立创建。最终状态可能是 `Ready/Partial/Failed/Empty`。

Squad 整体命令入口：

```cpp
request_aoe_squad_move(world, squad, destination);
request_aoe_squad_attack(world, squad, target);
request_aoe_squad_attack_move(world, squad, destination);
request_aoe_squad_stop(world, squad);
set_aoe_squad_formation(world, squad, formation);
disband_aoe_gameplay_squad(world, squad);
```

### 10.2 Formation 静态接口

Formation 通过 enum 到静态实现的 Registry 配置：

```cpp
formations.bind<AoeFormationType::Skirmish, MyFormation>();
```

实现接收成员 ordinal、tags、碰撞半径与 spacing，返回每个带 instance ID 成员的 local slot。Registry 会验证：

- slot 数与成员数一致；
- 坐标有限；
- 每个成员恰好出现一次；
- 不允许不属于 Squad 的 entity。

### 10.3 当前散兵方阵

内置 `DefaultSkirmishFormation` 使用 `AoeSquareFormation`：

```cpp
spearman = 300
cavalry  = 200
scout    = 100
archer   = -100
```

一个成员命中多个 tag 时优先级相加，按总优先级降序排列，同优先级按稳定 ordinal 排列。因此高优先级成员进入前排，Archer 倾向后排。

方阵列数约为 `ceil(sqrt(member_count))`。当前 cell 尺寸为：

```text
cell_size = 2 * max_collision_radius_of_all_members + formation_spacing
```

这里使用全队最大的 X/Y 碰撞半径，保证混合单位阵型拥有统一网格。`formation_spacing` 是碰撞外的额外空隙，所以阵型不是强制密铺。代价是少量大单位会放大全队 cell；后续可以实现按行/按成员尺寸打包的 Formation 策略。

slot local X 表示队形横向，local Y 表示 forward 方向。世界坐标为：

```text
slot_world = squad_center
           + right   * local_x
           + forward * local_y
```

### 10.4 协同移动

- Squad anchor 使用 `squad_pathfinder_id` 规划静态障碍 guide；
- 每个成员使用 `unit_pathfinder_id` 向不断移动的 formation slot 规划；
- Squad 移动速度限制为仍存活成员中的最低速度；
- slot 移动超过阈值时成员路径重算；
- 成员可以在障碍两侧分流，之后逐步回到阵型；
- `squad_leash` 当前主要用于诊断，不会让整队因为一个落后成员而停止；
- anchor 自己无路可走时 Squad 才进入 `Blocked`。

Squad AttackMove 的 anchor 到达最终目的地后，如果还有成员尚未到达 slot，会执行一次终点重排。重排只改变 `slot.unit` 绑定，不改变 slot 几何、不瞬移成员，也不改写位置历史：

- 按 Formation priority 分组，成员只能在同一优先级组内换位，因此前排/后排角色顺序保持不变；
- 每组使用确定性的最小总代价匹配，代价是成员当前位置到 slot 世界坐标的平方距离；
- 相同代价按稳定 member ordinal、instance ID、entity ID 与原 slot 顺序决胜；
- 成功后同 tick 重新下发 slot goal，避免成员继续走向已被同队成员占据或交叉的旧 slot；
- 每个终点只执行一次；新命令、成员变化、Formation 变化或战斗 engagement 会重置该标记；
- 普通 Squad MoveTo 不执行终点重排。

### 10.5 Squad 作战

显式 `Squad AttackTarget`：

- 保持 focus-fire 语义，所有能攻击的成员使用同一个目标；
- 成员仍根据确定性的 approach direction 分散靠近，不会全部走向同一中心点。

`Squad AttackMove`：

- 任一成员发现的敌人进入全 Squad 共享候选集合；
- 每个成员使用 Squad combat settings 中的策略，从共享集合选择离自己最近的目标；
- 不再使用 claimed 去重，多个成员可以锁定同一个敌人；
- 存在共享 engagement 时 Squad anchor 暂停 AttackMove 推进；
- 某成员追击旧目标发生第一次停滞、且旧目标仍在射程外时，可以立即改打一个已经进入该成员真实武器射程的敌人；该检查不会使用未按成员射程过滤的共享候选集合；
- 没有射程内替代目标时，该成员保持现有 target/path/不可达处理，Squad 仍保持原 engagement；
- 目标死亡后，持有该目标的成员立即寻找下一个目标；
- 仍有敌人时不会先强制回归 Formation；
- 范围内没有目标后，Squad 才恢复原 AttackMove 终点并在移动中重建阵型。

Engagement approach 使用实例 ID 生成稳定的环形方向。当前期望额外 gap：

- melee：`attack.range * 0.5`；
- projectile：`attack.range * 0.8`。

目标点再加上双方在该方向上的椭圆 support radius。因此近战形成内圈，远程形成外圈，并能围住目标，而不是为了保持 travel formation 一直排在后方。

## 11. Gameplay 驱动动画

### 11.1 语义动画

Gameplay 配置只使用语义名称：

| Gameplay 状态 | 动画语义 | 回退 |
| --- | --- | --- |
| `Idle` | `idle` | 必填 |
| `Moving` | `moving` | `idle` |
| `Attacking` | `critical_attack` 或 `attack` | 暴击动画缺失时回退 `attack` |
| `Dying` | `death` | schema 2 lifecycle 要求提供 |
| `Disappearing` | `disappear` | schema 2 lifecycle 要求提供 |

AoE2 bridge 把语义动画映射到具体 `idleA/walkA/attackA/deathA/decayA` 等资源名称。

### 11.2 External Playback

桥接层始终把 AoE2 render unit 设为 `Aoe2PlaybackMode::External`：

- render animation system 不自行决定 gameplay 时间；
- `playing=false`，每帧由 bridge 写入 `playback_time`；
- direction/player color/loop 同样由 gameplay snapshot 同步。

这保证 headless gameplay 与有渲染 gameplay 使用同一攻击释放、死亡和回收时刻。

### 11.3 各类动画时间来源

Idle：

- 按 gameplay tick 的固定时间推进；
- 不使用 render frame delta，渲染掉帧不会改变 gameplay 相位。

Moving：

- 按 `AoeLocomotionState::distance_travelled` 增量推进；
- 换算公式约为 `distance_delta / definition.movement.speed`；
- 原地受阻时 distance 不增加，walk 动画冻结，避免原地滑步；
- 实际速度较慢时动画同比减慢。

Attacking/Dying/Disappearing：

- 使用 `state_started_tick` 到当前 gameplay tick 的权威动作时间；
- 新 authored action 开始时同步到 action elapsed，而不是沿用旧 loop 的任意时间；
- 攻击 release 和生命周期不依赖动画帧 event。

### 11.4 切换连续性

`Aoe2PresentationSnapshot` 保存：

- state 与 action sequence；
- last gameplay tick；
- playback time；
- locomotion distance；
- critical；
- direction 与 direction count；
- 当前请求动画。

Idle 与 Moving 这类 loop 之间切换时保留累计 playback phase，再按目标 clip duration 取模。仅改变 facing direction 时不会重置 playback time，因此单位转向不会跳回动画第一帧。

Attack/Death/Disappear 属于 authored action。新 sequence 会从该动作的 gameplay elapsed 开始，以保证攻击释放点、死亡时长和动画含义同步。它们不继承上一个 Idle/Move clip 的任意帧序号。

### 11.5 位置插值与脚点

每个 fixed tick 开始时把当前位置写入 `AoePositionHistory::previous`。表现层使用：

```text
render_position = lerp(previous, current, accumulator / fixed_dt)
```

这提供一 fixed tick 延迟的平滑显示。碰撞、寻路、索敌、攻击和 Projectile 始终读取最新 `AoePosition`，不读取插值位置。

AoE2 SLD 的 foot/hotspot 只负责把 Sprite 原点对齐 gameplay 世界原点。切 direction 或 frame 时，batch 顶点构造会补偿各帧 hotspot，避免 gameplay foot 因贴图裁剪差异跳动。

## 12. 死亡、消失与对象池

生命值降到 0 后：

```text
Attacking/Moving/Idle
        │ DeathStarted
        ▼
      Dying
        │ death_duration_seconds
        ▼
  Disappearing（如果配置了时长）
        │ disappear_duration_seconds
        ▼
  AoeRecyclePending
        │ PostUpdate
        ▼
  AoePooledUnit + AoeGameplayPool::available
```

进入死亡状态会取消攻击、AttackMove、MoveGoal 和 NavigationPath。生命周期只根据 fixed tick 推进，不等待 render clip 回调。

PostUpdate 回收会移除 gameplay、空间、Squad、导航和表现相关组件，并把 entity 放入 `AoeGameplayPool`。之后 spawn 可以复用 entity，但会分配新的 `instance_id` 和 RNG seed。AoE2 bridge 会清理旧 render child，防止遗留表现对象。

## 13. 事件与诊断

### 13.1 Action Event

`AoeActionEventType` 当前包括：

- `AttackStarted`；
- `AttackReleased`；
- `AttackFinished`；
- `DamageApplied`；
- `DeathStarted`；
- `DisappearStarted`；
- `RecycleRequested`；
- `ProjectileSpawned`；
- `ProjectileHit`；
- `ProjectileMiss`；
- `ProjectileSpawnFailed`。

Event 包含 tick、sequence、critical、amount、target、Projectile entity/ID 和 miss reason，可用于音效、UI、统计或 replay 记录。消费者不应使用这些 event 再决定权威伤害；伤害在产生 event 前已经由 gameplay 提交。

### 13.2 Navigation Event

`AoeNavigationEvent` 状态为：

- `Ready`；
- `Blocked`；
- `NoPath`。

并携带 subject、request sequence 和 tick，便于 UI/AI 区分过期路径事件。

### 13.3 性能诊断

现有诊断包括：

- 攻击、伤害、命令拒绝、Projectile 生成/命中/丢失；
- steering fast/full/cached/imminent solve 数；
- neighbor 数、换边数、朝向抑制/提交数；
- movement 最近/峰值耗时；
- 动态障碍索引 entry、membership、query 和 candidate 数；
- Squad preview 中的 live entity 分类与 high-water；
- AoE2 animation/batch/upload/render 统计。

`aoe_gameplay_squad_preview` 提供 16/64/128 每边的压力档位以及 Map、路径、碰撞体和 foot Gizmo 开关。做性能比较时应关闭调试 Gizmo，并确认 HUD 的 `vsync=OFF`，否则 FPS 会被显示器刷新率限制。

## 14. 扩展指南

### 14.1 新寻路算法

实现静态 `find()`，注册到 `AoePathfinderRegistry`，再分别配置 unit 或 squad pathfinder ID。新算法必须：

- 接受椭圆 clearance；
- 返回明确状态；
- 保存/返回 map revision；
- 对相同输入保持稳定顺序；
- 不直接修改 unit position。

### 14.2 新局部避障

实现静态 `steer()` 并注册。算法只输出 target velocity 和 avoidance side，不应直接写 ECS。最终加速度、转向限制和 swept collision 仍由 movement system 统一执行。

### 14.3 新索敌策略

实现静态 `select()` 并注册，然后在单位 JSON 填写 strategy ID。策略必须返回带正确 `instance_id` 的 `AoeUnitTarget`，尊重 team、有效性和 excluded 列表。

未来大规模索敌建议新增独立的 combat spatial index，按 team/faction 建桶；不要把动态碰撞网格内部 storage 直接泄露给策略。

### 14.4 新 Formation

增加 `AoeFormationType`，实现 `layout()`，通过模板绑定 enum 与实现。Formation 只决定 slot，不直接移动成员。可以扩展为：

- 按角色分前、中、后排；
- 不同单位尺寸的紧凑 packing；
- 楔形、线形、圆阵；
- 根据道路宽度缩阵；
- ranged/melee 动态战斗阵型。

### 14.5 新 Projectile

实现静态 `spawn()` 并注册 ID。Projectile entity 的后续运动可以沿用通用 `AoeProjectile` tick，也可以在以后扩展专属 component/system。表现资源映射应放在具体 presentation bridge，而不是通用 gameplay Registry。

## 15. 当前限制与后续方向

当前已知限制：

1. `nearest_enemy` 仍遍历所有活动单位，尚未使用 team-aware 索敌空间索引；
2. Grid A* 是逐单位路径，未实现 flow field、hierarchical pathfinding 或 navmesh；
3. 局部 steering 是确定性的轻量候选转向，不是完整 ORCA；
4. Formation 使用全队最大半径的统一 cell，大单位会扩大所有 slot 间距；
5. Squad 没有 formation shrinking、共享 corridor reservation 和严格队形约束；
6. 地形高度不影响可通行性、移动速度、视野和攻击加成；
7. 静态障碍只有 AABB 与 circle；
8. `level` 已实例化但尚未接入成长和伤害公式；
9. 当前 Projectile 为跟踪目标当前位置的逻辑箭矢，不包含散布、弹道提前量、地形遮挡和友军碰撞；
10. gameplay 浮点计算在单机同平台上保持稳定顺序，但尚未承诺跨平台 lockstep bitwise determinism。

优先演进建议：

1. 为索敌增加 team-aware uniform grid，并与动态碰撞索引保持接口隔离；
2. 为路径请求增加 terrain cost、坡度和不可跨越高度差；
3. 为大 Squad 增加共享 corridor/flow field，同时保留成员局部 steering；
4. 将 attack policy、chase leash 和 threat response 从 AttackMove 语义中进一步配置化；
5. 为 Formation 引入 per-member footprint packing 和窄路临时队形；
6. 增加 gameplay snapshot/replay 测试，为网络同步做准备。

## 16. 必须保持的系统不变量

后续修改应保持以下不变量：

- gameplay 结算不依赖 render entity、动画帧 event 或窗口 FPS；
- 所有跨生命周期目标引用都校验 `instance_id`；
- dynamic obstacle index 每个 fixed tick 最多统一重建一次；
- pathfinder 和 steering 不直接生成临时 ECS entity；
- 最终位移必须经过静态/动态 swept collision；
- terminal unit 不再接受正常移动或攻击推进；
- 回收前先结束 gameplay 生命周期，复用时生成新身份；
- Squad member 的直接单位 order 与 Squad controller 不能同时拥有控制权；
- debug Gizmo 和 HUD 不参与 gameplay 判定；
- AoE2/DAT/SLD 细节不能反向污染通用 gameplay、Map 和寻路接口。
