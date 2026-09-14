# AoE2DE 资源导出器技术说明

本文总结 `tools/aoe2de_export` 的当前实现、已经验证的资源语义，以及渲染端必须遵守的加载约定。它用于后续维护导出器、生成新的 Unit/Projectile 缓存，以及将缓存接入其他引擎。

> 2026-09-07 逆向更新：SLD 解码器的 mask offset、差分关键帧及合并输出矩形已经按官方路径修复；修复后的 2,885 帧 Player Color 与官方原代码模拟输出一致。此前导出的资源仍需重新导出。当前 shader 尚未完整复现官方混色，函数地址、对照结果及剩余工作见 [Player Color 完整链路调查](aoe2de_playercolor_reverse_engineering.md)。

## 1. 目标与边界

导出器从本机 Age of Empires II: Definitive Edition 安装目录读取：

- `.sld` Sprite 动画；
- Gameplay DAT 中的 Unit/Combat 元数据；

输出是供本地开发使用的版本化缓存，不是通用的 AoE2DE 资源再分发工具。生成的原始游戏资源不应提交或重新分发。

当前导出器负责图像、帧布局、脚点、基础碰撞和武器元数据，不负责生成具体引擎的 `UnitDef`、`WeaponDef` 或 Gameplay 行为。不同引擎应在缓存之上增加独立转换层。

## 2. 代码结构

| 文件 | 职责 |
| --- | --- |
| `tools/aoe2de_export/aoe2de_export.py` | CLI、资源发现、DAT 读取、帧校验、图集生成和 manifest 写出 |
| `tools/aoe2de_export/sld/sld.pyx` | SLD 二进制解析和 BC1/BC4 图层解码；代码复制/改编自 openage，必须保留其版权声明 |
| `tools/aoe2de_export/sld/texture.py` | 将 SLD 帧转换成 Python/Pillow 可用的图像对象 |
| `tools/aoe2de_export/unit_dat_map.json` | SLD Unit 前缀到文明与 DAT Unit ID 的稳定映射 |
| `tools/aoe2de_export/tests/test_aoe2de_export.py` | 导出参数、图层对齐、Player Color 原始通道保留和 manifest 的回归测试 |

依赖包括 Python、Cython、NumPy、Pillow 和 `genieutils-py`。普通独立 Graphic
导出不读取 DAT；使用 `--projectile-unit-id` 自动解析 Projectile Graphic 语义时
会读取 DAT，因此也需要 `genieutils-py`。

## 3. 构建与运行

构建本地 SLD 扩展：

```powershell
python -m pip install cython numpy pillow genieutils-py
cmake -S tools\aoe2de_export -B tools\aoe2de_export\build
cmake --build tools\aoe2de_export\build --config Release --target aoe2de_export_sld
```

验证扩展：

```powershell
python -c "import sys; sys.path.insert(0, r'E:\code\gld\tools\aoe2de_export'); from sld.sld import SLD; print('local SLD import ok')"
```

导出 Unit：

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --unit u_arc_archer `
  --animations idleA walkA attackA deathA
```

导出独立 Graphic，例如箭矢：

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --name p_arrow --graphics p_arrow_x2.sld --directions 32 --fps 30
```

Projectile 应优先通过 DAT Unit ID 导出，避免把时间帧误判为方向帧。例如手推炮
炮弹 Unit 368：

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --name p_ball --graphics p_ball_x2.sld --projectile-unit-id 368
```

列出可用 Unit：

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --list "u_inf_*" --page 1
```

运行回归测试：

```powershell
python -m unittest discover -s tools\aoe2de_export\tests -p "test_aoe2de_export.py"
```

## 4. 导出模式与目录

导出器支持四类导出入口及一个诊断入口：

- `--list`：按前缀发现 Unit Graphic；
- `--unit`：导出 Unit 动画，同时读取 DAT；
- `--building`：导出 Building 状态与 DAT，使用显式 Building 映射；
- `--graphics`：导出一个或多个独立 Graphic，不读取 DAT；
- `--dump-layers`：诊断性导出 SLD 的各个原始图层。

`--out` 表示缓存根目录，不是最终资源目录。输出布局为：

```text
<cache-root>/
  units/<unit-id>/
    manifest.json
    graphics/
      idleA.json
      idleA.png
      idleA_shadow.png
      idleA_playercolor.png
      ...
  buildings/<building-id>/
    manifest.json
    graphics/
      built.json
      destruction.json
      rubble.json
      ...
  graphics/<graphic-id>/
    manifest.json
    graphics/
      ...
