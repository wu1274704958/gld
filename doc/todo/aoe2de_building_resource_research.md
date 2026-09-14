# AOE2DE 建筑资源、导出器与 Recoil 契合调研

调研日期：2026-09-08。范围为本机 AOE2DE 安装、gld 导出器、RecoilEngine `aoe_dev` 源码；没有改动游戏安装目录、导出器或引擎代码。

## 1. 结论摘要

建筑的主体资源仍是 SLDX/SLD，因而可以复用 gld 当前的五层解码、物理帧对齐、图集和 Player Color 管线。它们并不是“Unit SLD 的另一种格式”。真正的差异是：资源命名不是 `unitPrefix + idle/walk/attack/death`，建筑普遍是静态主体、`destruction`、`rubble`，城门另有方向及 `open/closed/constr` 状态；当前 Unit manifest、运行时动画槽和自动发现逻辑不能表达这些状态。

Recoil 的原生 `CBuilding` 正适合作为 Gameplay 宿主：它继承 `CUnit`，参与生命、武器、建造、选择、死亡和 Feature 体系，并额外处理不可移动、Footprint、YardMap 与整地。现有 AOE Bridge 已能从任意 `CUnit` 建立实例，因此普通静态建筑接入的主路径可复用；但渲染器需要支持建筑 manifest 和可命名状态，Bridge 需要按照建造进度、城门状态、攻击状态选择动画。

建议不把建筑伪装成当前 `aoe2de_unit`。新增 schema 4 的 `aoe2de_building` manifest，同时共享既有 graphics animation schema；将 Building 特有逻辑限制在导出器、渲染 manifest 解析和 unsynced Bridge，Recoil 原生建造/阻挡/武器逻辑保持不变。

## 2. AOE2DE 本机资源证据

### 2.1 路径、格式与命名

- 安装根：`D:\program1\steam\steamapps\common\AoE2DE`。
- 主图形根：`resources/_common/drs/graphics`。
- 样本 `b_afri_archery_range_age2_x2.sld` 的文件头为 `SLDX`；同目录普通单位和建筑共用该格式。
- 建筑命名以 `b_` 起始，文明、建筑、时代编码在文件名中。例如：
  - `b_afri_tower_age2_x2.sld`；
  - `b_afri_tower_age2_destruction_x2.sld`；
  - `b_afri_tower_age2_rubble_x2.sld`；
  - `b_afri_gate_stone_ne_open_x2.sld`、`..._closed_...`、`..._constr_...`、`..._destruction_...`、`..._rubble_...`。
- `b_afri_tower_age2_x2.sld` 可由当前本地 SLD 解码器读出 Layer 0/1/3/4 各一帧（Main、Shadow、Outline、Player Color）；Main hotspot 为 `(128, 440)`。这说明建筑继续具有脚点、阴影与队色图层，且静态塔不是 16 向 Unit 动画。

### 2.2 已确认的状态资源

| 状态 | 证据 | 建议语义 |
|---|---|---|
| 主体 | `b_*_ageN_x2.sld` | Built/Idle，通常静态 |
| 坍塌 | `*_destruction_x2.sld` | Death，一次性播放 |
| 瓦砾 | `*_rubble_x2.sld` | Feature/Rubble，静态或慢速衰减 |
| 城门建造 | `*_constr_x2.sld` | Construction；仅已确认城门、城墙等样本 |
| 城门开闭 | `*_open/_closed_x2.sld` | Building state，不应以旋转代替 |

不能从已检查命名推断每一种普通建筑都有独立 `constr` SLD；这是待通过 DAT Graphic ID 与实机状态逐项核实的项。`destruction` 的文件通常远大于主体，批量预载会明显增加缓存、解码和显存成本。

### 2.3 发射点

AOE2DE DAT 的 `type_50.graphic_displacement` 是当前导出器提取的原始三维武器偏移，已经写入 Unit manifest 的 `dat.combat.weapon_offset`。解析位置：`tools/aoe2de_export/aoe2de_export.py:270-350`。

它可用于建筑的初始候选发射点，但不能单独保证视觉正确：

- DAT 偏移是 AOE2 的 Gameplay 数值坐标，不是 SLD 像素坐标；需要集中进行轴、比例、朝向转换。
- 静态塔只有一个 Sprite 视图时，DAT 偏移可作为固定 Recoil 本地锚点；城门/旋转建筑、含多个武器或多个开火孔的建筑则可能需要 `weapon slot × state × Sprite direction` 覆盖。
- SLD 帧本身没有被当前导出器识别为“炮口点”的元数据。不能从 hotspot 推出炮口；hotspot 仅是建筑整体脚点。
- Recoil 已有每武器 `aoe2_weaponN_muzzle_local` 与开发期 runtime override：`rts/Sim/Units/UnitDef.cpp:103-109`、`rts/Sim/Weapons/Weapon.h:110-114`、`Weapon.cpp:308-316`。建筑可沿用同一校准工具，但应先把 DAT 原始 offset 和变换写入 manifest，避免手填全部单位。

### 2.4 建筑与地面的过渡

确认存在两类资源，但尚无证据表明存在“每栋建筑专属的地表混合图”：

1. 建筑 SLD 的 Main/Shadow。Shadow 本身可在地形上投射，且有独立裁剪/脚点；当前导出器已支持。
2. `resources/_common/terrain/{textures,masks,blends}` 下的全局地形、mask、blend 资源。检查到它们是通用 terrain 路径，未发现与 `b_*` 一一对应的 foundation 文件名。

