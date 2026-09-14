# AOE2DE Building 导出器扩展与 Recoil 接入实施方案

本方案以 [建筑资源调研](aoe2de_building_resource_research.md) 第 2-4 节为事实依据。实施前应先完成该报告第 5 节的 DAT 样本验证；本文件不授权修改 AOE2DE 安装目录。

## 1. 目标与非目标

目标：将一个 Recoil 原生 `CBuilding` 的主体、建造、攻击、坍塌和瓦砾，以 AOE2DE SLD Sprite 高性能渲染；保持 Recoil 的放置、Footprint、YardMap、整地、武器、Projectile、伤害和 Feature 生命周期为唯一 Gameplay 权威。

非目标：不导入 AOE2 的完整建造/科技/驻扎规则；不由 Renderer 修改同步状态；不把 Sprite 像素轮廓当作 Recoil 的路径阻挡或碰撞体；第一阶段不实现未证实存在的建筑专属 terrain blend。

## 2. 数据契约

### 2.1 新建 Building manifest（schema 4）

新增 `kind: "aoe2de_building"`，路径建议为 `cache/buildings/<id>/manifest.json`。复用既有 animation config 和 layer 格式；不得复制图集格式或 Player Color 规则。

```json
{
  "schema_version": 4,
  "kind": "aoe2de_building",
  "id": "b_afri_tower_age2",
  "building": {"prefix": "b_afri_tower_age2", "variant": "afri", "age": 2},
  "dat": {"civ_id": 0, "unit_id": 0, "collision_size": {}, "outline_size": {}, "combat": {}},
  "states": {
    "built": {"status": "exported", "config": "graphics/built.json", "loop": true},
    "construction": {"status": "missing_source"},
    "attack": {"status": "missing_source"},
    "destruction": {"status": "exported", "loop": false},
    "rubble": {"status": "exported", "loop": true}
  },
  "orientation": {"mode": "fixed", "direction_count": 1},
  "anchors": {"foot_source": "sld_hotspot", "muzzle_candidates": []},
  "ground_overlay": {"status": "missing"}
}
```

`dat` 仍保留 AOE 原始单位与数值，不直接成为 Recoil UnitDef。`muzzle_candidates` 记录原始 DAT displacement 与导出转换后的候选值/来源；最终可用锚点由 Recoil UnitDef 的每 Weapon custom param 覆盖。

### 2.2 Recoil UnitDef / WeaponDef

Recoil Def 是 Gameplay 真相，导出 manifest 是渲染与校准数据：

```lua
customParams = {
  aoe2_building_id = "b_afri_tower_age2",
  aoe2_building_scale = "1.0",
  aoe2_ground_offset = "0",
  aoe2_hide_native_model = "1",
  aoe2_weapon1_muzzle_local = "x y z"
}
footprintX = 2,
footprintZ = 2,
yardMap = "oooo",
levelGround = true,
```

- `footprint*`、`yardMap`、`levelGround`、Collision、WeaponDef 和尸体 Feature 保持 Recoil 配置；禁止从 Sprite 尺寸自动生成。
- `aoe2_building_id` 与 `aoe2_unit_id` 不可同时存在；桥接器应报错并保留原生模型。
- 多武器：每个 Weapon 独立有 muzzle；Sprite 的 Attack 仅可由一个明确的 `aoe2_attack_visual_weapon` 驱动。未声明时只支持 weapon 1，并给多武器 Building 打 warning。

## 3. 导出器实施步骤

