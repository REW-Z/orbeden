using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>在主线程登记、推进和释放协程</summary>
internal sealed class CoScheduler
{
    private readonly List<CoRoutine> routines = [];
    private readonly int mainThread = Environment.CurrentManagedThreadId;
    private ulong frame;
    private bool stopping;
    private bool updating;

    //检查主线程调用
    private void CheckThread()
    {
        if (Environment.CurrentManagedThreadId != mainThread)
        {
            throw new InvalidOperationException("CoRoutines must be accessed on the main thread.");
        }
    }

    //开始新帧
    internal void BeginFrame()
    {
        CheckThread();
        ++frame;
    }

    //登记宿主并立即执行到第一个等待点
    internal CoRoutine Start(object owner, IEnumerator<CoOperation?> routine)
    {
        CheckThread();
        ArgumentNullException.ThrowIfNull(routine);
        if (stopping)
        {
            throw new InvalidOperationException("CoRoutines cannot start during cleanup.");
        }

        //登记并执行协程首段
        CoRoutine state = new(owner, routine) { nextFrame = frame + 1 };
        routines.Add(state);
        Advance(state, 0.0f, true);
        return state;
    }

    //停止指定宿主的一个协程
    internal void Stop(object owner, CoRoutine state)
    {
        CheckThread();
        if (state.IsFinished || !ReferenceEquals(state.owner, owner))
        {
            return;
        }

        state.WasStopped = true;
        state.IsFinished = true;
        Release(state);
    }

    //停止指定宿主的全部协程
    internal void StopOwner(object owner)
    {
        CheckThread();
        bool previous = stopping;
        stopping = true;
        int count = routines.Count;
        for (int index = 0; index < count; ++index)
        {
            Stop(owner, routines[index]);
        }
        stopping = previous;
    }

    //停止并释放全部协程
    internal void StopAll()
    {
        CheckThread();
        bool previous = stopping;
        stopping = true;
        int count = routines.Count;
        for (int index = 0; index < count; ++index)
        {
            CoRoutine state = routines[index];
            if (state.owner != null)
            {
                Stop(state.owner, state);
            }
        }
        stopping = previous;

        if (!updating && !previous)
        {
            routines.Clear();
        }
    }

    //释放结束协程的枚举器与引用
    private void Release(CoRoutine state)
    {
        if (!state.IsFinished || state.executing || state.routine == null)
        {
            return;
        }

        //分离结束状态
        state.executing = true;
        bool previous = stopping;
        stopping = true;
        IEnumerator<CoOperation?> routine = state.routine;
        state.routine = null;
        state.operation = null;
        state.owner = null;

        //释放枚举器
        try
        {
            routine.Dispose();
        }
        catch (Exception exception)
        {
            string cleanupError = exception.ToString();
            state.Error = state.Error.Length == 0 ? cleanupError : state.Error + Environment.NewLine + cleanupError;
            Console.Error.WriteLine($"CoRoutine cleanup failed: {exception}");
        }
        finally
        {
            stopping = previous;
            state.executing = false;
        }
    }

    //执行等待检查和下一段协程
    private void Advance(CoRoutine state, float deltaTime, bool first)
    {
        state.executing = true;
        try
        {
            //检查当前等待项
            bool waiting = false;
            if (!first)
            {
                state.elapsedSeconds += deltaTime;
                waiting = state.operation?.ShouldWait(state.elapsedSeconds) ?? false;
            }

            //执行下一段协程
            if (!waiting && !state.IsFinished)
            {
                state.operation = null;
                bool moved = state.routine!.MoveNext();
                if (!moved)
                {
                    state.IsFinished = true;
                }
                else if (!state.IsFinished)
                {
                    state.operation = state.routine.Current;
                }
                state.elapsedSeconds = 0.0;
                state.nextFrame = frame + 1;
            }
        }
        catch (Exception exception)
        {
            state.IsFinished = true;
            state.Error = exception.ToString();
            Console.Error.WriteLine($"CoRoutine failed: {exception}");
        }
        finally
        {
            state.executing = false;
            Release(state);
        }
    }

    //恢复本帧到期且宿主活动的协程
    internal void Update(float deltaTime, Func<object, bool> isOwnerActive)
    {
        CheckThread();
        if (updating)
        {
            throw new InvalidOperationException("CoRoutine updates cannot be nested.");
        }

        //推进本帧已有协程
        updating = true;
        try
        {
            int count = routines.Count;
            for (int index = 0; index < count; ++index)
            {
                CoRoutine state = routines[index];
                if (!state.IsFinished && state.nextFrame <= frame && isOwnerActive(state.owner!))
                {
                    Advance(state, deltaTime, false);
                }
            }
        }
        finally
        {
            updating = false;

            //移除结束项
            routines.RemoveAll(static state => state.IsFinished);
        }
    }
}