因此第一阶段不应臆造独立 foundation 资产：使用 SLD shadow + 整体 foot 对齐；如视觉验证发现底座需要压地，可增加一个**可选**的 exported `ground_overlay` layer。它必须来自已确认的 SLD Layer/Graphic ID 或显式 Recoil UnitDef 配置，不可将通用 terrain mask 当作建筑专属资源。

Recoil 本身有与此相近但不等价的 GroundDecal：`SolidObjectDef::Parse` 接受 `useGroundDecal`、`groundDecalType`、尺寸及衰减（`rts/Sim/Objects/SolidObjectDef.cpp:29-36`），`GroundDecals` 明确覆盖建筑底部（`rts/Rendering/Env/IGroundDecalDrawer.cpp:15`）。它适合可重复的污渍/地基 decal，不适合直接承载带 Player Color、逐帧脚点的 AOE Sprite 层。

## 3. gld 导出器现状与可复用性

当前实现已具备建筑导出的核心图像能力：

- SLD Layer 0 Main、1 Shadow、3 Outline、4 Player Color 解码；
- Main/Player Color 按物理 frame ordinal 对齐，Player Color 强制复用 Main 的 atlas/UV/foot；Shadow 可有独立 atlas；
- 由 hotspot 输出 `foot`；
- x1/x2 优先级、帧数和方向数验证、图集打包、缺失辅助层透明占位、JSON manifest；
- DAT 唯一匹配、`collision_size`、`outline_size`、Projectile、时序、射程、攻击 Graphic 和 `graphic_displacement` 序列化。

证据：`tools/aoe2de_export/aoe2de_export.py:240-350, 1240-1345, 1421-1525`，以及 `doc/aoe2de_resource_exporter.md:117-163,212-244`。

现有约束不适配建筑：

- `discover_unit_actions`/`export_unit` 假定 Unit 前缀与 action；
- manifest 固定为 schema 3、`kind: aoe2de_unit`；
- Runtime 的枚举只提供 `IdleA/WalkA/AttackA/DeathA`；
- DAT 映射 `unit_dat_map.json` 目前面向 Unit；建筑多文明/时代 Graphic 共用会使“按 graphic prefix 唯一匹配”更易歧义。

## 4. Recoil Building 契合分析

### 4.1 可直接复用

- `CBuilding : CUnit`（`rts/Sim/Units/UnitTypes/Building.h:11-28`）复用原生生命、受击、Weapon、CommandAI、建造进度、选择和死亡逻辑。
- `CBuilding::PreInit` 绑定 YardMap、`levelGround`；`ForcedMove` 通过 `BuildInfo` 对齐建造格并 `FlattenGround`（`Building.cpp:22-58`）。
- UnitDef 将 `footprintX/Z` 转为阻挡分辨率，immobile Unit 创建 YardMap（`UnitDef.cpp:769-774,917-1000`）；这比由 Sprite 像素尺寸推导 Gameplay 占地更可靠。
- 现有 AOE Gameplay Bridge 已监听 `RenderUnitCreated/Destroyed`、`RenderFeatureCreated/Destroyed`，并能替换原生 Unit/Feature 模型（`Aoe2UnitGameplayRenderBridge.cpp:180-215,394-553,781-811`）。建筑作为 CUnit 自动进入 Unit 生命周期。
- Render 实例按 appearance/animation/纹理批次合并；静态建筑能获益于同一批处理。入口和限制见 `Aoe2UnitRenderer.h:10-90`、`.cpp:872-1237`。

### 4.2 必须扩展

- 当前 `PreloadAppearance` 仅接收 `aoe2de_unit` schema 2/3，且固定四个 Unit 槽（`Aoe2UnitRenderer.cpp:653-711`）；不能加载 Building manifest。
- Bridge 按 Unit 水平速度、第一把武器和 DeathA 驱动状态（`.cpp:661-721`），建筑需改为：建造比例 -> Construction/Built，Weapon fire -> Attack，gate state -> Open/Close，死亡 -> Destruction，Feature -> Rubble。
- Recoil 原生发射位置是三维 `weaponMuzzlePos`，参与射线、地形高度与弹道检查（`rts/Sim/Weapons/Weapon.cpp:308-326,1011-1055`）。AOE Sprite 发射点只能驱动同一个 Recoil 本地锚点；不能仅移动渲染箭矢而不更新 Weapon。
- CFeature 当前的 AOE Bridge 是 DeathA 最后一帧渐隐策略。建筑 Feature 需要可选 `rubble` appearance，不能强迫复用 DeathA。

## 5. 风险与待验证项

- 建筑的 DAT `graphic_displacement`、attack graphic、项目编号、Footprint、Collision、建造 graphic 的字段和文明覆盖关系，需选一座塔（有 Weapon）与一座非攻击建筑以 Python DAT inspection 实测。
- 城门的方向来自资源名/状态，而 Recoil YardMap 不旋转（`Building.cpp:52`）；接入时须制定 four-facing 映射，不能假定 16 方向。
- 透视相机下大建筑与碰撞/foot 的投影误差会比 Unit 明显；应沿用已验证的正交或长焦固定测试视角，并保留校准工具。
- 大型建筑 destruction 资源显著大于主体；必须按状态 lazy preload、引用计数和卸载，禁止在场景开始预载全状态。
