using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Threading;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct AsyncOperationBindApi
{
    internal delegate* unmanaged[Cdecl]<void> WakeMainThread;
}
#pragma warning restore CS0649

/// <summary>立即启动独立线程执行函数的协程等待项</summary>
public sealed class ThreadOperation : CoOperation
{
    private sealed class State
    {
        internal Thread thread = null!;
        internal Action? function;
        internal int finished;
        internal string error = string.Empty;
    }

    private static readonly object gate = new();
    private static readonly List<State> active = [];
    private static bool stopping;
    private readonly State state;

    /// <summary>启动一条独立线程执行指定函数</summary>
    public ThreadOperation(Action function)
    {
        ArgumentNullException.ThrowIfNull(function);
        state = new() { function = function };
        state.thread = new Thread(() =>
        {
            try
            {
                state.function!();
            }
            catch (Exception exception)
            {
                state.error = exception.ToString();
            }
            state.function = null;
            Volatile.Write(ref state.finished, 1);
            NativeBindingRuntime.WakeMainThread();
        }) { IsBackground = true, Name = "Orbeden function" };

        lock (gate)
        {
            if (stopping)
            {
                throw new InvalidOperationException("Thread operations cannot start during shutdown.");
            }
            active.Add(state);
            try
            {
                state.thread.Start();
            }
            catch
            {
                active.Remove(state);
                throw;
            }
        }
    }

    /// <summary>函数是否已经结束</summary>
    public bool IsFinished => Volatile.Read(ref state.finished) != 0;

    /// <summary>函数是否执行成功</summary>
    public bool Succeeded => IsFinished && state.error.Length == 0;

    /// <summary>后台函数错误，尚未失败时为空</summary>
    public string Error => IsFinished ? state.error : string.Empty;

    /// <summary>等待后台函数结束</summary>
    public override bool ShouldWait(double elapsedSeconds) => !IsFinished;

    //回收已经结束的线程
    internal static void ProcessFinished()
    {
        lock (gate)
        {
            if (active.Count == 0)
            {
                return;
            }
        }
        List<State> finished = [];
        lock (gate)
        {
            foreach (State state in active)
            {
                if (Volatile.Read(ref state.finished) != 0)
                {
                    finished.Add(state);
                }
            }
            foreach (State state in finished)
            {
                active.Remove(state);
            }
        }
        foreach (State state in finished)
        {
            state.thread.Join();
        }
    }

    //等待并回收全部线程
    internal static void WaitAll()
    {
        State[] pending;
        lock (gate)
        {
            stopping = true;
            pending = [.. active];
            active.Clear();
        }
        foreach (State state in pending)
        {
            state.thread.Join();
        }
        lock (gate)
        {
            stopping = false;
        }
    }
}
