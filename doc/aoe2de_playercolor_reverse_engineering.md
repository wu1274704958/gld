# AoE2DE Player Color 加载、重建与绘制链路

调查日期：2026-09-07。本文记录本机官方程序的逆向证据；第 4 节所述解码问题已于同日修复并完成对照验证，shader 差异仍待处理。

## 1. 结论

上次仅修改导出后的对齐方式和 shader 混合权重，遗漏了更早的 SLD 解码错误：

1. 修复前的 Cython 解码器给 mask 继承 Main 的尺寸和脚点，却可能使用 Shadow 的 offset。跨帧复制因此读错位置，尺寸校验无法发现。
2. `flag & 0x80` 的参考对象是该图层最近的非差分关键帧，不是上一张解码完成的帧。连续叠加 delta 会留下错误数据。
3. 官方先合并当前 Main 与相关图层关键帧的边界，得到共同输出坐标域，再重建图层。不应一律把当前 Main header 的矩形当成最终尺寸。
4. Team 数据在官方路径中保持 BC4 压缩块，上传 `BC4_UNORM`，shader 只读取 R；没有发现该路径中的 palette 查表转换。
5. 官方队伍色混合不是当前的 `teamRGB * clamp(luma * 1.6, ...)`。CPU 先将队伍色常量的 alpha 改成 RGB 亮度，shader 再做分段亮度变换。

因此，继续修改最终 PNG 的偏移或只调 shader，不能恢复解码时已经取错的像素。RGBA 四通道导出可以保留，但它是本项目的调试接口，不是游戏原生四通道 Team 数据。

## 2. 对象与方法

| 项目 | 值 |
| --- | --- |
| 程序 | `D:\program1\steam\steamapps\common\AoE2DE\AoE2DE_s.exe` |
| 文件版本 | `101.103.48987.0` |
| 文件长度 | 71,648,568 bytes |
| 原始 EXE SHA256 | `6378ca6f1fbd2f230b5b7f2cd048198331848af70f44b5cd13ceb89420a321a4` |
| PE preferred image base | `0x140000000` |
| 官方 shader | `resources/_common/shaders/d3d11/SpritesSLD_ps.so`、`SpritesSLD_vs.so` |

磁盘上的部分函数不可直接正常反汇编，因此启动游戏后使用 `ReadProcessMemory` 只读取得运行时可执行节。没有注入、修改游戏内存或修改安装文件。用户关闭游戏后已重新启动；重启后的采样进程为 PID 49792，模块基址 `0x7ff6fa020000`。

本文地址统一使用 preferred VA。实际地址为 `模块基址 + (本文地址 - 0x140000000)`；版本升级后不能直接套用。

使用 Capstone 分析运行时代码，使用 Windows SDK `fxc /dumpbin` 读取官方 DXBC，使用 Unicorn 执行实际 x64 几何计算和块重建函数。模拟器仅替换设备能力查询 `0x141765260`，返回 1 以选择原生 BC4 路径；没有替换 SLD 重建逻辑。

## 3. 链路与证据地址

```text
SLD 文件发现
  -> SLDX/header 校验、逐帧索引、每图层关键帧记录
  -> 合并当前 Main 与相关关键帧矩形
  -> 先解码关键帧，再覆盖当前帧 draw blocks
  -> Team BC4 压缩块图集
  -> BC4_UNORM Texture2D + ShaderResourceView
  -> 图集上传接口、Team texture slots 绑定
  -> 独立 Team UV 传入像素 shader
  -> sample.r 作为权重，结合队伍色及亮度参数混色
```

以下函数名称是分析用途的语义命名，不是调试符号：

