using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>预编译托管方法签名；扩展签名在初始化阶段登记一次。</summary>
public static unsafe class ManagedMethodSignatures
{
    /// <summary>登记 0 个参数和一个返回值的具体签名。</summary>
    public static void Register<TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] {  },
            static (method, target) => method.CreateDelegate<Func<TResult>>(target), &InvokeMethod<TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<TResult> method = Unsafe.As<Func<TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(), ref storage);
    }

    /// <summary>登记 0 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] {  },
            static (method, target) => method.CreateDelegate<Action>(target), &InvokeAction);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action method = Unsafe.As<Action>(callback);
        method();
    }

    /// <summary>登记 1 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0) },
            static (method, target) => method.CreateDelegate<Func<T0, TResult>>(target), &InvokeMethod<T0, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, TResult> method = Unsafe.As<Func<T0, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0])), ref storage);
    }

    /// <summary>登记 1 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0) },
            static (method, target) => method.CreateDelegate<Action<T0>>(target), &InvokeAction<T0>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0> method = Unsafe.As<Action<T0>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]));
    }

    /// <summary>登记 2 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, TResult>>(target), &InvokeMethod<T0, T1, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, TResult> method = Unsafe.As<Func<T0, T1, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1])), ref storage);
    }

    /// <summary>登记 2 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1) },
            static (method, target) => method.CreateDelegate<Action<T0, T1>>(target), &InvokeAction<T0, T1>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1> method = Unsafe.As<Action<T0, T1>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]));
    }

    /// <summary>登记 3 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, TResult>>(target), &InvokeMethod<T0, T1, T2, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, TResult> method = Unsafe.As<Func<T0, T1, T2, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2])), ref storage);
    }

    /// <summary>登记 3 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2>>(target), &InvokeAction<T0, T1, T2>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2> method = Unsafe.As<Action<T0, T1, T2>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]));
    }

    /// <summary>登记 4 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3])), ref storage);
    }

    /// <summary>登记 4 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3>>(target), &InvokeAction<T0, T1, T2, T3>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3> method = Unsafe.As<Action<T0, T1, T2, T3>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]));
    }

    /// <summary>登记 5 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4])), ref storage);
    }

    /// <summary>登记 5 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4>>(target), &InvokeAction<T0, T1, T2, T3, T4>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4> method = Unsafe.As<Action<T0, T1, T2, T3, T4>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]));
    }

    /// <summary>登记 6 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5])), ref storage);
    }

    /// <summary>登记 6 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]));
    }

    /// <summary>登记 7 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6])), ref storage);
    }

    /// <summary>登记 7 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]));
    }

    /// <summary>登记 8 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7])), ref storage);
    }

    /// <summary>登记 8 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]));
    }

    /// <summary>登记 9 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8])), ref storage);
    }

    /// <summary>登记 9 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]));
    }

    /// <summary>登记 10 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9])), ref storage);
    }

    /// <summary>登记 10 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]));
    }

    /// <summary>登记 11 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10])), ref storage);
    }

    /// <summary>登记 11 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]));
    }

    /// <summary>登记 12 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11])), ref storage);
    }

    /// <summary>登记 12 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]));
    }

    /// <summary>登记 13 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12])), ref storage);
    }

    /// <summary>登记 13 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]));
    }

    /// <summary>登记 14 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13])), ref storage);
    }

    /// <summary>登记 14 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13]));
    }

    /// <summary>登记 15 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13), typeof(T14) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13]), NativeMarshaller<T14>.Read(arguments[14])), ref storage);
    }

    /// <summary>登记 15 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13), typeof(T14) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13]), NativeMarshaller<T14>.Read(arguments[14]));
    }

    /// <summary>登记 16 个参数和一个返回值的具体签名。</summary>
    public static void Register<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(TResult), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13), typeof(T14), typeof(T15) },
            static (method, target) => method.CreateDelegate<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult>>(target), &InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult>);
    }

    //按已登记类型调用委托并封送结果
    private static void InvokeMethod<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult> method = Unsafe.As<Func<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, TResult>>(callback);
        NativeMarshaller<TResult>.Write(output, method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13]), NativeMarshaller<T14>.Read(arguments[14]), NativeMarshaller<T15>.Read(arguments[15])), ref storage);
    }

    /// <summary>登记 16 个参数且无返回值的具体签名。</summary>
    public static void RegisterAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>()
    {
        ManagedScriptInterop.AddTypedSignature(typeof(void), new Type[] { typeof(T0), typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8), typeof(T9), typeof(T10), typeof(T11), typeof(T12), typeof(T13), typeof(T14), typeof(T15) },
            static (method, target) => method.CreateDelegate<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>>(target), &InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>);
    }

    //按已登记类型调用无返回值委托
    private static void InvokeAction<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>(Delegate callback, void** arguments, void* output, ref NativeMarshallingScope storage)
    {
        Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15> method = Unsafe.As<Action<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15>>(callback);
        method(NativeMarshaller<T0>.Read(arguments[0]), NativeMarshaller<T1>.Read(arguments[1]), NativeMarshaller<T2>.Read(arguments[2]), NativeMarshaller<T3>.Read(arguments[3]), NativeMarshaller<T4>.Read(arguments[4]), NativeMarshaller<T5>.Read(arguments[5]), NativeMarshaller<T6>.Read(arguments[6]), NativeMarshaller<T7>.Read(arguments[7]), NativeMarshaller<T8>.Read(arguments[8]), NativeMarshaller<T9>.Read(arguments[9]), NativeMarshaller<T10>.Read(arguments[10]), NativeMarshaller<T11>.Read(arguments[11]), NativeMarshaller<T12>.Read(arguments[12]), NativeMarshaller<T13>.Read(arguments[13]), NativeMarshaller<T14>.Read(arguments[14]), NativeMarshaller<T15>.Read(arguments[15]));
    }

}


