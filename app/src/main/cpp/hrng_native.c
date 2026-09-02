/**
 * hrng_native.c
 * Qualcomm Hardware Random Number Generator (HRNG) Android JNI interface
 *
 * RNG source priority:
 *   1. /dev/hw_random  - Direct Qualcomm HRNG device (root required)
 *   2. getrandom()     - Linux syscall, kernel CSPRNG (HRNG entropy mixed in)
 *   3. /dev/urandom    - File interface to kernel CSPRNG (fallback)
 */

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <linux/random.h>
#include <android/log.h>

#define TAG "QualcommHRNG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef __NR_getrandom
#define __NR_getrandom 278
#endif

static ssize_t getrandom_syscall(void *buf, size_t buflen, unsigned int flags) {
    return syscall(__NR_getrandom, buf, buflen, flags);
}

typedef enum {
    HRNG_SOURCE_HWRNG,
    HRNG_SOURCE_GETRANDOM,
    HRNG_SOURCE_URANDOM,
    HRNG_SOURCE_NONE
} hrng_source_t;

static const char* source_name(hrng_source_t src) {
    switch (src) {
        case HRNG_SOURCE_HWRNG:     return "Qualcomm /dev/hw_random (direct HW access)";
        case HRNG_SOURCE_GETRANDOM: return "getrandom() syscall (kernel CSPRNG, HRNG entropy)";
        case HRNG_SOURCE_URANDOM:   return "/dev/urandom (kernel CSPRNG, HRNG entropy)";
        case HRNG_SOURCE_NONE:      return "No available source";
        default:                    return "Unknown";
    }
}

/*
 * Detect HRNG source. Open directly instead of stat+open to avoid TOCTOU.
 */
static hrng_source_t detect_hrng_source(void) {
    /* Try /dev/hw_random directly */
    int fd = open("/dev/hw_random", O_RDONLY | O_NOCTTY);
    if (fd >= 0) {
        close(fd);
        LOGI("Detected /dev/hw_random (Qualcomm HRNG device)");
        return HRNG_SOURCE_HWRNG;
    }

    /* Try getrandom() syscall */
    unsigned char test_buf[1];
    ssize_t ret = getrandom_syscall(test_buf, 1, 0);
    if (ret == 1) {
        LOGI("getrandom() syscall available (Linux 3.17+)");
        return HRNG_SOURCE_GETRANDOM;
    }

    /* Try /dev/urandom */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        LOGI("Falling back to /dev/urandom");
        return HRNG_SOURCE_URANDOM;
    }

    LOGE("No available random source!");
    return HRNG_SOURCE_NONE;
}

static int read_random_bytes(hrng_source_t source, unsigned char *buf, size_t len) {
    size_t total_read = 0;
    switch (source) {
        case HRNG_SOURCE_HWRNG: {
            int fd = open("/dev/hw_random", O_RDONLY | O_NOCTTY);
            if (fd < 0) { LOGE("Cannot open /dev/hw_random: %s", strerror(errno)); return -1; }
            while (total_read < len) {
                ssize_t n = read(fd, buf + total_read, len - total_read);
                if (n < 0) { if (errno == EINTR) continue; LOGE("Read /dev/hw_random failed: %s", strerror(errno)); close(fd); return -1; }
                if (n == 0) { LOGW("/dev/hw_random returned EOF"); break; }
                total_read += (size_t)n;
            }
            close(fd);
            break;
        }
        case HRNG_SOURCE_GETRANDOM: {
            while (total_read < len) {
                ssize_t n = getrandom_syscall(buf + total_read, len - total_read, GRND_NONBLOCK);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN) { struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 }; nanosleep(&ts, NULL); continue; }
                    LOGE("getrandom() failed: %s", strerror(errno)); return -1;
                }
                total_read += (size_t)n;
            }
            break;
        }
        case HRNG_SOURCE_URANDOM: {
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd < 0) { LOGE("Cannot open /dev/urandom: %s", strerror(errno)); return -1; }
            while (total_read < len) {
                ssize_t n = read(fd, buf + total_read, len - total_read);
                if (n < 0) { if (errno == EINTR) continue; LOGE("Read /dev/urandom failed: %s", strerror(errno)); close(fd); return -1; }
                total_read += (size_t)n;
            }
            close(fd);
            break;
        }
        default: return -1;
    }
    return (total_read == len) ? 0 : -1;
}

static int generate_in_range(hrng_source_t source, uint64_t max, uint64_t *out_value) {
    if (max == 0) { *out_value = 0; return 0; }
    uint64_t range = max + 1;
    uint64_t limit;
    if (range == 0) { limit = UINT64_MAX; }
    else { limit = (UINT64_MAX / range) * range - 1; if (limit == UINT64_MAX) limit = UINT64_MAX; }
    for (int attempts = 0; attempts < 32; attempts++) {
        unsigned char buf[8];
        if (read_random_bytes(source, buf, 8) != 0) { LOGE("Failed to read random bytes"); return -1; }
        uint64_t candidate = 0;
        memcpy(&candidate, buf, 8);
        if (candidate <= limit) { *out_value = candidate % range; return 0; }
    }
    LOGE("Rejection sampling exceeded max attempts");
    return -1;
}

