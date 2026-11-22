# CMake del módulo nativo (Vulkan port PSP Flower).
cmake_minimum_required(VERSION 3.22.1)
project(flower_vulkan)

add_library(flower_vulkan SHARED
    main.cpp
    renderer_vulkan.cpp
    flower_scene.cpp
    tga.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(flower_vulkan PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

target_link_libraries(flower_vulkan
    ${log-lib}
    ${android-lib}
    ${vulkan-lib})
