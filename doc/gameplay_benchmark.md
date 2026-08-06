  ### 万单位实际验证

  > 性能优化记录（导航热点）：`GridAStarPathfinderLogic::find` 增加直线视野快速路径
  > （起终点直线可达即跳过整个网格 A\*），使 navigation 在 5000 单位场景由约
  > 141ms 降到约 7–12ms、20000 场景由约 1426ms 降到约 63–79ms（≈12–22×），
  > 且各交战阶段稳定。GPU 全局运动（`aoe_gpu_motion`）回读由同步 `glGetBufferSubData`
  > 改为双缓冲 SSBO+fence 异步流水线（决策滞后 1 fixed tick，安全钳制仍每 tick 执行），
  > 消除每 tick 的同步 GPU stall。`collect_squad_targets` 的候选收集由
  > O(candidates×members) 降为 O(candidates)。navigation 已不再是主导项；持续满交战下
  > 主导项转为 GPU unit_flow（万单位下 GPU 解本身饱和）与前线 squad_control。
  > 现有 `aoe_map_tests`/`aoe_gameplay_tests`/`aoe2_gameplay_bridge_tests` 全部通过。

  使用启用了 GLD_ENABLE_PERFORMANCE_MONITORING 的 Profile 构建运行了：

  $env:GLD_AOE_STRESS_PRESET = "6"
  $env:GLD_AOE_SYSTEM_PROFILE = "E:\code\gld\build\profile\aoe_squad_20k_final.csv"
  $env:GLD_AOE_SYSTEM_PROFILE_SECONDS = "1"

  Profile 自动检测到场景完成驻留，随后采样并正常退出：

  [aoe_squad_profile] stable; capturing 1.0 seconds
  [aoe_squad_profile] wrote E:\code\gld\build\profile\aoe_squad_20k_final.csv

  采样结果确认：

  - Gameplay units：20000
  - Render units：20000
  - 两队 SpawnRequest 和 AoE2 SpawnRequest 已清空
  - 双方 AttackMove 已成功下发
  - 没有 GPU unit handle exhaustion
  - 没有 spawn error
  - 没有地图越界或 batch 容量错误
  - Profile CSV 正常写出
  - 进程正常自动退出

  本机最高档采样约为：

  - 总帧时间：1564.87 ms
  - Fixed ticks：3
  - Gameplay fixed tick 总耗时：约 1538.79 ms
  - Navigation：约 1425.78 ms
  - Squad control：约 37.57 ms
  - GPU/global unit flow：约 32.90 ms
  - Local avoidance：约 19.83 ms
  - AoE2 animation：约 0.82 ms
  - AoE2 batch：约 2.82 ms

  因此最高档已经能有效暴露当前的大规模寻路瓶颈；它符合“可加载、可渲染、可下命令、可采样”的压力测试目标，但不承诺交互帧
  率。
---

## aoe2x 编队压测（example/aoe2x_formation_preview）

`aoe2x_formation_preview` 已扩展为 aoe2x 的压测台：环境变量驱动的规模档位、按档位放大的地图、
多编队独立寻路，以及在 HUD 上直接显示各阶段耗时。

### 用法

```powershell
$env:GLD_AOE2X_STRESS_PRESET   = "5"     # 1..5 -> 500 / 2000 / 5000 / 10000 / 20000 单位
$env:GLD_AOE2X_SQUAD_SIZE      = "500"   # 每队单位数，上限 1024（CompactSquare 限制）
$env:GLD_AOE2X_RENDER_DETAIL   = "lite"  # full | lite | off，缺省 >2000 单位自动 lite
$env:GLD_AOE2X_PROFILE_SECONDS = "16"    # 非 0 时到点自动退出，并把每个 0.5s 窗口打到 stdout
.\aoe2x_formation_preview.exe
```

档位映射：单位数拆成 N 个 squad（默认 500/队），在地图左侧铺开，目标为各自关于地图中轴的镜像点；
全部到达后自动反向再来一趟，使运动与寻路持续处于负载中。运行中按 `G` 循环切换渲染精度。

| preset | units | squads | map | 备注 |
|--------|-------|--------|-----|------|
| 1 | 500 | 1 | 192×120 | 默认，等价于原 preview 场景 |
| 2 | 2000 | 4 | 192×120 | full 渲染上限档 |
| 3 | 5000 | 10 | 320×200 | |
| 4 | 10000 | 20 | 400×250 | |
| 5 | 20000 | 40 | 512×320 | |

### 实测（x64-Release，vsync 关闭，稳态 0.5s 窗口）

| preset | units | detail | fps | frame ms | sim ms | formation avg/peak ms | gizmo ms | gizmo verts |
|--------|-------|--------|-----|----------|--------|------------------------|----------|-------------|
| 1 | 500 | full | 775 | 1.29 | 0.11 | 0.106 / 0.324 | 0.89 | 26,600 |
| 2 | 2000 | full | 143 | 6.97 | 0.66 | 0.645 / 1.460 | 4.91 | 104,624 |
| 3 | 5000 | lite | 621 | 1.61 | 1.01 | 1.004 / 3.551 | 0.46 | 10,766 |
| 4 | 10000 | lite | 244 | 4.11 | 2.66 | 2.654 / 6.931 | 1.15 | 20,894 |
| 5 | 20000 | lite | 210 | 4.77 | 3.10 | 3.091 / 7.206 | 1.40 | 41,238 |