1. **资源发现与状态表**：新增 `--building <prefix>`，不复用 `--unit` 的 action 推断。按显式规则探测 `built/destruction/rubble` 和可配置的 `construction/attack/open/closed`；输出 `missing_source` 而非失败。加入 `building_dat_map.json`，键为 building resource ID，值显式包含 civ/unit ID 与状态 Graphic override。
2. **DAT 元数据**：复用 `serialize_dat_metadata`，补充并验证 Building 需要的原始字段（Graphic IDs、建造 Graphic、Footprint/terrain restriction 若 genieutils 可读）。任何未知字段只写原始命名与值，不作语义转换。
3. **manifest**：增加 schema 4 writer、state -> animation config 映射、`orientation.mode`（`fixed`、`directional`、`named_facing`）。沿用 Layer/Main/Shadow/PlayerColor 的全部校验。
4. **锚点输出**：将 `graphic_displacement` 原样记录为 `dat.combat.weapon_offset`，另输出 `anchors.muzzle_candidates[]`，包含 `source: dat_graphic_displacement` 与未校准状态；不在 exporter 内硬编码 Recoil 比例或朝向。
5. **测试**：至少为塔、非攻击建筑、城门各加 fixture：状态发现、缺失状态、Layer 对齐、固定/多 facing 帧数、Player Color、rubble；对 DAT mapping 的歧义必须失败而不是选择第一个。

复杂度仍为 O(物理帧像素数) 解码和 O(帧数) 打包；building destruction 使用 lazy 状态导出/加载，避免将所有状态常驻显存。

## 4. Recoil Renderer 与 Bridge 实施步骤

1. **泛化 appearance**：将 `Aoe2UnitAnimationSlot` 改为小的可命名状态 ID 或新增 BuildingState 枚举；appearance 统一接受 Unit/Building kind，但用独立 loader 验证各自 schema。保留原 Unit slot API 的兼容包装。
2. **batch key 不变**：仍按 animation 的 Main/Shadow/PlayerColor 纹理与 GPU 批次分组；Building 不得逐实例 Draw。仅状态切换/transform/visibility 标脏。
3. **Building mapping**：扩展现有 unsynced `CAoe2UnitGameplayRenderBridge` 的 UnitDef mapping，读取 `aoe2_building_id`。`RenderUnitCreated` 可直接创建实例；仅 mapping 层区分 `unitDef->IsBuildingUnit()`，不要引入同步 AOE Building Entity。
4. **状态驱动**：
   - `beingBuilt/buildProgress`：Construction（无资源则 Built + construction tint/alpha 的明确降级）；
   - static ready：Built；
   - visual weapon firing event：Attack（无攻击帧则保留 Built）；
   - native Unit death：Destruction；
   - native CFeature created：Rubble；无 Rubble 时保持 Destruction 末帧并按现有 Feature fade；
   - gates：仅在原生开闭状态有可靠事件/状态读取后实现 `open/closed`，否则本阶段排除。
5. **muzzle**：先在专用校准场景中把 manifest DAT 候选经过统一坐标转换显示为 debug marker；人工校准后写回 Recoil `aoe2_weaponN_muzzle_local`。由现有 `CWeapon::Update` 计算 `weaponMuzzlePos`，所以 Projectile、命中和地形射线一致。禁止仅在 Projectile Renderer 里偏移发射点。
6. **ground**：先使用 `CBuilding` 的 `levelGround` + Sprite foot + SLD Shadow。若证实存在独立 SLD/graphic 的底层过渡，作为第二实例（ground batch）渲染；若仅需通用 decal，优先映射 Recoil `groundDecal*`，不扩展 Sprite renderer。

## 5. 最小验证闭环

选择一座攻击塔而非城门：

1. 导出 `built/destruction/rubble`，核对 Main/Shadow/Player Color 与 foot。
2. 生成一个 Recoil immobile UnitDef，配置 Footprint、YardMap、Collision、Weapon 和 native Feature。
3. 显示 Recoil `debugcolvol`、AOE foot/muzzle marker、原生 muzzle marker；在正交/长焦测试相机下校准。
4. 触发原生攻击，确认 projectile 起点、地形检查、攻击 Sprite 和 Draw Call。
5. 触发建造、死亡与 Feature，确认无原生模型重叠、无 instance 泄漏。
6. 以 100/1,000 个静态建筑测量实例数、visible 数、batch/draw 数、上传字节、CPU/GPU 帧耗时；主体图集共享时 Draw Call 应随唯一 state/appearance 增长，而非实例数量线性增长。

城门、多炮口城堡、驻扎攻击、建筑专属 foundation/terrain blend 是后续阶段；它们不应阻塞塔的最小闭环。
