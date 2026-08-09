# aoe2x Gameplay System 规范

本文约束 `aoe2x` gameplay 系统的组件访问、阶段排序和可替换 pipeline，避免系统依赖注册顺序或隐式共享状态。

## 1. 系统声明

每个系统必须满足 `Aoe2xGameplaySystem` concept，并声明：

- `name`：全局稳定且唯一的名称；
- `app_stage`：当前 gameplay 系统统一运行于 `Stage::PreUpdate`；
- `phase`：使用 `Aoe2xGameplayPhase` 表达数据依赖顺序；
- `ReadOnlyComponents`、`WriteOnlyComponents`、`ReadWriteComponents`：完整列出 ECS 组件访问权限。

资源（例如 `AoeLogicMap`、命令队列、配置和诊断信息）不放入组件列表。系统不得写入声明为只读的组件，也不得隐式修改未声明的组件。

## 2. 阶段顺序

同一 tick 按下列语义排序：

1. `Prepare`：消费 spawn 请求，建立实体和初始组件；
2. `Command`：消费外部命令，创建本次命令状态；
3. `Navigation`：计算路径；
4. `MovementIntent`：把路径转换为单位级意图或路线；
5. `LocalAvoidance`：处理局部动态避障；
6. `GlobalMotion`：处理全局运动约束或回退；
7. `Movement`：推进位置、速度和朝向；
8. `Combat`：结算攻击等战斗行为；
9. `Lifecycle`：处理死亡、释放和残留组件清理。

跨阶段传递的数据必须是显式组件或资源。后置系统只能消费前置系统已经提交的稳定状态。

## 3. 可替换 pipeline

共享 API 不表示实现可以同时注册。一个 app 对同一种请求只能选择一条消费 pipeline：

| 能力 | FollowChain Formation | RouteSplit Formation |
|---|---|---|
| Spawn 消费者 | `SpawnFormationSystem` | `RouteSquadSpawnSystem` |
| Command 消费者 | `FormationCommandSystem` | `RouteSquadCommandSystem` |
| 路径计算 | 队长路径 | squad 中心路径 |
| 路径后处理 | `FormationSystem` 驱动跟随链 | `RouteSquadSplitSystem` 分发成员路线 |
| Lifecycle | `Aoe2xUnitLifecycleSystem` | `RouteSquadCleanupSystem` |

禁止在同一个 app 同时注册两套 Spawn 或 Command 消费者，否则它们会竞争 `FormationSpawnRequest` 或 `FormationCommands`。RouteSplit 只负责生成成员路线，不负责执行这些路线；运动系统应由上层按需要另行组合。

## 4. Formation 公共契约

两套 pipeline 复用：

- `FormationSpawnOptions`、`FormationSpawnRequest`、`FormationSpawnState`；
- `SquadInfo`；
- `FormationAttackMoveCommand`、`FormationCommands`、`FormationAttackMove`；
- `spawn_aoe2x_formation()` 与 `request_aoe2x_formation_attack_move()`。

`FormationAttackMoveCommand::destination_facing` 可省略。RouteSplit 会把显式值归一化；未提供时使用“有效成员质心到目标”的方向，同点时回退到 spawn 朝向。FollowChain 保持原有行为，不消费该字段。

`FormationAttackMoveStatus::Completed` 的具体含义由 pipeline 定义：FollowChain 表示移动完成；RouteSplit 表示所有有效成员的路线已经成功分发，并不表示成员已经到达。

## 5. RouteSplit Formation

### Spawn

Spawn 必须存在有效 `AoeLogicMap`。阵型中心不因成员阻挡而移动；每个成员从自己的理想槽位开始，在配置半径内独立搜索合法位置。默认搜索半径为一个 tile。候选位置必须：

- 位于地图范围内；
- 不与静态障碍碰撞；
- 不与本次已经分配的成员重叠。

所有成员位置先计算、后创建实体；任何成员无合法位置时整次 spawn 失败，不留下部分成员。成员用 `RouteSquadMemberInfo::slot_index` 保持稳定槽位身份。RouteSplit 不创建 captain、`UnitSquadInfo`、`SquadCaptainInfo` 或 follow chain。

### AttackMove 与路径拆分

新命令立即清理同 squad 上一次分发的成员路线，只对有效成员计算质心，并在 squad 实体上创建一个 `Aoe2xNavigationDestination`。因此每次命令只产生一次 pathfinding query。

中心路径按最多一个 tile 的间隔重采样。阵型生成器从完整列数逐步生成到单列 profile。动态规划在每个采样点选择 profile：

1. 优先累计宽度最大（窄化惩罚最小）；
2. 同分时优先 profile 转换次数最少；
3. 只允许相邻宽度之间转换，保持变化连续且确定。

每条候选转换都使用成员自己的 collider 调用 `AoeLogicMap::static_safe_fraction`。只有所有有效成员都能安全通过，转换才可用。最终先在临时数据中生成所有成员的 `Aoe2xRoutePlan`，全部成功后一次性写回；无可行拆分时命令整体 `Failed`。本阶段不做动态避障，也不为成员额外寻路。

死亡、Released、被销毁或缺少必需组件的成员被忽略，槽位不压缩；全部成员无效时命令失败。

## 6. 变更检查清单

- 新系统的组件访问声明是否完整；
- phase 是否与生产/消费关系一致；
- 是否与另一条可替换 pipeline 竞争同一请求或命令资源；
- spawn 和路线分发是否保持事务性；
- 失败时是否移除临时 navigation/planning 组件；
- 是否覆盖地图边界、静态障碍、命令替换、成员失效和旧 pipeline 回归测试。
