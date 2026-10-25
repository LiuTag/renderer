# 相机控制只依赖 core 公共接口，既可驱动窗口场景，也可用于离屏/CPU 工具。
set(CMAKE_FOLDER "modules")
add_library(renderer_camera STATIC src/camera_controller.cpp)
add_library(renderer::camera ALIAS renderer_camera)
target_link_libraries(renderer_camera PUBLIC renderer::core)
target_include_directories(renderer_camera PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_camera PROPERTIES EXPORT_NAME camera)
renderer_warnings(renderer_camera)
