# particles 模块仅依赖公共核心接口，不包含原生图形 API 或核心内部头文件。
set(CMAKE_FOLDER "modules")
add_library(renderer_particles STATIC src/particles.cpp)
add_library(renderer::particles ALIAS renderer_particles)
target_link_libraries(renderer_particles PUBLIC renderer::core)
target_include_directories(renderer_particles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_particles PROPERTIES EXPORT_NAME particles)
renderer_warnings(renderer_particles)
