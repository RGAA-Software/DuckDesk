$ErrorActionPreference='Stop'
$env:WINPR_NATIVE_SSPI='0'
$env:OPENSSL_MODULES='C:/source/vcpkg/installed/x64-windows/bin'
$env:PATH="D:/source/GoCloud/GammaRayPremium/.cache/rdp_proxy_probe/winpr/libwinpr/Release;$env:PATH"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SspiPackageProbe {
 [StructLayout(LayoutKind.Sequential)] public struct Handle {public IntPtr Lower; public IntPtr Upper;}
 [DllImport("winpr3.dll",ExactSpelling=true)] static extern IntPtr InitSecurityInterfaceExW(uint flags);
 [UnmanagedFunctionPointer(CallingConvention.Winapi,CharSet=CharSet.Unicode)]
 delegate uint Acquire(IntPtr principal,string package,uint use,IntPtr logon,IntPtr auth,IntPtr key,IntPtr arg,out Handle handle,out long expiry);
 [UnmanagedFunctionPointer(CallingConvention.Winapi)] delegate uint Free(ref Handle handle);
 public static void Run() {
  var table=InitSecurityInterfaceExW(0);
  // SecurityFunctionTable: DWORD version, padding, then function slots.
  var acquire=Marshal.GetDelegateForFunctionPointer<Acquire>(Marshal.ReadIntPtr(table,8+2*8));
  var free=Marshal.GetDelegateForFunctionPointer<Free>(Marshal.ReadIntPtr(table,8+3*8));
  Handle handle; long expiry;
  uint status=acquire(IntPtr.Zero,"Negotiate",1,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,out handle,out expiry);
  Console.WriteLine("AcquireCredentialsHandleW status=0x"+status.ToString("X8"));
  if(status!=0) return;
  var name=new IntPtr(~handle.Upper.ToInt64());
  Console.WriteLine("Credential package interpreted as ANSI="+Marshal.PtrToStringAnsi(name));
  Console.WriteLine("Credential package interpreted as UTF16="+Marshal.PtrToStringUni(name));
  Console.WriteLine("FreeCredentialsHandle status=0x"+free(ref handle).ToString("X8"));
 }
}
'@
[SspiPackageProbe]::Run()