```

Unit 默认使用完整 SLD 前缀作为资源 ID，`--name` 可以覆盖。资源 ID 仅允许字母、数字、`.`、`_` 和 `-`。

重新导出时，目标 `units/<id>`、`buildings/<id>` 或 `graphics/<id>` 会被完整删除再创建。导出器会先验证参数、输入 Graphic、DAT 和 Unit 映射，再执行删除；但仍不得将包含其他数据的目录误传给 `--out`。

Building 使用 schema 4、`kind: "aoe2de_building"`，其 DAT 映射来自默认的
`building_dat_map.json` 或 `--building-map`。建筑 Graphic 可能跨文明/时代共享，
因此不支持按前缀自动匹配 DAT Unit。首个样本可这样导出：

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --building b_afri_tower_age2
```

导出器按明确后缀寻找 `built`、`destruction` 与 `rubble`。`construction`、
`attack`、`open`、`closed` 没有源文件时记录为 `missing_source`；如需覆盖，在
Building map 的 `states` 中提供同一 graphics 目录内的完整 `.sld` 文件名。
`--building-directions` 默认为 1。DAT `combat.weapon_offset` 同时会作为未校准的
`anchors.muzzle_candidates` 输出，坐标空间为 `aoe2_dat_local`，不能直接当作 Recoil
炮口坐标使用。

## 5. SLD 图层语义

SLD 帧最多声明五类图层：

| Layer | 名称 | 当前处理方式 |
| --- | --- | --- |
| 0 | Main | 导出 RGBA Diffuse 图集 |
| 1 | Shadow | 导出独立阴影图集；运行时通常只上传灰度/R8 |
| 2 | Outline | 记录为 `unsupported`，当前不导出 |
| 3 | Damage mask | 记录为 `unsupported`，当前不导出 |
| 4 | Player Color | 解码 BC4 数据，保留解码器输出的 RGBA 四通道图集 |

Main、Shadow、Player Color 必须按 SLD 的物理帧序号对齐，不能只按各图层自身的数组索引配对。辅助图层缺帧时会插入透明占位帧并记录 `partial`；Main 缺少有效帧则整段动画无效。

图层状态包括：

- `complete`：所有有效物理帧都存在；
- `partial`：辅助图层有缺帧，已插入透明占位；
- `missing`：源文件没有该图层；
- `unsupported`：源文件存在，但当前导出器不输出；
- `invalid`：无法解码，或尺寸、脚点、帧布局无法安全对齐。

## 6. 帧方向、时间与图集布局

- 默认方向数为 16，可通过 `--directions` 修改；
- 默认帧率为 30 FPS，可通过 `--fps` 修改；
- `--scale auto` 优先选择 `_x2.sld`，不存在时回退到 `_x1.sld`；
- 源帧采用 `direction_major`：先排列某方向的全部动画帧，再排列下一方向；
- 每方向帧数为 `有效总帧数 / direction_count`；
- 不能整除方向数的尾部帧会在打包前删除，并写入 `unused_source_frames`。

普通 Unit/Building 动画在 JSON 中写入 `sampling_mode: "timeline"`。Projectile
使用 `--projectile-unit-id` 时，导出器从 DAT Graphic 读取 `angle_count`、
`frame_count`、`sequence_type` 和 `frame_duration`，校验它们的乘积与 SLD 物理帧数
完全相等，然后选择：

- `pitch_pose`：多角度、Sequence Type 2；运行时按弹道俯仰选择帧，例如箭矢；
- `time_loop`：单角度、Sequence Type 1 且具有正帧时长；运行时按时间循环，例如
  `p_ball` 的 1 方向 × 30 帧旋转动画。

不满足上述已验证组合的 Projectile 会在清理旧输出前终止，不能再用“总帧数就是
方向数”的经验规则猜测。Manifest 的 `projectile` 节点同时保留 DAT Unit/Graphic
ID、速度、弧度和原始 Graphic 维度，供 Gameplay 转换与诊断使用。

Main 图集使用固定大小的网格 Cell：Cell 宽高取所有帧裁剪尺寸的最大值，列数近似取总帧数平方根。每帧的实际 `x/y/w/h` 会写入 JSON。

Shadow 使用自己的裁剪尺寸和图集布局。SLD 的 Player Color mask header 没有独立尺寸或 offset，其源坐标来自对应 Main，不能使用 Shadow offset。官方最终输出矩形还会合并当前 Main 与相关图层关键帧的边界，不一定等于当前 Main header 的矩形。当前导出器强制复用解码后 Main 的图集布局、帧矩形和 UV；几何不一致时判为 `invalid`，但这不能检测或修复底层参考帧和块地址错误，详见完整链路调查第 4 节。

