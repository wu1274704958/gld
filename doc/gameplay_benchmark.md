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