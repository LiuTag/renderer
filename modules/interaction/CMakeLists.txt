# 交互扩展通过只读 SpriteViewSnapshot 查询，不读取核心私有数据，也不依赖动画扩展。
set(CMAKE_FOLDER "modules")
add_library(renderer_interaction STATIC src/picking.cpp)
add_library(renderer::interaction ALIAS renderer_interaction)
target_link_libraries(renderer_interaction PUBLIC renderer::core)
target_include_directories(renderer_interaction PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_interaction PROPERTIES EXPORT_NAME interaction)
renderer_warnings(renderer_interaction)
