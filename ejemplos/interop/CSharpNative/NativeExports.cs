using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

public enum CfvType : int { Null = 0, Integer = 1, Decimal = 2, Text = 3 }

[StructLayout(LayoutKind.Sequential)]
public unsafe struct CfvValue
{
    public CfvType Type;
    public long Integer;
    public double Decimal;
    public byte* Text;
    public void* Owner;
    public delegate* unmanaged[Cdecl]<void*, void> Release;
}

public static unsafe class NativeExports
{
    [UnmanagedCallersOnly(EntryPoint = "csharp_add", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Add(
        CfvValue* arguments, nuint count, CfvValue* result,
        byte* errorBuffer, nuint errorBufferSize)
    {
        try {
            if (count != 2 || arguments[0].Type != CfvType.Integer || arguments[1].Type != CfvType.Integer)
                return WriteError(errorBuffer, errorBufferSize, "csharp_add requiere dos enteros");
            *result = new CfvValue {
                Type = CfvType.Integer,
                Integer = checked(arguments[0].Integer + arguments[1].Integer)
            };
            return 0;
        } catch (Exception exception) {
            return WriteException(errorBuffer, errorBufferSize, exception);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "csharp_half", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Half(
        CfvValue* arguments, nuint count, CfvValue* result,
        byte* errorBuffer, nuint errorBufferSize)
    {
        try {
            if (count != 1 || (arguments[0].Type != CfvType.Integer && arguments[0].Type != CfvType.Decimal))
                return WriteError(errorBuffer, errorBufferSize, "csharp_half requiere un número");
            double value = arguments[0].Type == CfvType.Integer ? arguments[0].Integer : arguments[0].Decimal;
            *result = new CfvValue { Type = CfvType.Decimal, Decimal = value / 2.0 };
            return 0;
        } catch (Exception exception) {
            return WriteException(errorBuffer, errorBufferSize, exception);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "csharp_greet", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Greet(
        CfvValue* arguments, nuint count, CfvValue* result,
        byte* errorBuffer, nuint errorBufferSize)
    {
        try {
            if (count != 1 || arguments[0].Type != CfvType.Text)
                return WriteError(errorBuffer, errorBufferSize, "csharp_greet requiere texto");
            string name = Marshal.PtrToStringUTF8((IntPtr)arguments[0].Text) ?? "";
            IntPtr returnedText = Marshal.StringToCoTaskMemUTF8($"Hola {name} desde C#");
            *result = new CfvValue {
                Type = CfvType.Text,
                Text = (byte*)returnedText,
                Owner = (void*)returnedText,
                Release = &ReleaseText
            };
            return 0;
        } catch (Exception exception) {
            return WriteException(errorBuffer, errorBufferSize, exception);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "csharp_fail", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Fail(
        CfvValue* arguments, nuint count, CfvValue* result,
        byte* errorBuffer, nuint errorBufferSize)
    {
        try {
            throw new InvalidOperationException("fallo controlado desde C#");
        } catch (Exception exception) {
            return WriteException(errorBuffer, errorBufferSize, exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void ReleaseText(void* owner)
    {
        if (owner != null) Marshal.FreeCoTaskMem((IntPtr)owner);
    }

    private static int WriteError(byte* buffer, nuint size, string message)
    {
        if (buffer != null && size > 0) {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(message);
            int length = Math.Min(bytes.Length, checked((int)size - 1));
            Marshal.Copy(bytes, 0, (IntPtr)buffer, length);
            buffer[length] = 0;
        }
        return 1;
    }

    private static int WriteException(byte* buffer, nuint size, Exception exception) =>
        WriteError(buffer, size, $"excepción C#: {exception.Message}");
}
