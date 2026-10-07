# AoE2DE 视觉地图导出器

## 范围

入口：`tools/aoe2de_export/aoe2de_export.py --visual-map <场景.aoe2scenario>`。

当前只导出**引擎无关的视觉地图源数据包**。Recoil 适配层尚未实现；旧的近似 `.sdd` 直出已移除，避免误以为其中保留了源地形语义。
这不是场景逻辑移植；无需修改 `aoe2_replica` 的引擎、Live GP 或寻路。

包含：

- 地图尺寸；逐格地表 ID、layer 和原始 elevation。
- 仅被地图引用的地表 DDS、别名地形、混合优先级、混合类型、遮罩、原始铺设尺寸。
- 地表过渡资源、颜色气氛配置；使用水面地形时复制水面资源与配置。
- 装饰物的源坐标、原始朝向/变体值、初始帧、外观颜色；没有玩家所属队伍或战斗属性。
- 去重后的 SLD 主图、阴影和玩家色图集，以及 Graphic delta 叠加关系。
- 建筑 annex 的视觉外观和相对位置，不把 annex 生成为游戏单位。
- 金矿闪光等纹理图集型粒子的原始视觉配置、DDS、TexturePacker 帧布局。Loop、启动延迟等不强行转换为一次性动画。
- 低成本地表分类预览图和资源完整性报告。

不包含：触发器、AI、XS、胜负条件、玩家资源、科技、出兵规则、UnitDef/WeaponDef、碰撞、寻路、阻挡、采集行为。
DAT 只在导出阶段读取，不把整份 DAT 或场景文件复制进输出。

## 安装与使用

Python 3.11+。基础依赖与 SLD 扩展构建方法见 [导出器 README](../tools/aoe2de_export/README.md)。
新增解析库是可选依赖，旧的 Unit/Building/Graphics 导出不会导入它。

在 gld 仓库根目录执行：

```powershell
python -m pip install -r tools/aoe2de_export/requirements-visual-map.txt

python tools/aoe2de_export/aoe2de_export.py `
  --aoe2 "F:/SteamLibrary/steamapps/common/AoE2DE" `
  --visual-map "D:/maps/my-map.aoe2scenario" `
  --out "E:/code/gld/res/aoe2de_cache" `
  --name my_map
```

将示例场景路径替换为实际文件。地图须为已保存的 `.aoe2scenario`；`.rms` 是生成脚本，不能直接作为地图布局。

多个地图共用同一 DAT 时，优先使用共享地形库与轻量地图模式：

```powershell
python tools/aoe2de_export/aoe2de_export.py --terrain-library `
  --aoe2 "F:/SteamLibrary/steamapps/common/AoE2DE" `
  --out "E:/code/aoe2_replica/cont/aoe2de_cache" --name de-terrain-20261004

python tools/aoe2de_export/aoe2de_export.py --visual-map "D:/maps/my-map.aoe2scenario" `
  --map-terrain-library "E:/code/aoe2_replica/cont/aoe2de_cache/terrain-libraries/de-terrain-20261004" `
  --map-objects none --aoe2 "F:/SteamLibrary/steamapps/common/AoE2DE" `
  --out "E:/code/aoe2_replica/cont/aoe2de_cache" --name my_map
