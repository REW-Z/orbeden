# OrbEvent

OrbEvent 是可序列化的同步多播事件，提供 `Subscribe`、`Unsubscribe`、`ClearSubscriptions` 和 `Dispatch`。UI 输入仍由 FIFO 队列派发；队列到达控件时调用相应事件字段。

```csharp
public OrbEvent finished = new();
public OrbEvent<float> progressChanged = new();

finished.Subscribe(OnFinished);
finished.Dispatch();
progressChanged.Subscribe(OnProgressChanged);
progressChanged.Dispatch(0.5f);
```

```cpp
#include "Runtime/OrbEvent.h"

OrbEvent finished;
OrbEvent progressChanged({ Reflection::ValueKind::Float32 });
auto subscription = finished.Subscribe([](std::span<const Reflection::Value>) { /*处理完成*/ });
finished.Dispatch();
List<Reflection::Value> arguments{ Reflection::Value(0.5f) };
progressChanged.Dispatch(arguments);
finished.Unsubscribe(subscription);
```

托管字段公开或标注 SerializeField 后自动持久化；原生字段进入 MetaGen 反射。原生绑定的事件属性按值传输持久化配置，修改后将事件写回属性；运行时订阅属于创建事件的语言侧实例，不序列化。托管字段读回配置时保留原事件对象及其运行时订阅。原生事件复制只复制签名和持久化调用，赋值要求相同签名并保留目标运行时订阅。

每条 OrbEventCall 保存目标 Ens 的稳定键、Native/Managed 语言域、完整组件类型名、同类型实例序号、方法名、启用状态与参数传递方式。实例序号从零开始。方法按事件参数种类精确匹配，也可以配置无参方法；不做隐式类型转换。跨语言调用复用 ComponentProxy，失效目标和方法失败只影响当前调用。Inspector 的函数选择器仅列出无返回值且参数匹配的方法。

支持的参数包括 bool、int32、uint32、uint64、float32、string、StringId、vector2、vector3、color、quaternion、EnsId 和 Object。托管单参便利类型使用 `OrbEvent<T>`；多个参数使用带 InteropValueKind 签名的 OrbEvent 与 InteropValue 参数数组。集合不作为事件参数。

调用开始时同时快照持久化调用与运行时订阅，随后先执行持久化调用，再执行运行时订阅。回调增删订阅或编辑调用配置从下一次派发生效，单个回调异常不阻断后续回调。直接递归派发的最大深度为 32。

持久化格式版本为 1，嵌套使用现有 UTF-8 字节长度前缀数组：顶层为格式版本、参数签名数组和各调用行；调用行依次保存目标键、语言域、组件类型、实例序号、方法、启用标记、传参标记。完整验证后才替换配置，损坏数据或不匹配签名不覆盖原值。运行时代理、委托和订阅编号不落盘。复制层级和 Prefab 实例化时重映射行内目标键，缺失目标键保留用于修复。

事件以独立字段声明和序列化，暂不支持事件数组或事件列表。

Button 使用同节点的 Image 显示状态色，并通过 DependsOnComponent 声明此依赖；点击事件字段名为 ClickEvent。事件字段在 Inspector 普通属性之后显示。每条调用只显示 Enabled、Target 和 Function，Function 下拉项同时确定语言域、组件实例、方法及参数传递方式：无参方法不传参数，签名匹配的方法接收事件参数。

项目版本为 66。旧控件绑定需要重新配置到对应事件字段；旧五列数组不再读取，内容更新不自动迁移场景。