static int generate_range(hrng_source_t source, int64_t min_val, int64_t max_val, int64_t *out_value) {
    if (min_val > max_val) { LOGE("Invalid range: min(%lld) > max(%lld)", (long long)min_val, (long long)max_val); return -1; }
    uint64_t span = (uint64_t)(max_val - min_val);
    uint64_t result = 0;
    if (generate_in_range(source, span, &result) != 0) { return -1; }
    *out_value = min_val + (int64_t)result;
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_qualcomm_hrng_MainActivity_nativeInitHRNG(JNIEnv *env, jobject thiz) {
    (void)thiz;
    hrng_source_t source = detect_hrng_source();
    const char *name = source_name(source);

    /* Sanitized output - no sensitive system info */
    char info[512];
    snprintf(info, sizeof(info),
        "=== Qualcomm HRNG ===\n\n"
        "RNG Source: %s\n\n"
        "=====================\n"
        "Notes:\n"
        "* /dev/hw_random: Direct Qualcomm QSEE HW RNG\n"
        "  Requires root or system signature\n"
        "* getrandom(): Kernel CSPRNG, HRNG entropy mixed in\n"
        "  Works for normal apps, equivalent security\n"
        "* /dev/urandom: Same source as getrandom() (fallback)",
        name);

    LOGI("HRNG init done: %s", name);
    return (*env)->NewStringUTF(env, info);
}

JNIEXPORT jlong JNICALL
Java_com_qualcomm_hrng_MainActivity_nativeGenerateRandom(JNIEnv *env, jobject thiz, jlong minVal, jlong maxVal) {
    (void)env; (void)thiz;
    hrng_source_t source = detect_hrng_source();
    if (source == HRNG_SOURCE_NONE) { LOGE("No source, returning 0"); return 0; }
    int64_t result = 0;
    if (generate_range(source, (int64_t)minVal, (int64_t)maxVal, &result) != 0) { LOGE("Generation failed"); return 0; }
    LOGI("Generated: [%lld, %lld] -> %lld", (long long)minVal, (long long)maxVal, (long long)result);
    return (jlong)result;
}

JNIEXPORT jlongArray JNICALL
Java_com_qualcomm_hrng_MainActivity_nativeGenerateBatch(JNIEnv *env, jobject thiz, jlong minVal, jlong maxVal, jint count) {
    (void)thiz;
    hrng_source_t source = detect_hrng_source();
    jlongArray resultArray = (*env)->NewLongArray(env, count);
    if (resultArray == NULL) return NULL;
    jlong *buf = (*env)->GetLongArrayElements(env, resultArray, NULL);
    if (buf == NULL) return NULL;
    int success_count = 0;
    for (jint i = 0; i < count; i++) {
        int64_t val = 0;
        if (generate_range(source, (int64_t)minVal, (int64_t)maxVal, &val) == 0) { buf[i] = (jlong)val; success_count++; }
        else { buf[i] = 0; }
    }
    (*env)->ReleaseLongArrayElements(env, resultArray, buf, 0);
    LOGI("Batch generated %d/%d random numbers", success_count, count);
    return resultArray;
}

JNIEXPORT jstring JNICALL
Java_com_qualcomm_hrng_MainActivity_nativeReadRawEntropy(JNIEnv *env, jobject thiz, jint byteCount) {
    (void)thiz;
    hrng_source_t source = detect_hrng_source();
    if (source == HRNG_SOURCE_NONE) return (*env)->NewStringUTF(env, "Error: No available source");
    if (byteCount <= 0 || byteCount > 256) byteCount = 32;

    /* Heap buffer instead of stack to avoid stack overflow */
    unsigned char *buf = (unsigned char *)malloc((size_t)byteCount);
    if (buf == NULL) return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    if (read_random_bytes(source, buf, (size_t)byteCount) != 0) { free(buf); return (*env)->NewStringUTF(env, "Error: Failed to read random bytes"); }

    /* Heap-allocated output buffer with bounds checking via snprintf */
    size_t hex_size = (size_t)byteCount * 4 + 512;
    char *hex = (char *)malloc(hex_size);
    if (hex == NULL) { free(buf); return (*env)->NewStringUTF(env, "Error: Memory allocation failed"); }

    char *p = hex;
    size_t remaining = hex_size;
    int written;

    written = snprintf(p, remaining, "Raw Entropy (%d bytes, source: %s):\n", byteCount, source_name(source));
    if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }

    written = snprintf(p, remaining, "+--------------------------------------+\n| ");
    if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }

    for (int i = 0; i < byteCount; i++) {
        written = snprintf(p, remaining, "%02X ", buf[i]);
        if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }
        if ((i + 1) % 16 == 0 && i + 1 < byteCount) {
            written = snprintf(p, remaining, "|\n| ");
            if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }
        }
    }

    int pad = 16 - (byteCount % 16);
    if (pad < 16) {
        for (int i = 0; i < pad; i++) {
            if (remaining > 3) { *p++ = ' '; *p++ = ' '; *p++ = ' '; remaining -= 3; }
        }
    }

    written = snprintf(p, remaining, "|\n+--------------------------------------+\n");
    if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }

    /* Bit distribution */
    int ones[8] = {0};
    for (int i = 0; i < byteCount; i++) { for (int b = 0; b < 8; b++) { if (buf[i] & (1 << b)) ones[b]++; } }

    written = snprintf(p, remaining, "\nBit Distribution:\n");
    if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }

    for (int b = 7; b >= 0; b--) {
        double ratio = (double)ones[b] / (double)(byteCount * 8) * 100.0;
        written = snprintf(p, remaining, "  bit%d: %d/%d (%.1f%%)%s\n", b, ones[b], byteCount * 8, ratio,
                          (ratio > 48.0 && ratio < 52.0) ? " OK" : " WARN");
        if (written > 0 && (size_t)written < remaining) { p += written; remaining -= (size_t)written; }
    }

    jstring result = (*env)->NewStringUTF(env, hex);
    free(buf);
    free(hex);
    return result;
}