internal static unsafe partial class ManagedScriptInterop
{
    private sealed class TypedSignature(Type result, Type[] arguments, Func<MethodInfo, Script, Delegate> bind,
        delegate* managed<Delegate, void**, void*, ref NativeMarshallingScope, void> invoke)
    {
        internal readonly Type Result = result;
        internal readonly Type[] Arguments = arguments;
        internal readonly Func<MethodInfo, Script, Delegate> Bind = bind;
        internal readonly delegate* managed<Delegate, void**, void*, ref NativeMarshallingScope, void> Invoke = invoke;
    }

    private sealed class TypedBinding(Delegate callback, TypedSignature signature)
    {
        internal readonly Delegate Callback = callback;
        internal readonly TypedSignature Signature = signature;
        internal GCHandle Handle;
    }

    private static readonly object typedSignatureGate = new();
    private static readonly List<TypedSignature> typedSignatures = [];
    private static readonly Dictionary<(uint Generation, ulong Slot, MethodInfo Method), TypedBinding> typedBindings = [];
    private static ManagedScriptInteropApi registeredApi;
    private static bool apiRegistered;
    private static bool defaultSignaturesAdded;
    [ThreadStatic] private static int typedDepth;
    [ThreadStatic] private static List<List<GCHandle>?>? typedResults;
    internal static bool HasTypedCalls => typedDepth != 0;

    //登记静态可达的泛型适配函数
    internal static void AddTypedSignature(Type result, Type[] arguments, Func<MethodInfo, Script, Delegate> bind,
        delegate* managed<Delegate, void**, void*, ref NativeMarshallingScope, void> invoke)
    {
        if ((uint)NativeValues.GetKind(result) == uint.MaxValue || arguments.Any(type => (uint)NativeValues.GetKind(type) == uint.MaxValue))
        {
            throw new NotSupportedException("The signature contains a type without a native marshalling layout.");
        }
        lock (typedSignatureGate)
        {
            if (typedSignatures.Any(entry => entry.Result == result && entry.Arguments.SequenceEqual(arguments)))
            {
                return;
            }
            typedSignatures.Add(new(result, arguments, bind, invoke));
        }
    }

