# 基础渲染冒烟

同一个程序经公共 `RenderBackend` 接口验证 OpenGL 4.3 和 Vulkan 1.3，不需要游戏项目或 NativeAOT 产物。它创建隐藏的 GLFW 窗口，真实提交 GPU 绘制并检查深度回读。

先按目标配置构建 OrbedenThirdParty 和 OrbedenCore，再在仓库根目录执行：

```powershell
cmake -S Tools/OrbedenRenderSmoke -B Temp/RenderSmoke -A x64
cmake --build Temp/RenderSmoke --config Debug
ctest --test-dir Temp/RenderSmoke -C Debug --output-on-failure
```

也可分别运行：

```powershell
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api opengl
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api vulkan
```

覆盖参数快照、流式缓冲同帧更新与跨帧保留、CCW 剔除、纹理采样、HDR / 深度复制、异步深度统计、显示输出、实例输入、屏幕与离屏片元坐标，以及附加窗口和 ImGui 动态字体。随后用真实资源导入器编译模板中的全部 `.orbshader` 及其几何变体。构建还会检查 Player 入口源码。

资源缓存检查包含同帧采样槽切换、已采样纹理的更新与恢复，以及连续空等待后继续绘制；深度回读也会覆盖命令缓冲切换后的绑定恢复。

需要 Windows x64 图形会话与对应驱动；Vulkan 还需要支持 1.3、Dynamic Rendering、Synchronization2 和非实心填充的设备。测试不替代完整场景画面对比、Editor 交互回归或 Player 打包验证。

构建 Editor 后可加入设置切换冒烟：

```powershell
cmake -S Tools/OrbedenRenderSmoke -B Temp/RenderSmoke -A x64 -DORBEDEN_EDITOR_BACKEND_SMOKE=ON
cmake --build Temp/RenderSmoke --config Debug
ctest --test-dir Temp/RenderSmoke -C Debug --output-on-failure
```

该测试加载真实托管 Editor 和临时项目，显示浮动的 Editor Settings，按项目配置连续切换 OpenGL → Vulkan → OpenGL 两轮，继续绘制网格、选中描边和线框，检查未保存 World 对象、脏标记、选择、逻辑离屏目标、输入法钩子、浮动布局和配置持久化，并验证不支持的后端保留原窗口。原生异常写入测试日志，不弹出系统错误对话框。

CPU 提交性能可用固定负载复测：

```powershell
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api opengl --submission-benchmark
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api vulkan --submission-benchmark
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api opengl --scene-benchmark
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api vulkan --scene-benchmark
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api opengl --scene-benchmark --profile-capture
Temp/RenderSmoke/Debug/OrbedenRenderSmoke.exe --graphics-api vulkan --scene-benchmark --profile-capture
```

`--submission-benchmark` 直接执行 240 次同材质绘制，包含重复参数写入、数组参数与逐对象矩阵变化。`--scene-benchmark` 通过真实 RenderSystem / ForwardPipeline 绘制 240 个同材质物体，使用 Individual 策略保留 240 次绘制。两种模式预热 10 帧、统计 50 帧，输出中位数与 P90；等待 GPU 完成位于计时区间之外。结果用于同一后端、设备与构建配置下的修改前后比较，不等同于完整 Editor 的整帧耗时或 GPU 时间。普通冒烟还验证数组首元素别名、动态参数偏移与跨帧快照。

场景模式加 `--profile-capture` 后开启真实 Profiler 采集及原有阶段 GPU 完成等待，这些阶段等待计入 Render 耗时；帧末的 Present 与额外完成等待仍在计时区间外。此模式检查 60 帧均已采集且没有丢失事件，用于复查录制路径，不能和未录制的数值混用。
