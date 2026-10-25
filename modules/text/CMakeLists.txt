# 文字只依赖 core 公共接口和共用的 FreeType，不要求 UI/RmlUi 或图形后端。
set(CMAKE_FOLDER "modules")
include("${PROJECT_SOURCE_DIR}/cmake/rendererFreetype.cmake")
renderer_use_freetype()
add_library(renderer_text STATIC src/text.cpp)
add_library(renderer::text ALIAS renderer_text)
target_link_libraries(renderer_text PUBLIC renderer::core PRIVATE Freetype::Freetype)
target_include_directories(renderer_text PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_text PROPERTIES EXPORT_NAME text)
renderer_warnings(renderer_text)
if(DEFINED renderer_UI_FONT)
    set(renderer_TEXT_FONT "${renderer_UI_FONT}")
else()
    include(FetchContent)
    FetchContent_Declare(renderer_demo_font
        URL https://raw.githubusercontent.com/mikke89/RmlUi/6.3/Samples/assets/LatoLatin-Regular.ttf
        URL_HASH SHA256=d785334ac4e7810f571def986bbad41161f68ac385db8813f798bf04d71478e1
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(renderer_demo_font)
    set(renderer_TEXT_FONT "${renderer_demo_font_SOURCE_DIR}/LatoLatin-Regular.ttf")
    if(RENDERER_INSTALL_SDK)
        install(FILES "${renderer_TEXT_FONT}" DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/assets)
    endif()
endif()
set(renderer_TEXT_FONT "${renderer_TEXT_FONT}" PARENT_SCOPE)
