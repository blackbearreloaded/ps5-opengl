set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if (NOT DEFINED ENV{PS5_PAYLOAD_SDK})
    message(FATAL_ERROR "PS5_PAYLOAD_SDK must name the native boilerplate SDK")
endif ()

set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}" CACHE PATH "PS5 payload SDK")
set(CMAKE_C_COMPILER clang-18)
set(CMAKE_CXX_COMPILER clang++-18)
set(CMAKE_C_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_AR "${PS5_PAYLOAD_SDK}/bin/prospero-ar")
set(CMAKE_RANLIB "${PS5_PAYLOAD_SDK}/bin/prospero-ranlib")
set(CMAKE_NM "${PS5_PAYLOAD_SDK}/bin/prospero-nm")
set(CMAKE_OBJCOPY "${PS5_PAYLOAD_SDK}/bin/prospero-objcopy")
set(CMAKE_STRIP "${PS5_PAYLOAD_SDK}/bin/prospero-strip")

set(PS5_COMMON_FLAGS
    "-fvisibility-nodllstorageclass=default -fno-stack-protector -fno-plt -femulated-tls -isysroot ${PS5_PAYLOAD_SDK}")
set(CMAKE_C_FLAGS_INIT
    "${PS5_COMMON_FLAGS} -isystem ${PS5_PAYLOAD_SDK}/target/include")
set(CMAKE_CXX_FLAGS_INIT
    "${PS5_COMMON_FLAGS} -isystem ${PS5_PAYLOAD_SDK}/target/include/c++/v1 -isystem ${PS5_PAYLOAD_SDK}/target/include")

set(CMAKE_FIND_ROOT_PATH "${PS5_PAYLOAD_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(DE_OS DE_OS_UNIX CACHE STRING "drawElements OS")
set(DE_CPU DE_CPU_X86_64 CACHE STRING "drawElements CPU")
set(DE_PTR_SIZE 8 CACHE STRING "drawElements pointer size")