    //登记常用标量、字符串、数组和引用签名
    private static void AddDefaultTypedSignatures()
    {
        if (defaultSignaturesAdded)
        {
            return;
        }
        defaultSignaturesAdded = true;
        ManagedMethodSignatures.RegisterAction();
        AddDefaultTypedType<bool>();
        AddDefaultTypedType<int>();
        AddDefaultTypedType<uint>();
        AddDefaultTypedType<ulong>();
        AddDefaultTypedType<float>();
        AddDefaultTypedType<nint>();
        AddDefaultTypedType<nuint>();
        AddDefaultTypedType<vector2>();
        AddDefaultTypedType<vector3>();
        AddDefaultTypedType<color>();
        AddDefaultTypedType<quaternion>();
        AddDefaultTypedType<EnsId>();
        AddDefaultTypedType<string>();
        AddDefaultTypedType<StringId>();
        AddDefaultTypedType<Object?>();
        AddDefaultTypedType<Transform?>();
        AddDefaultTypedType<Script?>();
        AddDefaultTypedType<bool[]>();
        AddDefaultTypedType<int[]>();
        AddDefaultTypedType<uint[]>();
        AddDefaultTypedType<ulong[]>();
        AddDefaultTypedType<float[]>();
        AddDefaultTypedType<nint[]>();
        AddDefaultTypedType<vector2[]>();
        AddDefaultTypedType<vector3[]>();
        AddDefaultTypedType<color[]>();
        AddDefaultTypedType<quaternion[]>();
        AddDefaultTypedType<EnsId[]>();
        AddDefaultTypedType<string[]>();
        AddDefaultTypedType<StringId[]>();
        AddDefaultTypedType<Object?[]>();
        AddDefaultTypedType<Transform?[]>();
        ManagedMethodSignatures.Register<int, int, int>();
        ManagedMethodSignatures.Register<uint, uint, uint>();
        ManagedMethodSignatures.Register<ulong, ulong, ulong>();
        ManagedMethodSignatures.Register<float, float, float>();
        ManagedMethodSignatures.Register<vector3, vector3, vector3>();
        ManagedMethodSignatures.Register<string, string, string>();
        ManagedMethodSignatures.Register<string, int, string>();
        ManagedMethodSignatures.Register<int, string, string>();
        ManagedMethodSignatures.Register<int[], int[], int[]>();
        ManagedMethodSignatures.Register<string[], string[], string[]>();
        ManagedMethodSignatures.Register<float, int, bool, vector3, float>();
        ManagedMethodSignatures.Register<string, int, vector3, bool, string>();
    }

    //为同一类型预置读取、写入和往返签名
    private static void AddDefaultTypedType<T>()
    {
        ManagedMethodSignatures.Register<T>();
        ManagedMethodSignatures.Register<T, T>();
        ManagedMethodSignatures.RegisterAction<T>();
    }

    //先使原生句柄失效，再释放托管上下文
    internal static void ClearTypedBindings()
    {
        if (typedBindings.Count == 0)
        {
            return;
        }
        if (apiRegistered)
        {
            fixed (ManagedScriptInteropApi* api = &registeredApi)
            {
                NativeCallFrame.ThrowIfFailed(ScriptInteropDispatch.RegisterManaged(api));
            }
        }
        foreach (TypedBinding binding in typedBindings.Values)
        {
            binding.Handle.Free();
        }
        typedBindings.Clear();
    }

    //卸载前移除签名中的游戏类型引用
    internal static void RemoveTypedSignatures(Assembly assembly)
    {
        ClearTypedBindings();
        lock (typedSignatureGate)
        {
            typedSignatures.RemoveAll(entry => UsesAssembly(entry.Result, assembly) || entry.Arguments.Any(type => UsesAssembly(type, assembly)));
        }
    }

