# Harness de validación en Linux (no forma parte del APK).
#   cmake -S tests -B tests/build && cmake --build tests/build && ./tests/build/harness
cmake_minimum_required(VERSION 3.16)
project(flower_vulkan_harness)
find_package(Vulkan REQUIRED)
add_executable(harness
    harness.cpp
    ../app/src/main/cpp/renderer_vulkan.cpp
    ../app/src/main/cpp/flower_scene.cpp
    ../app/src/main/cpp/tga.cpp)
target_include_directories(harness PRIVATE ../app/src/main/cpp)
target_link_libraries(harness Vulkan::Vulkan)
target_compile_features(harness PRIVATE cxx_std_17)
