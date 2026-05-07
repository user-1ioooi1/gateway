set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(BUILDROOT_HOST
    /linux/IMX6ULL/tool/buildroot-2022.02.4/output/host)

# 编译器前缀
set(CROSS_PREFIX arm-buildroot-linux-gnueabihf)

set(CMAKE_C_COMPILER
    ${BUILDROOT_HOST}/bin/${CROSS_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER
    ${BUILDROOT_HOST}/bin/${CROSS_PREFIX}-g++)
set(CMAKE_STRIP
    ${BUILDROOT_HOST}/bin/${CROSS_PREFIX}-strip)

# sysroot
set(SYSROOT
    ${BUILDROOT_HOST}/arm-buildroot-linux-gnueabihf/sysroot)
set(CMAKE_SYSROOT ${SYSROOT})

set(CMAKE_FIND_ROOT_PATH ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# iMX6ULL Cortex-A7 硬件浮点
set(CMAKE_C_FLAGS
    "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
    CACHE STRING "" FORCE)