| 阶段 | Preferred VA | 证据 |
| --- | --- | --- |
| 资源选择 | `0x1409b2ae0` | `.sld` / `.smx` / `.smp` 选择路径 |
| SLD 索引构造 | `0x1409b5d90` | 校验 `SLDX`、版本、帧数；建立每帧 36-byte 索引记录 |
| 关键帧更新 | `0x1409b60be` | 图层 flag 高位未置位时，更新该图层关键帧 ordinal |
| 输出几何 | `0x1409b6410` | 合并当前 Main 和引用的关键帧边界；Team 分支在 `0x1409b6632` |
| 图层地址查询 | `0x1409b67f0` | 从索引记录定位对应图层数据 |
| 解码调度 | `0x1409b4fc0` | 读取引用关键帧，先重建关键帧再覆盖当前帧 |
| 两次解码调用 | `0x1409b524a`、`0x1409b526f` | 分别处理基准关键帧和当前 delta |
| 8-byte 块重建 | `0x1409b4210` | skip 保留基准或填空块；draw 复制压缩块 |
| draw 块复制 | `0x1409b4860` | 每次从源取 8 bytes，直接写入目标图集 |
| Team 解码包装 | `0x14097e080` | 指定 layer 4 后进入解码调度 |
| 图集组配置 | `0x1409cd320` | Team 描述项位于 `0x143d37068`，组索引 9、compression 2 |
| 图集分配 | `0x1409c0d40` | Team 分配使用每像素 0.5 bytes 的 BC4 存储 |
| GPU 图集初始化 | `0x1409be0d0` | compression 2 转为引擎纹理枚举 `0x12` |
| DXGI 映射 | `0x14097b0b0` | 枚举 `0x12` 转为 DXGI 80，模拟执行已验证 |
| 纹理描述 | `0x14176e0a0` | 设置 Texture2D format、尺寸、mips=1、array=1 |
| D3D 创建 | `0x14176d948`、`0x14176de35` | 分别调用 `CreateTexture2D`、`CreateShaderResourceView` |
| 图集上传包装 | `0x1409edb90` | 选择图集纹理页并转入更新函数 |
| 更新纹理 | `0x14176e8b0` | 计算 box/pitch，`0x14176e954` 调用 `UpdateSubresource` |
| 队伍色来源 | `0x140986380`、`0x140986b10` | 加载 `spritecolors.json`、读取 FloatRGBA |
| 绘制准备 | `0x1409cdb10` | 亮度、模式、队伍色常量及 Team 图集绑定 |
| 队伍色 alpha | `0x1409cdd7c` 至 `0x1409cdf44` | 将 RGB 亮度写入 alpha，再上传常量数组 |
| Team 绑定 | `0x1409ce0e6` 至 `0x1409ce10a` | SLD 路径选择 Team 图集组 9 并绑定 |

DXGI 80 是 `DXGI_FORMAT_BC4_UNORM`，见 [Microsoft DXGI_FORMAT](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format)。上传接口语义见 [Microsoft UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource)。

已经定位图集更新虚函数及其 D3D11 上传实现，但没有逐一还原所有异步图集任务的调度调用者，也没有抓取实际对局中的完整 GPU draw。这是函数级链路与解码实证，不应表述成完整实时 GPU 捕获。

## 4. 已修复的两个解码错误

### 4.1 Mask 错用了 Shadow offset

`tools/aoe2de_export/sld/sld.pyx` 在解析 Main 和 Shadow 时，都会写共享的 `offset_x1`、`offset_y1`。Main 的宽、高、脚点另有缓存，但没有对应缓存 Main offset。后续构造 Damage/PlayerColor header 时恢复了 Main 尺寸与脚点，却继续传入最近一次赋值的 offset，通常来自 Shadow。

这会影响基于 `get_block_index` 的跨帧块定位。即使导出层检查到 Main/Team 宽高和脚点一致，内部 mask 数据仍可能已错位。

例如弓手 attack 的物理 frame 3，Main rect 为 `[180,124,264,212]`，Shadow rect 为 `[148,124,264,220]`。二者不能互换用于 Team 定位。

独立重建器刻意采用“上一帧 + Shadow offset”后，五组样本的 R 通道全部精确复现修复前的本地解码器，支持上述定位，而不是单凭目测推断。

### 4.2 Delta 参考关键帧，不是上一帧

本地 `previous_main`、`previous_shadow`、`previous_dmg_mask`、`previous_playercolor` 每解码一帧就更新。这样第二张及后续 delta 会继承上一张 delta 的修改。

官方索引中的每个图层有独立关键帧 ordinal：只有当前图层不是 delta 时才更新。delta 解码时先恢复该关键帧，然后执行当前帧 draw 命令；skip 意味着保留关键帧对应块。

```text
正确：frame 35 = keyframe 23 + frame 35 的 draw blocks
错误：frame 35 = frame 34 + frame 35 的 draw blocks
```

弓手 idle 的 frame 34 至 38 共同引用 keyframe 23。修正 offset 后，继续使用上一帧基准仍会分别产生 2、8、11、40、20 个 R 像素差异。

同一参考模型也用于其他图层，因此本次修复没有仅替换 `previous_playercolor`，而是统一处理 Main、Shadow、Damage 和 Player Color。

### 4.3 输出矩形也依赖关键帧

官方几何函数合并当前 Main rect、Main 关键帧 rect，以及必要的 Damage/Team 关键帧对应 Main rect，形成输出坐标域。mask 自身没有独立矩形，不代表其完整重建结果永远局限在当前 Main header 的矩形内。

先确定此坐标域，再重建 Main/Team，最后统一导出布局。`align_playercolor_frame` 之类的最终图像对齐函数无法修复错误的参考帧或块地址。

