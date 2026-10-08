using System.Text;

namespace Orbeden;

internal static unsafe class NativeText
{
    //调用单字符串输入的 native bool 函数。
    internal static bool CallBool(delegate* unmanaged[Cdecl]<byte*, int, byte> function, string? value)
    {
        if (function == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length) != 0;
        }
    }

    //调用双字符串输入的 native bool 函数。
    internal static bool CallBool(delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> function, string? first, string? second)
    {
        if (function == null) return false;

        byte[] firstBytes = Encoding.UTF8.GetBytes(first ?? string.Empty);
        byte[] secondBytes = Encoding.UTF8.GetBytes(second ?? string.Empty);
        fixed (byte* firstPointer = firstBytes)
        fixed (byte* secondPointer = secondBytes)
        {
            return function(firstPointer, firstBytes.Length, secondPointer, secondBytes.Length) != 0;
        }
    }

    //调用三字符串输入的 native bool 函数。
    internal static bool CallBool(delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, byte> function, string? first, string? second, string? third)
    {
        if (function == null) return false;

        byte[] firstBytes = Encoding.UTF8.GetBytes(first ?? string.Empty);
        byte[] secondBytes = Encoding.UTF8.GetBytes(second ?? string.Empty);
        byte[] thirdBytes = Encoding.UTF8.GetBytes(third ?? string.Empty);
        fixed (byte* firstPointer = firstBytes)
        fixed (byte* secondPointer = secondBytes)
        fixed (byte* thirdPointer = thirdBytes)
        {
            return function(firstPointer, firstBytes.Length, secondPointer, secondBytes.Length, thirdPointer, thirdBytes.Length) != 0;
        }
    }

    //调用单字符串输入的 native int 函数。
    internal static int CallInt(delegate* unmanaged[Cdecl]<byte*, int, int> function, string? value)
    {
        if (function == null) return 0;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //调用单字符串输入的 native uint 函数。
    internal static uint CallUInt(delegate* unmanaged[Cdecl]<byte*, int, int, uint> function, string? value, int index)
    {
        if (function == null) return 0;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length, index);
        }
    }

    //调用单字符串输入的 native ulong 函数。
    internal static ulong CallULong(delegate* unmanaged[Cdecl]<byte*, int, ulong> function, string? value)
    {
        if (function == null) return 0;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //调用单字符串输入的 native string 函数。
    internal static string GetString(delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> function, string? value)
    {
        if (function == null) return string.Empty;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            int requiredBytes = function(pointer, bytes.Length, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            byte[] output = new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = function(pointer, bytes.Length, outputPointer, output.Length);
                int length = Math.Clamp(actualBytes, 0, output.Length);
                return Encoding.UTF8.GetString(output, 0, length);
            }
        }
    }

    //调用字符串和索引输入的 native string 函数。
    internal static string GetString(delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> function, string? value, int index)
    {
        if (function == null) return string.Empty;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            int requiredBytes = function(pointer, bytes.Length, index, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            byte[] output = new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = function(pointer, bytes.Length, index, outputPointer, output.Length);
                int length = Math.Clamp(actualBytes, 0, output.Length);
                return Encoding.UTF8.GetString(output, 0, length);
            }
        }
    }

    //调用双字符串输入的 native string 函数。
    internal static string GetString(delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, int> function, string? first, string? second)
    {
        if (function == null) return string.Empty;

        byte[] firstBytes = Encoding.UTF8.GetBytes(first ?? string.Empty);
        byte[] secondBytes = Encoding.UTF8.GetBytes(second ?? string.Empty);
        fixed (byte* firstPointer = firstBytes)
        fixed (byte* secondPointer = secondBytes)
        {
            int requiredBytes = function(firstPointer, firstBytes.Length, secondPointer, secondBytes.Length, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            byte[] output = new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = function(firstPointer, firstBytes.Length, secondPointer, secondBytes.Length, outputPointer, output.Length);
                int length = Math.Clamp(actualBytes, 0, output.Length);
                return Encoding.UTF8.GetString(output, 0, length);
            }
        }
    }
}
