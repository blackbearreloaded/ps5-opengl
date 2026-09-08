# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

message("*** Using PS5 OpenGL context target")

set(DEQP_TARGET_NAME "PS5 OpenGL")
set(DEQP_SUPPORT_GLES1 OFF)
set(DEQP_GLES1_LIBRARIES)
set(DEQP_GLES2_LIBRARIES)
set(DEQP_GLES3_LIBRARIES)
set(DEQP_GLES31_LIBRARIES)
set(DEQP_GLES32_LIBRARIES)
set(DEQP_EGL_LIBRARIES)
set(DEQP_PLATFORM_LIBRARIES)

set(TCUTIL_PLATFORM_SRCS
    ps5/tcuPS5Platform.cpp
    ps5/tcuPS5Platform.hpp
)

add_compile_definitions(DEQP_SURFACELESS=1)
add_compile_options(
    -include malloc_np.h
    -Wno-unreachable-code-generic-assoc
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
)
