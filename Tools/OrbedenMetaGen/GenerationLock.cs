using System.Security.Cryptography;
using System.Text;

namespace OrbedenMetaGen;

/// <summary>串行化同一目录的生成，并在异常退出路径释放锁。</summary>
internal sealed class GenerationLock : IDisposable
{
    private readonly Mutex mutex;

    internal GenerationLock(string outputDirectory)
    {
        string key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(outputDirectory.ToUpperInvariant())));
        mutex = new Mutex(false, "OrbedenMetaGen_" + key);
        try { mutex.WaitOne(); }
        catch (AbandonedMutexException) { }
    }

    public void Dispose()
    {
        mutex.ReleaseMutex();
        mutex.Dispose();
    }
}