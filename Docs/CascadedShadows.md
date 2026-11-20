# 手动分区级联阴影

在方向光 Inspector 中展开 `shadowCascadeEnds`，按从近到远填写各级联的结束距离。每项使用相机视线方向上的世界单位，最多六项。第一级从相机近裁剪面开始，之后每级从前一级末尾开始。最后一个结束距离决定最远覆盖范围，相机远裁剪面可进一步缩短范围。

脚本写入同一字段，例如：

```csharp
sun.shadowCascadeEnds = [50f, 250f, 1000f, 3000f];
```

| 字段 | 默认值 | 约束与语义 |
| --- | --- | --- |
| `shadowCascadeEnds` | 2.11、44.72、945.74、20000 | 填写递增的正数，最多六项 |
| `shadowBias` | 0.0005 | 世界单位的深度偏移 |
| `shadowStrength` | 0.45 | 直接光阴影强度，范围 0..1 |
| `shadowMapResolution` | 2048 | 每级分辨率，范围 256..4096，向下取二次幂 |
| `shadowNormalBias` | 0.25 | 几何法线偏移的世界纹素倍数 |
| `shadowBlendRatio` | 0.1 | 每级末尾过渡比例，范围 0..0.3 |

`ShadowCascadeBuilder::BuildSplits` 在 CPU 上按输入顺序读取分界，跳过非有限和非递增的值，以及位于相机近裁剪面之前的区间。第一个超过远裁剪面的分界裁剪到远裁剪面，之后的级联省略。当前相机没有有效区间时，阴影绘制阶段直接结束。

旧场景首次加载时将旧的级联数、最远距离和分配系数转换为手动列表，转换以 0.1 世界单位的近裁剪面为参考。保存后使用新字段。仓库内的场景按各自相机的原固定分界显式迁移。

## 稳定投影与投射物

`ShadowCascadeBuilder::BuildCascade`：

1. 用相机投影逆矩阵还原 NDC 四角射线，在本级起止视深度截取八角。后一级从前一级的过渡起点开始覆盖。
2. 相机空间八角的包围球中心变换到世界空间，半径按 1/16 世界单位向上量化。该球半径不随相机朝向改变。
3. 从太阳方向建立固定正交基；退化方向使用引擎默认太阳方向。选择不平行的 up 轴。
4. 球投影到光源 XY，增加 PCF 边缘保护，中心对齐本级世界纹素网格。
5. 从完整场景筛选有效、castShadows、Opaque、与相机 layer mask 匹配的渲染器。投射物的光源 XY AABB 与接收矩形相交，且光源 Z 不完全位于接收区域下游时，纳入深度范围；不使用相机视锥剔除投射物。
6. Z 范围覆盖接收球与候选投射物，并留出偏移余量。无效 bounds 不参与数学合并，但绘制阶段不以 bounds 剔除该物体。
7. 输出 worldToShadow 矩阵、光源视锥、世界纹素宽度与 depthRange。

近处驾驶舱和远景使用同一分区、同一采样规则。大范围投射物造成 Z 跨度增加时 D32F 与世界尺度偏移减轻误差；不承诺任意掠射角仍保留毫米精度。

## Atlas 与采样

- 两列、ceil(N/2) 行，每格 resolution²；D32F，最近邻，手动 PCF；最多一个 atlas 供当前顺序绘制的相机复用。
- 每相机在主 Pass 前重绘有效级联。图集按方向光配置的级联容量分配，可由裁剪范围不同的相机顺序复用。
- 每格通过 viewport 与局部清深度初始化；明确设置 Less、depth write、无 blend、Cull None、无 polygon offset，结束后恢复管线基线。
- `BindUniforms` 上传最多六组矩阵、分界、atlas rect、texel/depthRange，以及相机 view、世界尺度 bias、过渡和 debug 参数。
- 片元通过世界位置求视深度选择级联；先按几何法线和光照夹角施加 normal bias，再投影。
- 使用接收面导数求阴影 UV 对应的深度梯度；导数在非一致分支之前计算。雅可比退化时关闭梯度修正，拒绝非有限结果。
- 4×4 tent PCF 比较结果加权过滤；每次采样限制在当前 atlas 格的半纹素内，禁止跨级联读深度。
- 每级末端按 blendRatio 双采样并混合；最远级最后 blendRatio 范围淡出。
- 查询接口返回 0..1 遮挡，再乘 shadowStrength；只影响直接太阳光。debug 模式在示例最终颜色阶段显示级联或纹素尺寸。

## 自定义 Shader 迁移

1. 将模板的 `Builtin/shadow_common.orbinc` 复制到自定义 Shader 可访问的 include 路径。include 先按内容根解析、再按 Shader 所在目录解析，所以放进内容根的 `Builtin/` 就能被所有 Shader 共用，无需各自再拷贝一份。
2. 删除旧 v_LightSpacePosition、顶点阶段 u_LightViewProjection 计算及旧 SampleShadow() 函数。
3. 片元阶段声明 u_LightDirection 后 include 新文件。
4. 使用 SampleShadow(worldPosition, surfaceNormal) 获取遮挡，仅用 (1-shadow) 调制太阳直接光。
5. 不给材质手动配置 u_Shadow* 参数；它们由管线提供。shadow_depth.orbshader 内的 u_LightViewProjection 是深度绘制入口，继续保留。
6. 默认 `shadowBias` 为 0.0005 世界单位。扩大最后一级的结束距离时，同时检查 `Camera.farPlane`。

DevPanel 的 `Reset from Template` 可以直接用于把示例的旧 Shader 换成本文件 ABI：该操作现在会连同资源注册表一起重置，改完立即生效，不需要再手动重载项目。改用之前它只清世界、不清 `ResourceManager`，已加载的 Shader 会继续持有旧源码并沿用旧编译结果（旧程序读 `u_LightViewProjection`，而新管线不再上传，表现为**完全无阴影**）。

## 人工验收边界

在编辑器中检查实际内容的阴影清晰度和级联过渡。重点观察驾驶舱近景、低角度掠射、远景，以及编辑器主视口与 Game 视图并存时的表现。