    //检查数组及泛型参数所属程序集
    private static bool UsesAssembly(Type type, Assembly assembly) => type.Assembly == assembly
        || type.HasElementType && UsesAssembly(type.GetElementType()!, assembly)
        || type.IsGenericType && type.GetGenericArguments().Any(argument => UsesAssembly(argument, assembly));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus BindTypedMethodAbi(ComponentHandle component, byte* name, int length, uint result, uint* arguments, int count, nint* context)
    {
        InteropStatus entered = EnterLocalCall();
        if (entered != InteropStatus.Ok)
        {
            return entered;
        }
        try
        {
            if (length <= 0 || name == null || count < 0 || count > 16 || count != 0 && arguments == null || context == null)
            {
                return InteropStatus.InvalidArgument;
            }
            if (!ScriptRuntimeRegistry.TryResolve(component, out Script? script))
            {
                return InteropStatus.StaleHandle;
            }
            string methodName = InteropText.DecodeUtf8(name, length);
            MethodInfo? selected = null;
            bool parametersMatched = false;
            foreach (MethodInfo method in script.GetType().GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (method.Name != methodName || method.ContainsGenericParameters || method.ReturnType.IsByRef)
                {
                    continue;
                }
                ParameterInfo[] parameters = method.GetParameters();
                if (parameters.Length != count)
                {
                    continue;
                }
                bool matches = true;
                for (int index = 0; index < count; ++index)
                {
                    if ((uint)NativeValues.GetKind(parameters[index].ParameterType) != arguments[index])
                    {
                        matches = false;
                        break;
                    }
                }
                if (!matches)
                {
                    continue;
                }
                parametersMatched = true;
                if ((uint)NativeValues.GetKind(method.ReturnType) != result)
                {
                    continue;
                }
                if (selected != null)
                {
                    return InteropStatus.AmbiguousMethod;
                }
                selected = method;
            }
            if (selected == null)
            {
                return parametersMatched ? InteropStatus.TypeMismatch : InteropStatus.NotFound;
            }
            var key = (component.Generation, component.Slot, selected);
            if (!typedBindings.TryGetValue(key, out TypedBinding? binding))
            {
                Type[] parameterTypes = selected.GetParameters().Select(parameter => parameter.ParameterType).ToArray();
                TypedSignature? signature;
                lock (typedSignatureGate)
                {
                    signature = typedSignatures.Find(entry => entry.Result == selected.ReturnType && entry.Arguments.SequenceEqual(parameterTypes));
                }
                if (signature == null)
                {
                    return InteropStatus.UnsupportedType;
                }
                binding = new(signature.Bind(selected, script), signature);
                binding.Handle = GCHandle.Alloc(binding);
                try { typedBindings.Add(key, binding); }
                catch { binding.Handle.Free(); throw; }
            }
            *context = GCHandle.ToIntPtr(binding.Handle);
            return InteropStatus.Ok;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitLocalCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus InvokeTypedMethodAbi(nint context, void** arguments, void* output)
    {
        ++typedDepth;
        NativeMarshallingScope storage = new();
        try
        {
            TypedBinding binding = Unsafe.As<TypedBinding>(GCHandle.FromIntPtr(context).Target!);
            binding.Signature.Invoke(binding.Callback, arguments, output, ref storage);
            if (storage.HasPins)
            {
                typedResults ??= [];
                while (typedResults.Count < typedDepth)
                {
                    typedResults.Add(null);
                }
                typedResults[typedDepth - 1] = storage.TakePins();
            }
            GC.KeepAlive(binding);
            return InteropStatus.Ok;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { storage.Dispose(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CompleteTypedMethodAbi()
    {
        int slot = typedDepth - 1;
        if (typedResults != null && slot < typedResults.Count)
        {
            List<GCHandle>? pins = typedResults[slot];
            if (pins != null)
            {
                foreach (GCHandle pin in pins)
                {
                    pin.Free();
                }
                typedResults[slot] = null;
            }
        }
        --typedDepth;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostAttachedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostAttached(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostDetachedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostDetached(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostEnabledChangedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostEnabledChanged(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostFieldChangedAbi(IntPtr host, byte* name, int length)
    {
        try
        {
            if (host == IntPtr.Zero || name == null || length <= 0)
                return InteropStatus.InvalidArgument;
            return ScriptRuntime.OnHostFieldChanged(host, InteropText.DecodeUtf8(name, length));
        }
        catch { return InteropStatus.InvocationFailed; }
    }
}
