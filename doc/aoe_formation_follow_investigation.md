# AOE Formation 跟随与窄路缩宽调查记录

更新时间：2026-08-29
状态：P0 跟随逻辑与通用缩宽均分已实现并加入回归测试；Preview 诊断与大规模动态性能复核仍待完成

## 1. 当前开发上下文

当前开发重点已经从 `aoe2x` 转回现有 `aoe` gameplay。`aoe2x` 暂时搁置，不应把本轮 Formation 代码放回 `aoe2x`。

功能和性能验证统一使用：

- `example/aoe_gameplay_squad_preview`
- 1～6 档总单位数分别为 128、512、2000、5000、10000、20000。
- 4 档为每方 2500 个单位：938 个 `camel_scout`、1562 个 `archer`。
- 当前 Preview 使用 modular Formation、NavMesh corridor/Funnel RouteSplit、同步 MovingControl。
- 当前 Preview 的 local avoidance、global motion 和碰撞均为 pass-through/disabled，便于单独观察路线与 Formation 行为。因此重叠不会被避障系统修正。

Formation 已拆成模板组合的独立模块：

1. `SquadLayout`
2. `RouteSplit`
3. `MovingControl`
4. `AttackControl`
5. `CommandCompletion`

当前主要实现为：

- Layout：`AoeFullSquadLayoutModule`
- RouteSplit：`AoeNavMeshRouteSplitModule`
- MovingControl：`AoeSynchronizedFormationMovingControlModule`
- Attack/Completion 当前仍以 pass-through 为主。

关键代码入口：

- `aoe/include/aoe/AoeFormation.hpp`
- `aoe/include/aoe/AoeFormationLayout.hpp`
- `aoe/include/aoe/AoeFormationFollow.hpp`
- `aoe/include/aoe/AoeFormationRoutePlan.hpp`
- `aoe/src/AoeFormationModular.cpp`
- `aoe/src/AoeFormationFollow.cpp`
- `aoe/src/AoeFormationMovingControl.cpp`
- `aoe/src/AoeFormationRouteSampling.cpp`
- `aoe/src/AoeFormationWidthSchedule.cpp`

当前工作区包含较多尚未提交的 Formation、RouteSplit、Follow、Preview 修改。后续处理时必须保留这些修改，不应通过 reset/checkout 覆盖整个工作区。

## 2. 已确定的 Formation 路线模型

### 2.1 Layout 与自然跟随链

`AoeSquareFormation` 会：

1. 按兵种标签优先级和 ordinal 对单位做稳定排序。
2. 按行填充 Square Formation。
3. 每一列是一条自然跟随链：
   - `chain_index` 表示列。
   - `chain_order == 0` 表示列队首。
   - 后续成员按 `chain_order` 串成链。

默认优先级为：

- `spearman`：300
- `cavalry`：200
- `scout`：100
- `archer`：-100

`camel_scout` 同时拥有 `cavalry` 和 `scout`，所以其优先级为 300；弓手优先级为 -100。混编时骆驼位于阵型前部。

### 2.2 RouteSplit 的所有权粒度

当前 RouteSplit 不是给每个 unit 单独规划完整路径，而是只给每条自然列的队首生成 `AoeNavigationPath`。

- 路线持有者保存在 `AoeFormationRouteSplitState::units`。
- 普通成员通过 `AoeFormationFollow` 采样路线源的历史路线位置。
- 2500 人的自然 Square Formation 为 50×50，因此每方只有约 50 个独立路线持有者。

这样可以把寻路和路线推进的主要成本控制在列数规模，而 MovingControl 保持每 tick 的 O(unit count) 遍历。

### 2.3 Unit action 与缩宽动作

当前 unit action 的优先级基础为 `UnitAction`：

- `AoeNavigationPath` 默认优先级为 0。
- `AoeFormationFollow` 默认优先级为 1。

RouteSplit 会给自然列队首以及发生切分的 segment head 生成
`AoeUnitActionChain`，其中可以包含：

- `NavigationPath`
- `FormationFollow`
- `FormationDetachFollow`

MovingControl 是这些 Formation 路线 action 的执行位置：根据路线进度激活临时 Follow，并在计划的 detach 边界移除临时 Follow。非队首 segment head 在 detach 后恢复为自然列内对前一成员的 Follow。

该设计必须支持：