本次 2,885 帧中有 409 帧的官方合并矩形大于当前 Main 矩形；不过这些弓手样本在 Main 裁剪区外的实际非零 Team 像素为 0。因此本次不能把像素差异数量归因于“裁掉了非零 Team”，几何结论来自原函数行为。

## 5. 逐帧验证

比较对象：

- 官方运行时代码在 Unicorn 中执行得到的 BC4 块图集。
- 独立实现的“Main 坐标 + 每图层关键帧”参考重建器。
- 修复前及修复后的本地 Cython `.pyd` 解码结果。

对压缩结果使用相同的 CPU BC4 插值规则再比较 R，统一到帧坐标域。差异数量按每帧像素累计，不是不同屏幕位置的去重数量。

| Archer x2 动画 | 帧数 | 官方模拟 vs 参考：差异 R 像素 | 官方模拟 vs 修复前本地：差异 R 像素 | 合并矩形更大的帧数 |
| --- | ---: | ---: | ---: | ---: |
| attackA | 481 | 0 | 11,526 | 103 |
| deathA | 481 | 0 | 6,439 | 59 |
| decayA | 481 | 0 | 5,595 | 80 |
| idleA | 961 | 0 | 19,014 | 108 |
| walkA | 481 | 0 | 3,761 | 59 |
| 合计 | 2,885 | 0 | 46,335 | 409 |

同一测试在修复并重建 `.pyd` 后再次执行，五组动画的“官方模拟 vs 修复后本地”均为 0 个差异 R 像素，包括全部 409 个合并矩形扩大的帧。

两个错误的独立对照如下；两列像素数可能重叠，不能相加：

| 动画 | 本地 vs 上一帧/Main坐标 | 上一帧/Main坐标 vs 关键帧/Main坐标 |
| --- | ---: | ---: |
| attackA | 11,481 | 188 |
| deathA | 6,350 | 219 |
| decayA | 5,571 | 73 |
| idleA | 17,250 | 2,292 |
| walkA | 3,722 | 67 |

这验证了地址计算和参考帧恢复，不等于 GPU BC4 插值与 RGBA8 导出逐 bit 相同。GPU 插值精度与 CPU byte 取整仍可能有小幅数值差异。

## 6. Shader 的实际输入与公式

### 6.1 纹理与 UV

`SpritesSLD_ps.so` 的纹理槽为 Diffuse `t0..t4`、Damage `t5..t8`、Team `t9..t13`，使用 `sPoint` 采样。Team 只读取 `.r`，启用且 R 大于零时进入队伍色分支。

顶点输入 `COLOR3` 携带 Team 的图集索引、U、V、启用状态，顶点 shader 将其传至像素 shader 的 `TEXCOORD3`。官方 Main/Team 可以独立打包，使用不同 UV，但帧的几何坐标域一致。本项目共用图集布局与 UV 是一种实现选择，不是官方要求它们的 UV 数值相等。

BC4 原生只有一个数据通道。本项目 RGBA8 中 G/B=0，A 是解码器适配统一图片接口生成的值，不是另外三种官方 Team 数据。正常着色只能把 R 当作权重，不能用 A 过滤或将 R 作为 palette index。

### 6.2 CPU 常量准备

常量布局：`g_spriteTeamColors` 为 9 个 float4，offset 144；`gTeamLuminance` offset 484；`gTeamColorMode` offset 488。

`spritecolors.json` 中的 alpha 为 1，但绘制准备代码会覆盖：

```glsl
teamColor.a = dot(teamColor.rgb, vec3(0.299, 0.587, 0.114));
```

模拟执行原代码验证，蓝色 alpha 为 0.114、红色为 0.299、灰色为 0.4。不要把 JSON alpha=1 直接代入官方混色公式。

重启后主菜单中的 renderer 状态读到 `gTeamLuminance=1.0`、`gTeamColorMode=1`。这不是对局 draw 的 GPU constant buffer 捕获。菜单中的原始颜色数组 alpha 仍为 1，alpha 覆盖另由原绘制准备代码的模拟执行确认。

内部观察到的颜色顺序为蓝、红、绿、黄、橙、青、紫、灰、白；不能未经验证就把内部数组索引等同于业务玩家编号。

### 6.3 官方 Team 分支的高层转写

以下转写对应 DXBC 的 Team 混色核心，不包括后续伤害、实例 tint 等其他处理。`mix` 表示线性插值，`A` 是 CPU 上传的队伍色亮度，不是 Team 纹理 alpha。

