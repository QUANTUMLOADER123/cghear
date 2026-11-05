LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

RAYLIB_SRC := $(LOCAL_PATH)/../build/raylib-5.5/src

LOCAL_SRC_FILES := \
    ../src/main.cpp \
    $(RAYLIB_SRC)/rcore.c \
    $(RAYLIB_SRC)/rshapes.c \
    $(RAYLIB_SRC)/rtextures.c \
    $(RAYLIB_SRC)/rtext.c \
    $(RAYLIB_SRC)/rmodels.c \
    $(RAYLIB_SRC)/raudio.c \
    $(RAYLIB_SRC)/utils.c

LOCAL_C_INCLUDES := $(RAYLIB_SRC)

LOCAL_CFLAGS   := -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -O2
LOCAL_CPPFLAGS := -std=c++17 -frtti -fexceptions -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -O2

LOCAL_LDLIBS := -lEGL -lGLESv2 -lOpenSLES -landroid -llog
LOCAL_STATIC_LIBRARIES := android_native_app_glue

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