- 一条路线出现多个窄路，而不是只处理一个 bottleneck。
- 每次缩宽具有独立的 attach/detach 区间。
- 缩宽和还原可以按列分波次发生。
- 临时需要跟随的单位位于自身后方时应等待，不能倒退。当前临时 Follow 已包含基础的禁止后退处理，但完整行为仍依赖正确的拓扑。

### 2.4 Width Schedule

阵型宽度计算和缩宽变体由 LayoutGenerator 负责；RouteSplit 使用 `AoeFormationWidthSchedule` 描述沿路线进度发生的宽度变化。

宽度 schedule 可以包含多个 stage，并为每个 chain/slot 生成各自的 shrink/restore progress window。这是未来支持多次缩宽的基础，不能退化为仅记录一个“当前是否缩宽”的布尔状态。

## 3. 问题一：混合兵种的跟随间隔持续放大

### 3.1 现象

一条跟随链中同时存在高速兵种和低速兵种时，后半段的间隔会越来越大。典型情况为列首是骆驼、后续成员是弓手。

### 3.2 已确认根因

`aoe_synchronized_follow_motion_system()` 中的 `shared_speed` 只遍历：

```cpp
split->units
```

这里保存的是拥有独立路线的列队首，不包含普通 follower。因此当前取得的是“路线持有者的最低速度”，不是“跟随链所有成员的最低可持续速度”。

4 档中：

- 每方 2500 人，自然 Formation 为 50 列。
- 938 个骆驼在排序后位于 1562 个弓手之前。
- 因此前 50 个 slot，也就是全部自然列队首，都是骆驼。

兵种移动参数为：

| 兵种 | 基础速度 | catch-up ratio | 最大追赶速度 |
| --- | ---: | ---: | ---: |
| `camel_scout` | 1.45 | 1.15 | 1.6675 |
| `archer` | 0.96 | 1.15 | 1.104 |

列队首按照接近 1.45 的共享速度前进。普通弓手的 cruise speed 会被自身速度限制为 0.96；即使进入 catch-up，最大也只有 1.104，仍低于列首的 1.45。因此弓手不可能追回间距，链条只能不断拉长。

相关代码：

- `aoe/src/AoeFormationMovingControl.cpp:132-171`
- `aoe/include/aoe/AoeFormationLayout.hpp:125-174`
- `res/aoe_units/camel_scout.json`
- `res/aoe_units/archer.json`

### 3.3 为什么现有测试没有发现

当前 Follow/catch-up 集成测试主要使用同一种 `test` unit，所有成员基础速度相同。现有测试验证了“掉队成员能在 catch-up ratio 内加速”，但没有覆盖：

- 路线持有者比 follower 快。
- follower 的最大追赶速度仍低于队首速度。
- 同一自然列中存在多种基础速度。

## 4. 问题二：4 档缩宽后重叠并卡住

### 4.1 截图表现

剪贴板截图中，大 Formation 在转弯和窄路附近形成多条交叉、折叠的长带，并有大量 unit 重叠。该形状符合“同一自然列的队首和普通成员使用了不同路线源”的特征，不像普通的局部避障拥堵。

由于 Preview 当前禁用了碰撞和局部避障，错误路线产生的重叠会原样保留，但禁用避障本身不是停滞的根因。

### 4.2 已确认的拓扑错误

RouteSplit 提交路线后始终调用：

```cpp
apply_follow_topology(reg, installed_follow_plan, false);
```

`false` 会安装自然列拓扑：

- 每条自然列的普通成员以本列队首作为 `route_source`。
- 普通成员的 `distance_from_route_source` 只包含自己在自然列中的纵向距离。

进入窄路后，MovingControl 根据 action 只修改需要合并的“列队首”：

- 临时列队首跟随前一列的具体队尾。
- 临时列队首的 route source 改为目标 lane 的 root leader。
- 该列其余普通成员的 `AoeFormationFollow` 没有同步修改。

因此同一列在运行时被拆成了两套路线来源：

```text
临时列队首 -> 合并 lane 的 root route / 前一列队尾
列内普通成员 -> 本列原始 route / 原始距离
```

当多个自然列缩到同一条 lane 时，普通成员仍使用各自从 0 开始计算的列内距离，没有加入前面整列长度和 inter-chain gap。多列成员会被映射到同一 lane 上的相同进度位置，直接产生大面积重叠。

