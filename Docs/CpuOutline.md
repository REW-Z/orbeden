# CPU Outline  

新增编辑器侧 EditorSelectionOutline，按 Mesh* + revision + 当前有效索引范围缓存三角形和边邻接关系。
按包围盒尺度 1e-5 焊接同位置顶点，避免 UV/法线接缝被误认为轮廓。
每帧使用 RenderItem::localToWorld 和 EditorCamera 判断三角形朝向；绘制正反面交界边、开放边，以及朝向混合的非流形边。
对线段执行六平面齐次裁剪，再映射到 ImGui 主视口逻辑坐标，并裁剪到 Workspace。
每条边绘制三层：7px 低透明光晕、4px 中透明过渡、2px 实色核心。
显式选择为橙色，所选父节点的后代 Renderer 为蓝色；显式选择优先并最后绘制。
无选择时完全跳过；拓扑仅在 Mesh revision 变化时重建，不自动降级为包围框。