## 7. 脚点与坐标语义

每个帧记录包含：

```json
"foot": {
  "x": 123,
  "y": 178,
  "space": "frame_pixels_top_left"
}
```

`foot` 来自 SLD 当前图层的 hotspot，表示裁剪帧左上角坐标系中的脚点。渲染时应让这个像素点落在 Unit 的世界位置/地面锚点，而不是让图片中心落在 Unit 位置。

需要注意：

- 脚点是逐帧数据，不应假设整段动画完全相同；
- Main 与 Player Color 共享脚点和 UV；
- Shadow 可以有独立脚点和布局；
- Projectile 的 hotspot 同样属于 Sprite 表现锚点，不等同于 Gameplay 发射点或受击点；
- DAT `collision_size`、`outline_size` 也不是 Sprite 像素包围盒。

## 8. Player Color 的真实语义

### 8.1 它是连续混合权重

Player Color layer 不存储“蓝、红、绿”等最终颜色，也不存储玩家编号。AoE2DE 的 `SpritesSLD_ps` 只采样 Team Atlas 的 R 通道，在启用且 `R > 0` 时进入队伍色分支，并用 R 作为亮度变换后的基础色与队伍色目标之间的 `0..1` 混合权重。混合的第一项不总是原始 Diffuse。玩家编号和基础色由运行时另外提供。

### 8.2 BC4 解码中间格式

SLD Layer 4 使用 BC4 单通道块压缩。当前 Cython 解码器为了复用统一 RGBA 图像接口，临时输出四个 byte：

```text
(R = BC4 UNORM 解码值, G = 0, B = 0, A = openage RGBA 适配值)
```

G/B 在 `SLDLayerBC4` 中明确始终写为 0。A 的 0/255 是 openage 为复用统一 RGBA 图片接口生成的辅助值，不是 BC4 或 AoE2DE Player Color 的运行时通道；AoE2DE shader 完全不读取 A。只有 R 是权威数据。

BC4 每个 4×4 Block 保存两个端点和 16 个三位插值索引，所以 R 可能是插值得到的值，并非未经压缩的精确原始字节。

### 8.3 RGBA 原始解码输出

Player Color 图集格式为 `rgba8_bc4_decoded`。导出器保留 8.2 节所述四个通道，不做阈值过滤、索引翻转、Diffuse alpha 过滤、跨帧恢复、通道提取或几何平移。

运行时从 RGBA8 PNG 提取 R 并上传为 R8，使用 nearest 采样；shader 以原始归一化值直接进行颜色混合。G/B/A 只保留在导出文件中供离线检查，不上传 GPU；不得用 A 判断有效性，也不得把 R 当作 palette index。

旧的 `consensus3` 平滑会修改原始数据，目前 CLI 只接受：

```text
--playercolor-temporal-filter off
```

### 8.4 Shader 使用

当前 gld/Recoil shader 从 AoE2DE 的 `spritecolors.json` 默认值取得八个玩家基础色，以 Diffuse 亮度构造队伍色目标，再按 raw R 做连续混合：

```text
teamWeight = rawR / 255
最终颜色 = mix(Diffuse, 队伍色目标, teamWeight)
```