相关代码：

- 初始自然拓扑：`aoe/src/AoeFormationModular.cpp:1851-1854`
- 临时列首 action：`aoe/src/AoeFormationModular.cpp:1577-1625`
- action 执行：`aoe/src/AoeFormationMovingControl.cpp:52-117`
- 自然/完整合并拓扑生成：`aoe/src/AoeFormationFollow.cpp:234-294`

### 4.3 级联停滞机制

除重叠外，当前实现还存在进度冻结风险：

1. 临时列队首进入 `AoeFormationFollow` 后，其运动由 Follow request 驱动，不再沿自己的 `AoeNavigationPath` 正常移动。
2. 普通成员仍把这个临时列队首当作本列路线源。
3. 普通路线源进度使用 `member_route_progress()` 计算，只在 `path->current` 所指的当前 segment 上投影并 clamp。
4. 临时列队首偏离自己的原路线，或者沿合并 lane 走出该 segment 后，`path->current` 不一定能继续推进，路线源进度可能冻结在 segment 端点。
5. 本列普通成员采样到的历史锚点随之冻结，队尾停住。
6. 下一条临时列队首又在等待这个队尾，于是后续列发生级联停滞。

`project_member_route_progress()` 虽然会扫描整条路线，并被 action 边界判断使用，但普通 follower 的 route source runtime 仍使用 O(1) 的 `member_route_progress()`，所以它不能消除上述冻结。

相关代码：

- `aoe/src/AoeFormationRouteSampling.cpp:28-49`
- `aoe/src/AoeFormationRouteSampling.cpp:51-82`
- `aoe/src/AoeFormationMovingControl.cpp:217-283`

### 4.4 为什么 4 档特别明显

4 档每方为 50 条自然链、每链约 50 人。通过当前 Preview 的窄路时，需要把大量自然列合并到较少 lane：

- 错误拓扑影响的列数更多。
- 每条受影响列包含更多普通 follower。
- 任一列队尾冻结都会阻塞下一列临时队首。
- 问题一中的高速骆驼队首/低速弓手 follower 又会进一步拉长队列。

因此 4 档会形成稳定可见的重叠形状并最终卡住。1～3 档即使存在同类缺陷，较少的链数和较短的链会使问题不易触发或不易放大到整体停滞。

## 5. 不能采用的简单修复

### 5.1 不能只调用一次 `apply_follow_topology(true)`

当前代码确实能够通过 `make_formation_follow_assignments(plan, true)` 生成完整合并拓扑，但不能在“发现任意临时 Follow”时把整个 Squad 全部切换到 merged：

- shrink/restore 是按列分波次发生的。
- 同一 tick 可能只有部分列已经 attach。
- 一条路线可能有多个窄路和多个 width stage。
- 不同 group/lane 的 attach/detach 状态相互独立。

单个 `AoeFormationFollowPlan::merged` 布尔值无法表达部分合并状态。

### 5.2 不能依赖 catch-up 掩盖速度配置错误

若 follower 的最大追赶速度低于队首速度，无论提高 spacing gain 还是更频繁触发 catch-up，都不可能收敛。必须先保证队列的基准进度速度不超过该队列中最慢成员的可持续速度。

### 5.3 不能把重叠归因于局部避障

局部避障未来可以处理小误差和动态冲突，但不能修复 Formation 路线拓扑错误。让多个 chain 共享同一 lane 却不重新累计纵向距离，理论目标位置本身就是重叠的。

### 5.4 实现必须遵守的运行时约束

