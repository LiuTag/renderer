# 固定 Box2D 版本，源码可用 FETCHCONTENT_SOURCE_DIR_BOX2D 离线覆盖。
set(CMAKE_FOLDER "modules")
include(FetchContent)
set(BUILD_SHARED_LIBS OFF)
set(BOX2D_DOUBLE_PRECISION OFF CACHE BOOL "Use float positions in the renderer physics module" FORCE)
set(BOX2D_PROFILE OFF CACHE BOOL "Disable external profiler" FORCE)
FetchContent_Declare(box2d
    URL https://codeload.github.com/erincatto/box2d/zip/refs/tags/v3.2.0
    URL_HASH SHA256=ed94013e8d16b19876757920bfa6d7663358aef8ba9407cc18146da6b714029e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(box2d)
# SDK 使用统一导出与配置目录，不执行上游的头文件/pkg-config/独立包安装。
set_property(DIRECTORY "${box2d_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
set_target_properties(box2d PROPERTIES FOLDER "modules" DEBUG_POSTFIX "d")
if(MSVC)
    target_compile_options(box2d PRIVATE /utf-8)
endif()
add_library(renderer_physics2d STATIC src/physics2d.cpp)
add_library(renderer::physics2d ALIAS renderer_physics2d)
target_link_libraries(renderer_physics2d PUBLIC renderer::core PRIVATE box2d)
target_include_directories(renderer_physics2d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_physics2d PROPERTIES EXPORT_NAME physics2d)
renderer_warnings(renderer_physics2d)
