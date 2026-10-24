# presentation 模块仅依赖公共核心接口，不包含原生图形 API 或核心内部头文件。
set(CMAKE_FOLDER "modules")
add_library(renderer_presentation STATIC src/presentation.cpp)
add_library(renderer::presentation ALIAS renderer_presentation)
target_link_libraries(renderer_presentation PUBLIC renderer::core)
target_include_directories(renderer_presentation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_presentation PROPERTIES EXPORT_NAME presentation)
renderer_warnings(renderer_presentation)