这只实现了 R 作为连续权重的方向，不能视为官方混色的完整复现。官方 CPU 在绘制前将队伍色常量 alpha 改写为 `dot(teamRGB, vec3(0.299, 0.587, 0.114))`，而不是直接使用 JSON 中的 alpha=1；shader 再结合 `gTeamLuminance`、`gTeamColorMode` 做分段变换。完整公式见 [链路调查第 6 节](aoe2de_playercolor_reverse_engineering.md#6-shader-的实际输入与公式)。底层解码现已修复，但旧缓存中的错误 mask 必须重新导出。

Player Color layer 必须用 nearest 采样；Diffuse 可使用 linear。预览器按 `M` 可切换 normal 和 R 灰度视图。G/B/A 如需检查，应直接查看导出的 RGBA8 PNG，不参与正常着色或 GPU 上传。

## 9. DAT 元数据

`--unit` 生成 schema 3 Unit manifest，并通过以下优先级选择 DAT Unit：

1. `--unit-id` 显式指定；
2. `--unit-map` 中的稳定映射；
3. 在指定文明中通过 Standing/Walking/Attack/Death Graphic 前缀唯一匹配。

零匹配或多匹配会在清理输出目录前终止。默认文明为 Gaia（`--civ-id 0`）。

`dat` 中目前可包含：

- DAT 来源、文明 ID、Unit ID、Unit 类型和映射来源；
- `collision_size`：X/Y 为地面平面半径，Z 为高度；
- `outline_size`：选择/轮廓范围，不替代 Gameplay Collision；
- Projectile Unit ID 和 Secondary Projectile ID；
- `frame_delay`、攻击 Graphic ID；
- `weapon_offset`；
- 命中率、散布、最小/最大射程、装填时间；
- 爆炸宽度与攻击等级；
- 爆炸伤害倍率、友军伤害倍率与 Unit 爆炸防御等级（DAT 版本提供时）；
- Projectile 数量和生成区域。

DAT 中合法的 `-1` ID 会原样保留。未提供的可选字段会省略，不使用 `null` 或随意构造默认值。

这些数据仍处于 AoE2DE 自身坐标和数值语义中。接入其他引擎时必须集中处理轴向、单位比例、朝向和时间单位转换，不能在 UnitDef/WeaponDef 中散落魔法常量。

## 10. Manifest 版本与运行时契约

- Unit manifest：`schema_version: 3`、`kind: aoe2de_unit`；
- 独立 Graphics manifest 和动画配置：schema 2；
- `export_settings.player_color.format` 必须为 `rgba8_bc4_decoded`；
- Runtime 必须校验 Main 与 Player Color 的图集尺寸、逐帧矩形、脚点和 UV；
- Runtime 应共享静态纹理资源，不能为每个 Unit 重复加载；
- Main 上传 RGBA8，Shadow 与 Player Color 上传 R8；
- Player Color Mask 使用 nearest；
- 动画帧由 `fps`、`direction_count`、`frames_per_direction` 和 `frame_order` 共同解释。

请求的动画不存在时记录为 `missing_source`，其他动画继续导出且命令可以成功。Runtime 不应把 missing 当成可加载动画。

项目当前约定不加载 Decay 动画：尸体播放 Death 最后一帧，并由程序控制透明度渐隐。这是 Gameplay/Renderer 策略，不是导出器格式限制。

## 11. 常见问题排查

### 队伍色区域整体错位

优先检查 Main 与 Player Color 是否按物理帧 ordinal 对齐，以及是否使用完全相同的 Atlas Cell、帧矩形和 UV。不要通过 Shader 偏移修补错误导出。

### 队伍色溢出到附近像素

确认 Player Color Texture 使用 R8、nearest、没有生成 Mipmap 或颜色空间转换，并检查 R 灰度调试视图。线性过滤会改变相邻像素的原始权重。

### 动画中偶尔露出白色

在 R 独立调试视图中检查对应像素的原始 BC4 解码值。导出器不再自动修复边界，不要对连续权重做阈值化、膨胀或普通图像模糊。

### 队伍色块随帧大幅漂移

先用 `--dump-layers` 检查 SLD Layer 4，而不是直接增加平滑。确认方向数、帧数、物理帧配对和脚点正确；错误的方向数会让本来不相邻的帧被当成同一动画序列。

### Runtime 报不支持 Player Color 格式

重新导出资源，并确认 manifest 中为：

```json
"player_color": {
  "format": "rgba8_bc4_decoded"
}
```

旧的 `r8_palette_index_plus_one`、`r8_subcolor_alpha_binary` 或量化缓存与当前 Runtime 不兼容。

### 修改资源后表现没有变化

确认运行时读取的是同一个缓存根目录；同步到其他引擎后还需替换其本地 cache。随后清除对应引擎的纹理/Archive 缓存或完整重启，避免仍使用旧 manifest 和 GPU Texture。

## 12. 已知限制与后续方向

- Outline 和 Damage Mask 尚未导出；
- BC4 解码已包含压缩插值误差，raw RGBA 导出不能还原压缩前不存在的信息；
- 当前着色方案尚未完整复现 AoE2DE 的亮度和颜色模式参数；
- Projectile 的多方向加俯仰 Pose 仍需由具体运行时解释；
- DAT 元数据尚不能直接替代目标引擎的 Unit/Weapon 配置；
- 近战攻击点、远程发射点和受击点需要目标引擎的专用校准/转换工具；
- 若未来引入时序平滑，必须作为明确的可选后处理，不能覆盖 raw RGBA 基线资源。

修改 Player Color、图层配对或帧裁剪规则时，至少补充对应单元测试，并以弓手和骆驼骑兵的 Idle、Walk、Attack、Death 全方向动画做实际回归。
