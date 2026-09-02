# Qualcomm HRNG (Hardware Random Number Generator)

Android app that calls Qualcomm's hardware random number generator to generate random numbers in a specified range. ARM V8A architecture.

## Features

- Direct access to /dev/hw_random (Qualcomm QSEE HRNG, requires root)
- getrandom() syscall (kernel CSPRNG, HRNG entropy mixed in, works without root)
- /dev/urandom fallback
- Rejection sampling for uniform distribution (no modulo bias)
- Single and batch generation with frequency analysis
- Raw entropy visualization with bit distribution statistics

## Architecture

- **ARM64-V8A (AArch64)** native C library via JNI
- Kotlin UI with ViewBinding and coroutines
- Material Design dark theme
- Min SDK: 24 (Android 7.0) / Target SDK: 35

## RNG Source Priority

1. /dev/hw_random - Direct Qualcomm HRNG device (root required)
2. getrandom() - Linux syscall, kernel CSPRNG (HRNG entropy mixed in)
3. /dev/urandom - File interface to kernel CSPRNG (fallback)

## Build

`ash
# Requires: JDK 17, Android SDK, NDK, CMake
./gradlew assembleDebug
`

Output: pp/build/outputs/apk/debug/app-debug.apk

## Security

- All sprintf calls use snprintf with bounds checking
- TOCTOU race conditions eliminated (direct open instead of stat+open)
- No sensitive system information leakage (SOC/kernel version removed)
- Heap-allocated buffers for variable-size data
- llowBackup=false in manifest
- ProGuard rules protect JNI native methods

## License

MIT