# collision 模块仅依赖公共核心接口，不包含原生图形 API 或核心内部头文件。
set(CMAKE_FOLDER "modules")
add_library(renderer_collision STATIC src/collision.cpp)
add_library(renderer::collision ALIAS renderer_collision)
target_link_libraries(renderer_collision PUBLIC renderer::core)
target_include_directories(renderer_collision PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_collision PROPERTIES EXPORT_NAME collision)
renderer_warnings(renderer_collision)
