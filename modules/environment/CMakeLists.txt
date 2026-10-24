# environment 模块仅依赖公共核心接口，不包含原生图形 API 或核心内部头文件。
set(CMAKE_FOLDER "modules")
add_library(renderer_environment STATIC src/environment.cpp)
add_library(renderer::environment ALIAS renderer_environment)
target_link_libraries(renderer_environment PUBLIC renderer::core)
target_include_directories(renderer_environment PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_environment PROPERTIES EXPORT_NAME environment)
renderer_warnings(renderer_environment)
