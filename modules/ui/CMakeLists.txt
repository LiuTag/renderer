# RmlUi 与 FreeType 固定版本/哈希，可通过 FETCHCONTENT_SOURCE_DIR_* 指定离线源码。
set(CMAKE_FOLDER "modules")
include(FetchContent)
include("${PROJECT_SOURCE_DIR}/cmake/rendererFreetype.cmake")
renderer_use_freetype()
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS ON CACHE BOOL "" FORCE)
FetchContent_Declare(rmlui
    URL https://codeload.github.com/mikke89/RmlUi/zip/refs/tags/6.3
    URL_HASH SHA256=bfb340660e4bce38d4211cffc836cfbe1be676aa36ef9cc48e7df3855ee74fe3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(rmlui)
set_property(DIRECTORY "${rmlui_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
set_target_properties(rmlui_core PROPERTIES FOLDER "modules" DEBUG_POSTFIX "d")
if(TARGET rmlui_debugger)
    set_target_properties(rmlui_debugger PROPERTIES FOLDER "modules")
endif()
if(MSVC)
    target_compile_options(rmlui_core PRIVATE /utf-8)
endif()
add_library(renderer_ui STATIC src/ui.cpp)
add_library(renderer::ui ALIAS renderer_ui)
target_link_libraries(renderer_ui PUBLIC renderer::core PRIVATE RmlUi::Core)
target_include_directories(renderer_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_ui PROPERTIES EXPORT_NAME ui)
renderer_warnings(renderer_ui)
# 字体只供示例使用；应用可自行加载其他合法字体。SDK 保留相邻许可说明。
set(renderer_UI_FONT "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf" PARENT_SCOPE)
if(RENDERER_INSTALL_SDK)
    install(FILES "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/assets)
    # 许可原文由根工程的 third_party/licenses 规则统一安装。
endif()