- 必须区分自然 chain 队首的物理 route progress 和当前连通 group 的 root progress。包含自然队首的 segment Attach 后，该 chain 队首的物理 progress 为 `root_progress - base_distance`；只切分 chain tail 时仍以自然队首自身路线维护 chain progress。Squad 的 lead/slowest 只比较当前各 group 的 root，不能把有意排在 root 后方的 attached segment 当成独立落后 lane。
- MovingControl 每 tick 的顺序固定为：更新单调 progress、消费全部 attach/detach 边界、得到稳定 topology、聚合 group speed、发布 route owner/follower motion request。边界 tick 不能混用新 topology 和旧速度组。
- 每条自然 chain 保留自身物理 progress；当前缩宽映射以 active segment binding 的 `natural_chain/first_member/member_count/root_chain/base_distance/preceding_tail/token` 为唯一权威。成员只保存自然 chain 内的局部距离，不能再缓存一份可能与 binding 分叉的 route source 和累计距离。
- Attach/Detach 以自然 chain 中的连续 segment 为原子单位并且必须幂等。Attach token 重放不得重复累计距离；Detach 只能移除匹配 token 的临时关系，旧 stage 的 token 不能破坏新 stage。同一自然 chain 可在一个 stage 被切成多个互不重叠的 segment。
- `N` 个成员缩到 `K` 条可通行 lane 时，每条 lane 的目标人数只能是 `floor(N/K)` 或 `ceil(N/K)`；每个成员必须且只能出现一次，lane 最大长度差不得超过 1。余数优先分配给靠中心的 lane，以保持外形稳定。
- 每条 lane 先选择一个顺序不交叉、横向移动成本最小的自然 root chain；root 保留满足目标容量的前缀，非 root chain 及 root 超出容量的尾部按连续 segment 填充所有 lane 的剩余容量。算法不得写死 4 档或任意预设人数/列数。
- lane 数等于自然 chain 数时必须保持原拓扑，不做无意义的切分或重排。lane 提取必须按 `chain_index` 取队首后再按横坐标去重，兼容末行不满和多个自然队首映射到同一窄 lane。
- route progress 必须单调不回退。完整路线投影只允许用于初始化、断言或低频异常恢复；普通 tick 不得为每个 follower 或 chain 扫描完整 waypoint 列表。
- group 的可持续 progress speed 为所有有效成员 `movement.speed / route_speed_ratio` 的最小值。`catch_up_speed_ratio` 只允许修正瞬态误差，不能抬高基准速度。
- active binding、route source 或 route sample 无效时必须 fail closed，并清除本 tick 的 Formation motion request，不能静默沿用上一 tick 的速度或目标。
- 普通 tick 复杂度保持 `O(unit count + chain count + active segment count)`；边界 tick 只更新受影响的 segment binding，不全量重建 Squad 的 Follow 组件。容量均分与 root 匹配只在 RouteSplit 规划阶段执行。

## 6. 接下来要做的待办

### P0：修复跟随链基准速度

- [x] 为自然链和临时合并链计算“有效进度速度”。
- [x] 速度统计必须包含完整 Follow chain 的所有有效成员，而不只是 route owner。
- [x] 将每个成员的基础速度、路线段 `speed_ratio` 纳入换算，得到该成员可持续的 route-progress speed。
- [x] 部分合并发生时，以当前连通 Follow group 为速度同步范围；不要无条件把整个 Squad 的所有 lane 降到全局最低速度。
- [x] catch-up 只用于消除暂时误差，不能参与抬高队列的基准速度。
- [x] 保持每 tick O(unit count)，不要为求最低速度引入逐链排序。

### P0：用显式的部分合并拓扑替代单一 `merged` 布尔状态

- [x] 为每条自然 chain 保存物理 progress，并为每个 active segment 保存 route source、root chain、base distance、前驱 tail 和 active follow token。
- [x] Attach action 必须原子地切换一个连续 segment，而不只是切换其 head。
- [x] 切换后，该 segment 所有成员都采样相同 root route，并使用累计后的 `base_distance + member-local distance`。
- [x] Detach action 必须把 segment head 和内部成员恢复为自然 chain 拓扑。
- [x] 每个 width stage/group 独立维护 attach/detach 状态，以支持同一路线多个窄路。
- [x] 不应在每 tick 全量重建所有 `AoeFormationFollow`；只在 action 边界更新受影响 segment，成员通过间接 topology state 读取当前映射。

建议状态关系：

```text
自然 chain
  -> 单调物理 progress
  -> 0..N 个互不重叠的 active segment binding
       - first member / member count
       - root chain / root unit
       - preceding tail
       - accumulated base distance
       - active token/stage
  -> segment 内成员使用 binding + segment 内局部距离
  -> 未覆盖成员保持自然 chain 拓扑
```

### P0：缩宽后把成员均分到全部可通行 lane