```glsl
vec3 applyTeam(vec3 diffuse, float w, vec3 teamRGB,
               float A, float teamLuminance, int teamColorMode)
{
    // Call only when Team is enabled; zero weight bypasses this branch.
    if (w <= 0.0)
        return diffuse;

    vec3 C = diffuse * teamLuminance;
    float Y = dot(C, vec3(0.299, 0.587, 0.114));
    float threshold = A;
    float pivot = 0.5;
    if (teamColorMode > 0) {
        threshold = mix(A, 0.5, w);
        pivot = mix(0.5, A, w);
    }

    vec3 adjustedBase;
    vec3 target;
    if (Y < threshold) {
        adjustedBase = C * (2.0 * pivot);
        target = teamRGB * (Y / threshold);
    } else {
        adjustedBase = C * (2.0 *
            (pivot + (2.0 * Y - 1.0) * (0.5 - pivot)));
        target = vec3(1.0) + (teamRGB - vec3(1.0)) *
            ((1.0 - Y) / (1.0 - threshold));
    }
    return mix(adjustedBase, target, w);
}
```

最终 mix 的第一项是 `adjustedBase`，不总是原始 Diffuse。官方核心没有当前预览器的 `luma * 1.6`、`0.22..1.15` 限幅。

例如 mode=1、w=1 时，阈值为 0.5；低亮度输出 `2*Y*teamRGB`，高亮度输出 `1 + 2*(1-Y)*(teamRGB-1)`，会向白色保留高光，而不是始终按队伍基础色缩放。

另外，官方所分析的 pass 1 在 Main alpha 小于 0.5 时 discard，并使用二值覆盖乘实例 alpha。本项目当前 0.01 阈值及保留原 alpha 的行为不同，但这不是已验证的 mask 解码差异来源。

## 7. 修复顺序与验证范围

1. 修改 `.pyx`：明确保存 Main offset，所有 mask 使用 Main 坐标；按图层维护最近关键帧，而不是逐帧更新参考。
2. 在重建之前计算官方的合并输出矩形，让 Main/Team 在同一坐标域恢复。覆盖 Main、Shadow、Damage、Team 各自参考关系。
3. 添加连续 delta、Main/Shadow offset 不同、关键帧与当前帧矩形不同的回归测试；重建 `.pyd`，重新导出旧资源。
4. 导出文件继续保留 RGBA8 供离线检查；人工确认通道后，运行时恢复为从源图提取 R 并上传 R8。后续再替换 shader 的亮度与分段混色公式，并核对业务玩家颜色映射。
5. 扩大到其他单位、缺失图层、其他压缩类型；必要时抓取实际对局 GPU draw，核对上传像素、UV、常量和最终混色。

本次实证覆盖弓手五种 x2 动画的 BC4 Team 路径。没有验证所有单位、BC7、outline、所有缺层组合，也没有完整验证其他渲染 pass。不能把本次结果扩写成所有 SLD 变体均已支持。

调查时的 [openage SLD 解码代码](https://raw.githubusercontent.com/SFTtech/openage/master/openage/convert/value_object/read/media/sld.pyx) 仍采用逐帧更新 previous 图层的模型，不能将其当作官方行为的最终依据。[openage SLD 文档](https://github.com/SFTtech/openage/blob/master/doc/media/sld-files.md) 中 Player Color 的 palette 描述也不适用于本次确认的官方 SLD/BC4 渲染路径。

## 8. 本地复现材料

分析材料保留在被 `.gitignore` 排除的 `build/aoe2_reverse/`，依赖在 `build/aoe2_reverse_deps/`。包含本机路径和运行时快照，不是干净 checkout 可直接运行的正式测试套件。游戏二进制快照不得提交或再分发。

```powershell
Set-Location E:\code\gld
python -B build\aoe2_reverse\compare_masks.py
python -B build\aoe2_reverse\emulate_decode.py
```

| 文件 | 内容 |
| --- | --- |
| `reverse.py` | PE 索引、引用和反汇编辅助 |
| `snapshot.py` | 只读运行时代码与指定 renderer 字段 |
| `compare_masks.py` | 分离坐标错误与参考帧错误 |
| `emulate_decode.py` | 执行官方几何/解码函数，验证常量准备及 DXGI 映射 |
| `comparison.json` | 独立算法及本地解码器对照 |
| `emulation.json` | 官方函数模拟执行的逐动画汇总 |
| `renderer_state.json` | 主菜单 renderer 常量只读快照 |
| `verified_constants.json` | 原生代码算出的队伍色 alpha、DXGI 格式 |
| `SpritesSLD_ps.asm`、`SpritesSLD_vs.asm` | 官方 shader DXBC 反汇编 |

生产解码器现已按第 4 节修复：mask 使用 Main 源坐标，各层引用最近关键帧，并在解码前计算合并输出矩形。重建扩展后的 2,885 帧 Player Color 与官方原代码模拟输出差异为 0。官方 shader 公式仍未合入，旧资源缓存也需要重新导出。
