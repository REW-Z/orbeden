# 主线程协程

C++ 与 C# 脚本通过 `RunCo` 启动协程。启动时立即执行到第一个等待点，后续在全部 Update 之后、LateUpdate 之前继续执行。协程代码和等待条件都运行在主线程。

## C# 用法

```csharp
using System.Collections.Generic;
using Orbeden;

public sealed class Countdown : Script
{
    private bool ready = false;

    public Countdown(Ens ens) : base(ens) {}

    private void OnStart()
    {
        RunCo(Execute());
    }

    private IEnumerator<CoOperation?> Execute()
    {
        yield return null;
        yield return new Wait(1.0);
        yield return new WaitUntil(() => ready);
        yield return RunCo(Finish());
    }

    private IEnumerator<CoOperation?> Finish()
    {
        yield return new Wait(0.5);
    }
}
```

## C++ 用法

脚本头文件包含 `Runtime/Object/Script.h` 后即可使用协程类型。

```cpp
class Countdown final : public Script
{
    OBJECT_TYPE_DECLARE(Countdown)

private:
    bool ready = false;

    //启动协程
    void OnStart()
    {
        RunCo(Execute());
    }

    //依次等待帧、时间、条件与子协程
    CoRoutine Execute()
    {
        co_yield nullptr;
        co_yield Wait(1.0);
        co_yield WaitUntil([this] { return ready; });
        co_yield RunCo(Finish());
    }

    //执行收尾等待
    CoRoutine Finish()
    {
        co_yield Wait(0.5);
    }
};
```

## 等待项

| 等待项 | 恢复条件 |
| --- | --- |
| `null` / `nullptr` | 最早下一帧 |
| `Wait(seconds)` | 本次等待累计的有效更新秒数达到指定值 |
| `WaitUntil(predicate)` | 无参条件函数返回 true |
| `WaitWhile(predicate)` | 无参条件函数返回 false |
| `CoRoutine` | 目标协程正常结束、被停止或失败 |
| `ThreadOperation` | 后台函数执行结束或失败 |
| `ResourceLoadOperation` | 资源及其依赖登记完成，或请求失败 |

任何等待项至少让出到下一帧，包括 `Wait(0)` 和已完成的 `CoRoutine`。一个协程在同一帧最多由调度器恢复一次。固定步中启动的协程也遵循这个规则。

`Wait` 接收有限的非负秒数。计时从让出后的有效更新开始；游戏暂停、脚本 disabled 或所属 Ens 非活动时不推进协程、不检查条件，也不累计等待时间。恢复活动后继续执行。

本次等待经过的时间保存在协程状态中，同一个 `Wait` 可以在多段等待或多个协程中复用。自定义等待项派生 `CoOperation` 并实现 `ShouldWait(elapsedSeconds)`；返回 true 继续等待，返回 false 恢复。自定义项的可变状态由实现方管理。

## 停止与生命周期

保存 `RunCo` 返回的 `CoRoutine` 后，可以调用当前脚本的 `StopCo(routine)` 停止指定协程，或调用 `StopAllCo()` 停止当前脚本的全部协程。停止其他脚本的句柄不生效。

句柄通过 `IsFinished`、`WasStopped` 和 `Error` 查询状态；C++ 使用 `IsFinished()`、`WasStopped()` 和 `GetError()`。停止当前执行中的协程会先记录状态，在该段代码返回或让出后释放。

脚本销毁前自动停止所属协程；世界替换、退出 Play、运行时关闭时清理全部协程。C# 释放枚举器并执行已进入作用域的 finally，C++ 销毁协程帧并释放局部对象。清理期间不能启动新协程。

等待一个子协程的结束不会传播它的失败，也不会随父协程停止而停止子协程。需要时检查子协程句柄的状态，或显式停止它。

## 使用约束

- `RunCo` 用于活动的运行时脚本，编辑模式与脚本构造期间不启动游戏协程。
- 协程函数或条件函数抛出异常时，记录错误并结束该协程，其他协程继续执行。
- 每一段代码运行到下一个等待点才交回控制权，耗时计算和阻塞 I/O 会占用主线程。
- C++ 协程参数按值保存，跨帧使用的引用需要由调用方维持有效生命周期。建议使用脚本成员协程函数。
- 先调用 `RunCo` 再等待其句柄；每个原生协程状态只能启动一次。