- [x] 按所有可通行 lane 计算 `floor(N/K)` / `ceil(N/K)` 容量，最大长度差不超过 1。
- [x] 余数采用中心优先的确定性分配，root 采用保持左右顺序的最小横移匹配。
- [x] 支持切分非 root chain，并支持切分超过目标容量的 root tail。
- [x] 保证 segment 连续、成员无遗漏且不重复；不依赖 4 档、50×50 或固定 lane 数。
- [x] 不缩宽时保持自然 chain 一一对应，不创建额外 segment action。
- [x] 覆盖 5→1、5→2、7→2、17→4、2500→48、2500→10 等非整除和大规模用例。

### P0：消除 Follow 路线源进度冻结

- [x] 路线源的单调 progress 不应完全依赖 `AoeNavigationPath::current`。
- [x] 临时 Follow 生效时仍应维护一个单调的 route-progress 标量。
- [x] Attach 时记录 progress；Follow 过程中根据 root route/当前 topology 推进；Detach 时同步 `AoeNavigationPath::current` 到 detach progress。
- [x] 避免每个 follower 每 tick 扫描完整路径。整条路线投影只能用于边界校正或低频恢复，不能成为 2 万单位常规路径。
- [x] 增加“progress 不回退”和“当前 segment 能越过 waypoint”的断言或诊断。

### P1：补齐自动化验证

- [x] 混合速度自然链：高速队首 + 低速 follower，验证间距长期不发散。
- [x] follower 最大追赶速度低于队首原始速度时，验证队首基准速度会被降到链最低速度。
- [ ] 50×50 Formation 缩到多个 lane，验证所有成员目标位置不重叠。
- [x] 验证部分 chain attach 时，未 attach chain 保持自然拓扑，已 attach chain 使用 merged topology。
- [x] 验证逐列 restore 后拓扑和路线进度完整恢复。
- [ ] 验证连续两个及以上窄路的 attach/detach action 顺序。
- [ ] 验证 4、5、6 档都能完成窄路通行；RouteSplit 仍保持“任一必需列失败则整队路线失败”的现有约束。
- [ ] 测试应同时覆盖直路和转弯窄路，特别是临时列首偏离自身自然路线的情况。

### P1：增加 Preview 诊断信息

- [ ] 显示每个 Squad 的自然 chain 数、当前 lane/group 数、已 attach chain 数。
- [ ] 显示每个 active group 的最低基础速度和最终 progress speed。
- [ ] 显示 route source progress 是否冻结、多久未推进。
- [ ] 可选择绘制某一 chain 的：自然路线、当前 root route、实际成员位置、采样锚点。
- [ ] 显示当前 action 的 stage/token、attach/detach progress。
- [ ] 保持诊断可关闭；关闭时不能给正常运行引入明显成本。

### P2：性能复核

- [ ] 4 档先验证正确性，再采集 MovingControl 平均/最高耗时。
- [ ] 依次运行 5、6 档，确认 MovingControl 保持线性增长。
- [ ] 分开记录 topology boundary tick 和普通 moving tick；边界更新允许较高但应是低频成本。
- [ ] 检查新增 topology state 是否引入大量随机 ECS lookup；必要时按 chain 预构建连续 runtime 数据。

## 7. 建议的实施顺序

1. 先增加混合速度和部分合并拓扑的最小失败测试。
2. 把 `shared_speed` 改为按当前连通 Follow group 统计最低可持续进度速度。
3. 引入自然 chain progress 和 active segment 的显式 topology binding。
4. Attach/Detach action 改为切换连续 segment binding。
5. 把 follower 路线采样改为读取当前 segment binding，并使用累计 base distance。
6. 独立维护 route source 的单调 progress，确保 Follow 期间不会因 `path.current` 冻结。
7. 通过小规模双窄路测试后，再验证 Squad Preview 4、5、6 档。
8. 正确性稳定后再做数据布局和 ECS 访问性能优化。

## 8. 当前调查边界

最初结论来自当时工作区代码、4 档配置、单位定义和剪贴板截图。2026-08-29 已按第 5.4 节约束完成 P0 跟随与通用缩宽均分实现；尚未勾选的 P1/P2 项仍是本轮实现边界之外的后续工作。

仓库中的 `aoe_gameplay_squad_profile.log` 时间早于本次截图，记录的是旧版本中 RouteSplit 失败的状态，不能用它证明本次“运动后卡住”的实时状态。后续实现时应重新运行 Preview 并采集新的 telemetry。
