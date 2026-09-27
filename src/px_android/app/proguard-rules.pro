# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Preserve useful release crash locations while keeping source file names private.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# The Play Services code scanner is created on demand. Its ML Kit component
# factories must retain their identities after R8 optimizes the release APK.
-keep class com.google.mlkit.** { *; }
-keep class com.google.android.gms.internal.mlkit_code_scanner.** { *; }
-keep class com.google.android.gms.internal.mlkit_common.** { *; }
