cmake_minimum_required(VERSION 3.22.1)

project("qualcomm_hrng" C)

add_library(
    qualcomm_hrng
    SHARED
    hrng_native.c
)

# 链接 Android 日志库
find_library(log-lib log)
target_link_libraries(qualcomm_hrng ${log-lib})

# ARM64 优化编译选项
if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    target_compile_options(qualcomm_hrng PRIVATE
        -O2
        -march=armv8-a
        -DANDROID
    )
endif()

target_compile_options(qualcomm_hrng PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
