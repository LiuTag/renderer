# tilemap 模块仅依赖公共核心接口，不包含原生图形 API 或核心内部头文件。
set(CMAKE_FOLDER "modules")
add_library(renderer_tilemap STATIC src/tilemap.cpp src/streaming.cpp src/chunk_file.cpp)
add_library(renderer::tilemap ALIAS renderer_tilemap)
target_link_libraries(renderer_tilemap PUBLIC renderer::core)
target_include_directories(renderer_tilemap PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_tilemap PROPERTIES EXPORT_NAME tilemap)
renderer_warnings(renderer_tilemap)