```

`terrain-libraries/<name>` 保存全部 DAT 地形 ID 表与一次复制的原始 `terrain/` 资源；轻量 `maps/<name>` 只保存 `terrain.bin`、`instance-metadata.json`、`manifest.json`。轻量模式采用 schema 3，检查 DAT/索引/实际使用素材的哈希，不解析随机文明。它仍是源数据包，不是可运行的 Recoil 地图。
对于随机地图，先在 DE 编辑器中生成并另存场景。安装目录的旧 E3 演示场景为 1.35，当前解析器不支持；应先在当前 DE 编辑器中打开并另存副本。

参数：

| 参数 | 行为 |
| --- | --- |
| `--map-objects scenery` | 默认；选择 DAT 类型 10/15/20/80/90 的场景对象，包含静态装饰、旗帜/占位物和建筑；没有可见图形的阻挡器被跳过 |
| `--map-objects all` | 同时导出军队、动物等的初始静态外观，不导出其逻辑 |
| `--map-objects none` | 不导出对象外观；完整摆放对象 metadata 仍保留，不需要 SLD 解码器 |
| `--map-player-civ 1:1` | 为场景玩家 1 指定 DAT 文明 1 的视觉外观；可重复传入。随机/自定义文明无法唯一解析时必须指定，不随机猜测 |
| `--map-allow-incomplete` | 允许资源不完整，记录 `summary.complete=false` 和具体缺失项；默认严格失败 |
| `--map-preview-size 1024` | 预览最长边，范围 1–4096；不改变源网格和 DDS 精度 |
| `--scale auto/x1/x2` | 装饰 SLD 的源分辨率；地表读取安装目录的 `textures/2x` |
| `--dat <路径>` | 使用与场景匹配的 DAT；默认与现有单位导出一致 |

未指定文明时使用场景的视觉文明/建筑风格设置。随机建筑风格回到玩家文明；仍为随机时明确报错。
人口、资源和外交关系均不输出。场景触发器中的更换外观/时代等操作不执行。

同名目录**不会覆盖**。修改地图后使用新的 `--name`，或由使用者自行管理旧资源。
导出在同级临时目录完成，通过后才发布；失败不会留下可被误加载的半成品，也不会删除既有资源。

## 资源包结构

```text
<out>/maps/<name>/
  manifest.json           版本、尺寸、坐标约定、完整性与限制
  terrain.bin             紧凑地表网格
  materials.json          仅视觉地形字段
  objects.json            实例位置/姿态/颜色 -> appearance
  instance-metadata.json  完整源实例 ID、所有权、位置、状态及 DAT 分类
  appearances.json        共用外观 -> graphic、建筑 annex
  graphics.json           共用 Graphic -> 图集、delta、粒子配方
  graphics/
    g<ID>.json            复用现有 schema-2 图集描述
    g<ID>.dds             BC1 主图
    g<ID>_shadow.dds      可选 BC4 阴影
    g<ID>_playercolor.dds 可选 BC4 玩家色
  assets.json             复制资源相对路径、大小、SHA-256
  source/
    terrain/...           原始 DDS、遮罩、blend、水面等视觉资源
    particles/...         可选粒子配置及图集
  terrain-preview.png     地表分类诊断图，不是最终渲染效果
