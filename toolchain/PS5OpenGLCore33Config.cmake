# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

get_filename_component(PS5OpenGLCore33_PREFIX
  "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT TARGET PS5OpenGLCore33::OpenGL)
  add_library(PS5OpenGLCore33::OpenGL INTERFACE IMPORTED)
  set_target_properties(PS5OpenGLCore33::OpenGL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PS5OpenGLCore33_PREFIX}/include"
    INTERFACE_COMPILE_DEFINITIONS GL_GLEXT_PROTOTYPES=1
    INTERFACE_LINK_LIBRARIES
      "${PS5OpenGLCore33_PREFIX}/lib/libPS5OpenGLCore33.a;${PS5OpenGLCore33_PREFIX}/lib/libSceAgc.so;${PS5OpenGLCore33_PREFIX}/lib/libSceAgcDriver.so;SceVideoOut;kernel_web;SceSystemService"
    INTERFACE_LINK_OPTIONS "LINKER:-u,ps5_agc_gate2_run")
endif()

set(PS5OpenGLCore33_FOUND TRUE)