渲染精度对比（preset 5，20000 单位，同一稳态）：

| detail | fps | gizmo ms | gizmo verts | sim ms |
|--------|-----|----------|-------------|--------|
| full | 19.0 | 39.77 | 1,041,020 | 4.47 |
| lite | 210 | 1.40 | 41,238 | 3.10 |
| off | 336 | 0.04 | 1,090 | 2.81 |

**结论**：不做渲染 LOD 时，2 万单位的 gizmo 构建（约 104 万顶点）要 ~40ms，会把仅 ~4.5ms 的仿真
耗时完全淹没。`lite`（每单位 1 条 2 顶点方向线）让仿真耗时成为可读数据。稳态下 aoe2x 的主导项是
`aoe2x_formation`（每单位 O(1) 的跟随链求解），2 万单位约 3ms；`aoe2x_pathfinding` 稳态几乎为 0，
因为 `captain_route_desired_velocity` 在拿到路线后即移除 `Aoe2xNavigationDestination`，
每条指令只寻路一次。

### 压测暴露的寻路热点与修复

放大地图后首次寻路出现巨大尖峰，逐步定位并修复：

| 阶段 | preset 5 首次寻路帧耗时 | high_level_expanded | 说明 |
|------|-------------------------|---------------------|------|
| 基线 | **218,592 ms** | 2,524,169 | 不可用 |
| 修复 1 | 4,408 ms | 2,524,169 | `local_path` 工作区复用 |
| 修复 2 | 2,519 ms | 402,271 | 抽象层 Dijkstra → A\* |
| 修复 3 | **1,288 ms** | 402,271 | 簇内全对 A\* → 每 portal 单源 Dijkstra |

1. **`local_path` 每次调用分配整张地图大小的 cost/parent 数组**，而搜索只在一个 `cluster_size²`
   的簇内进行。512×320 地图上每次调用要清 1.3MB，建图阶段约 127 万次调用 ≈ 1.6TB memset。
   改为**带 generation stamp 的复用工作区**（只重置真正访问过的格子）与复用 open 堆。
2. **抽象层（portal 图）是无启发的 Dijkstra**。加入到目标格的 octile 启发（可采纳、且一致），
   并把提前终止判据从 `cost >= best` 改为 `estimate >= best`，扩展数减少 **6.3×**，最优性不变。
3. **建图时对每个簇做全对 portal A\***（每簇 O(P²) 次搜索）。改为**每个 portal 一次簇内单源
   Dijkstra**，一次得到到该簇全部 portal 的距离，搜索次数从 O(P²) 降到 O(P)，产出的边集与代价完全相同。

合计 **约 170×**。三处均为纯实现优化，路线输出逐字节一致（`high_level_expanded`、
`waypoints 14650 -> 174` 与修复前相同），`aoe2x_pathfinding_tests` /
`aoe2x_formation_tests` / `aoe2x_gameplay_system_tests` 全部通过。

LOS string-pull 平滑（提交 `869b646`）在该场景收益为 **14,650 → 174 航点（约 84×）**。

### 编队阵亡处理（跟随链压缩与队长继承）

在此之前，编队中任何一个单位失效都会让整个 squad 的指令 `Failed`。现在阵亡由跟随链自身吸收。

**三段生命周期**（`Aoe2xUnitState`）：

| 状态 | 绘制 | 编队成员 | 组件 | 由谁推进 |
|------|------|----------|------|----------|
| `Alive` | ✔ | ✔ | 完整 | — |
| `Dead` | 死亡表现 | ✘ | **完整** | `Aoe2xUnitLifecycleSystem`，倒计时 10 帧 |
| `Released` | ✘ | ✘ | 仅 `Aoe2xPooledUnit` | 生命周期系统当帧移入 `Aoe2xUnitPool` |

`Dead` 的 10 帧窗口供死亡表现和仍需读取尸体数据的逻辑使用。倒计时结束即进入 `Released`：实体保留在
独立池中复用，但位置、碰撞、移动、编队、路线等 active 组件全部移除，因此普通 EnTT view 不会遍历到它。

**链压缩**：`r_i = pos(followed) - pos(self)` 描述的是相邻槽位之间的边，而不是 unit 的固有属性。
`SquadInfo::slot_edges` 保存独立的槽位边序列；压缩时幸存者按新序号继承前方槽位边并重接 `followed`，
于是后继顶上阵亡者的槽位，蛇形方阵的水平、垂直和换行几何也不会被错误混用。

