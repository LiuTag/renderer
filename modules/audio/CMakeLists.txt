# 音频只依赖 core 的公共类型；miniaudio 实现编入同一个静态库。
set(CMAKE_FOLDER "modules")
find_package(Threads REQUIRED)
add_library(renderer_audio_miniaudio OBJECT src/miniaudio.c)
target_include_directories(renderer_audio_miniaudio PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniaudio")
add_library(renderer_audio STATIC src/audio.cpp $<TARGET_OBJECTS:renderer_audio_miniaudio>)
add_library(renderer::audio ALIAS renderer_audio)
target_link_libraries(renderer_audio PUBLIC renderer::core PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(renderer_audio PRIVATE m)
endif()
target_include_directories(renderer_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(renderer_audio SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniaudio")
set_target_properties(renderer_audio PROPERTIES EXPORT_NAME audio)
renderer_warnings(renderer_audio)
# 上游源码保持原样，其内部实现不应用项目的额外告警策略。
if(MSVC)
    target_compile_options(renderer_audio_miniaudio PRIVATE /W0)
else()
    target_compile_options(renderer_audio_miniaudio PRIVATE -w)
endif()