```

`manifest.kind = aoe2de_visual_map`，`schema_version = 2`。
`summary.complete` 表示所选择的视觉数据/资源是否成功导出，**不表示已经实现 DE 的完整渲染器**。
`assets.json` 为原始复制资源清单；重新生成的 sprite 图集通过 `graphics.json` 和对应图集描述引用。

### 网格与坐标

`terrain.bin` 无额外头部，尺寸从 manifest 读取；小端，逐行排列 `i = x + y * width`，每格 8 字节：

| 字节偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | uint16 | 原始 terrain_id |
| 2 | int16 | 原始 layer 地形 ID；-1 无附加层 |
| 4 | uint8 | 原始 elevation 等级 |
| 5 | uint8[3] | 保留，写 0 |

对象位置保留 AoE 场景 `[x,y,z]`：x/y 是水平轴，z 是垂直轴。这里不写死 Recoil 的尺寸、轴向、坡高或地面高度转换。
消费者必须集中定义变换，同时应用于地表、装饰物和 annex；不能将 elevation 直接当作 Recoil 世界高度。

`rotation_raw` 并不保证是弧度。尤其 `sequence_type=6` 的树木等，其方向槽经常表示不同外观。
因此保留 `initial_frame_raw`、Graphic `sequence_type`、`pose_mode` 和全部帧，避免错误地转为 heading。
`mirroring_mode`、`layer_raw`、delta 的 `display_angle` 和像素偏移也保留源语义，不能直接当作引擎深度或世界偏移。

### 地形与粒子

地形别名使用 `draw_as` 指向另一材料；遇到循环引用立即失败。
`water_flags_raw` 是 DAT 原始位字段，**不能按 bool 使用**：普通草地的值就可能是 32。
保留的是渲染来源数据，不是通行权限。

地表过渡配方保留 `blend_type`、`blend_priority`、遮罩及 blends 文件，尚未做边缘混合烘焙。
`terrain-preview.png` 只使用纹理平均色标识地块，不含树木、混合、高低差、水面动画；缺资源显示洋红。

粒子节点使用 `aoe2de_atlas_recipe`，不伪装为 SLD。文件路径相对于资源包；原始配方内的 AtlasFile 路径保持与原始 particles 目录一致。
目前只接受已列举的纹理图集型视觉字段；组合粒子、未知字段、缺失图集或帧 JSON 会报错/标记不完整。

## 性能与可靠性

- 地表一次线性扫描，8 字节/格；1024² 格的网格为 8 MiB。
- 按被引用地形选择 DDS；同一路径只复制和计算 SHA-256 一次。
- 按 Graphic ID 导出一次，同类树木共享图集；建筑外观与 annex 也去重。
- 复用现有 BC1/BC4 block-copy SLD 输出，不为每个实例生成图片。
- 不在运行时解析场景、DAT 或执行地图脚本。
- 预览尺寸有上限，不生成整张高分辨率 RGBA 地图。
- 检查尺寸、数据范围、非有限坐标、路径越界、别名/组合循环及 DAT/SLD 帧数一致性。
- 严格模式不吞掉缺图；宽松模式聚合重复错误，避免 1 万棵树产生 1 万条相同日志。

## 测试

不需要游戏资源的回归测试：

```powershell
python -m unittest discover -s tools/aoe2de_export/tests -p "test_*.py"
```

需要本机 DE、可选解析库及已编译 SLD 扩展的集成测试：

```powershell
$env:AOE2_VISUAL_MAP_TEST_ROOT = "F:/SteamLibrary/steamapps/common/AoE2DE"
python -X utf8 -m unittest discover -s tools/aoe2de_export/tests -p "test_visual_map_integration.py"
Remove-Item Env:AOE2_VISUAL_MAP_TEST_ROOT
```

集成测试在临时目录生成场景并重新读取，严格模式验证真实草地/道路/泥地、树木共享图集、石矿、金矿粒子、房屋，以及地形 layer/elevation 保留；不会修改原游戏场景。
另有市镇中心 annex 的宽松模式测试：本机 DAT 引用了不存在的 `s_town_center_extra_x1.sld`，必须保留可导出附件并明确报告不完整，不能假装完整导出。

## Recoil 地图适配状态

当前源数据包不是可加载的 Recoil 地图。此前的直接烘焙路径只近似使用主 terrain ID，未保留 layer/blend/mask，且生成最终 .sdd 时删除了源 tile 资源包；因此已撤下。

后续将以轻量源包的 `terrain.bin` 与共享库 `terrain-ids.json` 为输入，给 Recoil 增加原生地图加载与渲染路径，不逐地图生成 SMF/SMT。高程、融合与 typemap 需分别验证；所有对象仍只保留 metadata，不烘焙进地表，也不在首版运行时实例化。实施阶段与验收标准见 aoe2_replica 的 `doc/todo/aoe2de-map-tile-pipeline-plan.md`。

### 已知源资源限制

- 墙的 sequence 2 和树的 sequence 6 需按各自的原始变体索引解释。
- 保留并使用初始帧；源 SLD 尾部记录仅在完整方向组与 DAT 帧数一致时可略过。
- 缺失组合主图不应丢弃可用 delta；缺失 annex 不应丢弃后续附件；宽松模式需报告缺图。
- 本机源场景引用的个别图形可能不存在，不能伪装为完整导出。

解析库资料：[AoE2ScenarioParser](https://github.com/KSneijders/AoE2ScenarioParser)、[TerrainTile API](https://ksneijders.github.io/AoE2ScenarioParser/api/AoE2ScenarioParser/objects/data_objects/terrain_tile.html)。