**队长继承**：新队长接管 `SquadCaptainInfo` / `Aoe2xRoutePlan` / `FormationMotionState`，
**绝不重新 emplace `Aoe2xNavigationDestination`**——每条指令只寻路一次是 aoe2x 的核心性能前提，
每次队长阵亡都重跑 HPA\* 会把刚优化掉的 1.3s 级开销请回来。路线按游标**重建**：丢掉已消费的前缀，
把阵亡队长的位置作为首个航点，`route_start` 设为新队长当前位置。前缀必须丢弃，否则那些已经走过、
位于新队长**前方**的航点会成为线段判据里的 `previous`，使 `passed` 反向成立，一帧之内吞掉整条路线。
首个「归队点」让新队长沿一段已被验证过的路线归位；开阔地不付出任何代价，因为
`advance_route_cursor` 的 LOS 跳过会在直线可行时立刻把它丢掉。

**随机杀伤压测**（300 组随机障碍地图，16 单位/队，共 294 次队长阵亡）：

| 杀伤模式 | 完成行军 | 全灭 | 卡住 |
|----------|----------|------|------|
| 不杀 | 300/300 | 0 | 0 |
| 只杀非队长 | 300/300 | 0 | 0 |
| 含队长 | 288/300 | 7 | **5** |

全过程链不变量（单链、无环、根为队长、成员全存活、仅队长持有 `SquadCaptainInfo`）逐帧成立，
额外寻路查询数恒为 **0**，尸体在释放窗口后全部回收无泄漏。

**已知问题**：继承的折线只对**阵亡队长所在的车道**做过 LOS 验算，而新队长在其后方一格且有横向偏移，
因此约 **1.7%** 的情况下会在原车道畅通的一段上正面楔进静态障碍（`static_constrained_displacement`
归零且无滑移）。只杀非队长时该现象为 0，可确认与链压缩无关。本轮不处理；后续可加「队长连续 N 帧
零位移则重发一次寻路」的看门狗，代价是每次真正楔死付一次 HPA\*（本次压测下为 5/294）。

### 压测暴露的队长卡死与修复

2w 压测中出现「编队永久停在障碍旁、`march legs` 不再增长」。用 400 张随机地图（3–7 个 AABB/圆障碍、
单队行军、4000 tick 预算、120 tick 内位移 <0.05 判定卡死）复现，逐项定位到四个独立缺陷：

| 阶段 | 400 场景卡死数 |
|------|----------------|
| 基线 | 47 (11.8%) |
| + 静态碰撞切向滑动（`static_constrained_displacement`） | 17 |
| + 指令速度与碰撞后速度解耦（`commanded_speed`） | 5 |
| + 航点可视跳过（`advance_route_cursor` LOS skip） | 3 |
| + `ellipse_segment_enter` 尺度相关早退修复 | 3（穿透消失）|
| + 路线避障间隙 `obstacle_gap`（≥0.05） | **0** |

1. `integrate_motion` 把 `static_safe_fraction` 当作整体位移的标量缩放，没有沿墙滑动，任何擦碰即全停；
   且路线是确定性的，重新寻路只会返回同一条折线（实测 repath-on-stall 440 次全部无效），无法自愈。
2. `move_along_direction` 用**碰撞后**的 `locomotion.velocity` 作为加速度积分基准，擦墙会让加速度每 tick
   从零重启，表现为 0.0001/tick 的蠕动。改为在运动状态里保存指令速度 `commanded_speed`。
3. `advance_route_cursor` 的 `passed` 判定在队长横向漂移后永远不成立，导致绕着一个永远消费不掉的航点打转。
   改为额外跳到直线可见的最远后继航点（上限 8 个）。
4. `aoe` 共享模块 `ellipse_segment_enter` 的 `a <= Epsilon` 早退是**尺度相关**的（`a = |Δ|²/r²`），
   任何短于 ~r/316 的步长都会直接穿过圆形障碍。改为 `b >= 0` 早退 + 数值稳定根式 `2c/(root-b)`。
5. `Aoe2xPathfindingSettings::obstacle_gap`（默认 .25）在**路线成形**时用 `radius + gap` 的 clearance
   （栅格通行性 + 直连快速路径 + LOS 平滑），让折线不贴着障碍走；目标合法性与实际碰撞仍用裸半径。
   若加间隙后无解则自动用原 clearance 重算一遍，保证窄通道不会因为间隙变成不可达——实测 gap 从 0.05
   一路加到 0.75，`failed` 数始终为 4（与 gap=0 相同）。

修复后 2w 档（preset 5）：`running 40/40 | march legs 5`（基线为 `2/40 | legs 1` 永久停滞），
sim 3.9–4.2 ms、170–178 fps，寻路 `rebuilds 1 / no-path 0`（间隙回退未触发，无额外建图开销）。

### 已知剩余项

- 每个边界穿越格都会生成一对 portal（为保持精确连通性），512×320 / cluster 8 下 portal 数约 8 万，
  抽象图规模与栅格同阶。若要进一步降低建图与查询成本，需要把连续的边界穿越归并为「入口」并只保留
  代表 portal，但这会改变连通性语义，需另行评估。
- `CompactSquareFormation::generate` 硬限单队 1024 单位，且 `valid_layout` 的重复点检查为 O(n²)。
  当前按 500/队编排未触发，若要支持单队上万需一并处理。
