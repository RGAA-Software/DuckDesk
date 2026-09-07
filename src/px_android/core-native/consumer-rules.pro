# JNI registers this class and reads configuration fields/callbacks by exact name.
-keep class yun.pixels.client.core.nativebridge.PixelsNativeBridge {
    native <methods>;
}
-keep class yun.pixels.client.core.nativebridge.NativeSessionConfig { *; }
-keep interface yun.pixels.client.core.nativebridge.NativeSessionListener { *; }
-keepclassmembers class yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport {
    public *** on*(...);
}
