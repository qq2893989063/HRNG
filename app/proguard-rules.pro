# Qualcomm HRNG ProGuard rules
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.qualcomm.hrng.** { *; }