## 后台函数

创建 `ThreadOperation` 时提交后台任务。C++ 使用按需启动、最多 4 条工作线程的共享线程池，C# 使用 CLR 线程池；两者保留原有协程等待接口。以下片段放在协程函数中：

```csharp
var work = new ThreadOperation(() => System.IO.File.WriteAllText("report.txt", "ready"));
yield return work;
if (!work.Succeeded)
{
    System.Console.WriteLine(work.Error);
}
```

```cpp
auto work = std::make_shared<ThreadOperation>([]
{
    std::ofstream("report.txt") << "ready";
});
co_yield work;
if (!work->Succeeded())
{
    Log::Error(work->GetError().c_str());
}
```

C++ 包含 `Runtime/Coroutines/ThreadOperation.h`，上述文件写入示例另需 `<fstream>` 和 `Log/Log.h`。回调应是普通的同步函数；C# 的 `async void` 回调在首次异步让出时就返回，不能代表整段异步工作完成。后台回调操作自己的数据，通过等待结束将结果交回主线程；引擎对象与资源管理接口在主线程使用。

停止协程不会中止已经启动的后台函数。运行时退出和脚本程序集卸载会等待排队及执行中的任务结束，因此回调应能自行结束。原生线程池排空后回收工作线程，下次提交时可重新启动；不要在工作线程里同步等待同一池中的其他任务。C++ 的 `Cancel()` 仅取消尚未开始的回调，不中止正在执行的函数。

## 异步资源加载

C# 与 C++ 使用相同的资源 Key，等待后通过 `Succeeded` 和结果判断是否加载成功：

```csharp
var load = Resources.LoadAsync<TextResource>("Data/settings.json");
yield return load;
if (load.Succeeded)
{
    TextResource? settings = load.Result as TextResource;
}
else
{
    System.Console.WriteLine(load.Error);
}
```

```cpp
auto load = ResourceManager::LoadAsync<TextResource>("Data/settings.json");
co_yield load;
if (load->Succeeded())
{
    TextResource* settings = load->GetResult()->Cast<TextResource>();
}
else
{
    Log::Error(load->GetError().c_str());
}
```

C++ 包含 `ResourceManager/ResourceManager.h` 和具体资源类型头文件，如 `Runtime/Object/TextResource.h`。加载请求通过返回的共享句柄持有。

同步与异步加载复用文件读取、导入、对象创建和依赖加载逻辑。同步加载在主线程直接执行；异步加载由首次需要后台加载时启动的线程池处理，线程数按硬件并发数取值，上限为 4；独立请求可以并行准备，在私有分配区创建资源对象，主线程登记完整资源后统一开放查询。等待项的 `IsFinished` 查询和 `ShouldWait` 不执行加载或登记。资源登记在编辑模式与暂停期间也会推进，暂停的协程则在恢复后继续。

重复请求会复用同一资源或源文件的后台构建；单个请求的依赖仍在该任务内加载，登记时复用已经加载的资源对象。已经开始的主线程登记会优先完成，再登记其他准备完成的请求。主线程默认以 2 毫秒为目标分批登记；依赖校验与最后的统一发布不受这个目标严格约束。同步加载会先完成已经准备好的后台结果的登记，再直接加载目标，不等待尚未准备好的后台任务。

请求的构建暂存数据保存在 `ResourceLoadOperation` 内部的共享 `State`，完成后释放。加载句柄存活期间，成功结果及其依赖参与资源保活；显式卸载仍可使结果失效。卸载、重导入、路径变更与内容切换会使相关旧请求失效，错误保存在 `Error`／`GetError()`。停止等待协程不会取消资源请求。

C++ 和 C# 的 `ResourceLoadOperation` 都是非泛型等待项，`LoadAsync<T>` 指定请求的资源类型。C++ 声明标记 `ORBEDEN_BIND_HANDLE`，MetaGen 自动生成共享句柄管理、原生调用入口和 C# 包装，包括状态、错误、结果与协程等待方法。C# 包装回收或原生 API 会话断开时释放对应句柄。

编辑器接收控制台关闭、Ctrl+C 或 Ctrl+Break 后请求主线程结束运行，退出时取消排队任务、等待正在执行的任务并回收加载线程。控制台处理线程只发送退出请求并等待主线程清理完成；系统对控制台关闭有退出时限，耗时任务仍应及时结束。
