# 动画扩展只依赖核心公共接口，不允许包含 src/internal 或绑定 OpenGL。
set(CMAKE_FOLDER "modules")
add_library(renderer_animation STATIC src/sprite_animation.cpp)
add_library(renderer::animation ALIAS renderer_animation)
target_link_libraries(renderer_animation PUBLIC renderer::core)
target_include_directories(renderer_animation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_animation PROPERTIES EXPORT_NAME animation)
renderer_warnings(renderer_animation)
