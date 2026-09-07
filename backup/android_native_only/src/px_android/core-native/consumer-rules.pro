# Invoked by name from JavaSessionCallback::MediaUnavailable, not from Java/Kotlin.
-keepclassmembers class yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport {
    public void onMediaUnavailable(java.lang.String, boolean);
}
