# Editor重构  

## 功能  

1.现在组件的引用类型字段（比如StaticMeshRenderer的mesh字段）还都是下拉框，应当改成引用框，类似UnityEditor的ObjectField。  
2.实现Panel之间的拖拽行为（EnsPanel拖到Inspector给Component的引用字段赋值、ProjectPanel拖到InspectorPanel给Component的资源引用字段赋值、ProjectPanel拖到EnsPanel或者场景里面代表创建Ens预制体实例）。
3.支持world创建和切换，支持设置启动world。  
4.ProjectPanel的目前的表现是错误，需要完全重做一下。（功能参考Unity的Project窗口（应该是这个名称），左边是目录树，右边是被选中目录的所有资产列表）  

## 外观  
 

## 统一panel外观和接口  

1.所有EditorPanel的外观样式必须统一，方便进行主题设置和机制修改。  
也就是说所有Panel继承同一个类（现在应该是做到了一部分），基础样式绘制都定义在基类里面。开发时修改dock机制时不用单独修改每个Panel代码文件，运行时更改主题也不用单独设置每个Panel参数。
2.dock系统，拖动时自动dock-float切换进行，不需要点击dock按钮。  
3.所有Panel在右上角加一个关闭小按钮（`×`或者`-`）。 

# Core功能扩展  

支持从world加载到另一个world（提供异步和同步加载）。



# 渲染系统  

新增折射Shader示例。  