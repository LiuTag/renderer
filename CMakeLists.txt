cmake_minimum_required(VERSION 3.25)
project(renderer VERSION 0.2.0 LANGUAGES C CXX)
# Windows 默认 SDK 安装到构建目录，避免 VS INSTALL 写入受保护的 Program Files。
# 仅替换 CMake 的首次默认值，保留用户通过 -D/环境变量显式指定的安装前缀。
if(WIN32 AND PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/sdk" CACHE PATH "Renderer SDK install directory" FORCE)
endif()
# 在 Visual Studio 等 IDE 中显示目标的逻辑文件夹。
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    URL https://codeload.github.com/KhronosGroup/glslang/zip/refs/tags/15.4.0
    URL_HASH SHA256=3d79df973f99af75c99e071989e94ee052db23fcf5909028eece9aee9157502b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(spirv_cross
    URL https://codeload.github.com/KhronosGroup/SPIRV-Cross/zip/refs/tags/vulkan-sdk-1.4.321.0
    URL_HASH SHA256=8971ebbce4ae9de0fd5dcc839c1d74f19b22476b9808d7d2f8a6d6b982a65671
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(glslang spirv_cross)
set_target_properties(spirv-cross-core spirv-cross-glsl PROPERTIES CXX_STANDARD 17)

# 旧名称仅为首次迁移提供默认值；显式设置的新开关始终优先。
if(DEFINED RENDERER_BUILD_DEMO AND NOT DEFINED RENDERER_BUILD_EXAMPLES)
    set(RENDERER_BUILD_EXAMPLES "${RENDERER_BUILD_DEMO}" CACHE BOOL "Build source examples")
endif()
option(RENDERER_BUILD_EXAMPLES "Build source examples" ON)
option(RENDERER_BUILD_TESTS "Build tests and register CTest checks" ON)
option(RENDERER_BUILD_OPENGL "Build the OpenGL backend" ON)
option(RENDERER_BUILD_VULKAN "Build the Vulkan 1.3 backend (requires Vulkan headers)" OFF)
option(RENDERER_BUILD_ANIMATION "Build the optional sprite animation module" ON)
option(RENDERER_BUILD_INTERACTION "Build the optional scene interaction module" ON)
option(RENDERER_BUILD_AUDIO "Build the optional miniaudio music and sound module" ON)
option(RENDERER_BUILD_TILEMAP "Build the optional tilemap module" ON)
option(RENDERER_BUILD_COLLISION "Build the optional collision module" ON)
option(RENDERER_BUILD_PARTICLES "Build the optional particles module" ON)
option(RENDERER_BUILD_ENVIRONMENT "Build the optional environment module" ON)
option(RENDERER_BUILD_PRESENTATION "Build the optional presentation module" ON)
option(RENDERER_BUILD_PHYSICS2D "Build the optional Box2D physics module" ON)
option(RENDERER_GL_TESTS "Include OpenGL tests when RENDERER_BUILD_TESTS is enabled" OFF)
option(RENDERER_VULKAN_TESTS "Include Vulkan GPU and presentation tests when tests are enabled" OFF)
option(RENDERER_INSTALL_SDK "Install public headers, libraries and CMake package" ON)

add_library(renderer_core STATIC
    src/events.cpp
    src/math.cpp
    src/assets.cpp
    src/image.cpp
    src/sprite.cpp
    src/sprite_batch.cpp
    src/sprite_snapshot.cpp
    src/model.cpp
    src/scene.cpp
    src/renderer.cpp
    src/frame_builder.cpp
    src/shader.cpp
    src/rhi.cpp
    src/rhi_commands.cpp
    src/render_graph.cpp)
add_library(renderer::core ALIAS renderer_core)
target_include_directories(renderer_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(renderer_core PROPERTIES EXPORT_NAME core FOLDER "core")
target_include_directories(renderer_core SYSTEM PRIVATE third_party/tinygltf)
target_compile_features(renderer_core PUBLIC cxx_std_20)
target_link_libraries(renderer_core PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits spirv-cross-glsl)

function(renderer_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
renderer_warnings(renderer_core)

if(RENDERER_BUILD_ANIMATION)
    add_subdirectory(modules/animation)
endif()
if(RENDERER_BUILD_INTERACTION)
    add_subdirectory(modules/interaction)
endif()
if(RENDERER_BUILD_AUDIO)
    add_subdirectory(modules/audio)
endif()

foreach(module IN ITEMS tilemap collision particles environment presentation physics2d)
    string(TOUPPER "${module}" module_upper)
    if(RENDERER_BUILD_${module_upper})
        add_subdirectory(modules/${module})
    endif()
endforeach()

if(RENDERER_BUILD_OPENGL OR RENDERER_BUILD_VULKAN)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        URL https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4
        URL_HASH SHA256=a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(glfw)

    add_library(renderer_platform STATIC src/platform/window.cpp)
    target_link_libraries(renderer_platform PUBLIC renderer_core PRIVATE glfw)
    target_compile_definitions(renderer_platform PRIVATE GLFW_INCLUDE_NONE)
    set_target_properties(renderer_platform PROPERTIES FOLDER "core")
    renderer_warnings(renderer_platform)
    add_library(renderer_pipeline STATIC src/backends/common/renderer_backend.cpp)
    target_link_libraries(renderer_pipeline PUBLIC renderer_core PRIVATE renderer_platform glfw)
    target_compile_definitions(renderer_pipeline PRIVATE GLFW_INCLUDE_NONE)
    set_target_properties(renderer_pipeline PROPERTIES FOLDER "core")
    renderer_warnings(renderer_pipeline)
endif()

if(RENDERER_BUILD_OPENGL)

    add_library(renderer_glad STATIC third_party/glad/src/gl.c)
    set_target_properties(renderer_glad PROPERTIES FOLDER "core")
    target_include_directories(renderer_glad SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include>)
    add_library(renderer_opengl STATIC src/backends/opengl/renderer_backend.cpp src/backends/opengl/rhi_device.cpp src/backends/opengl/offscreen_context.cpp)
    add_library(renderer::opengl ALIAS renderer_opengl)
    set_target_properties(renderer_opengl PROPERTIES EXPORT_NAME opengl FOLDER "core")
    target_link_libraries(renderer_opengl PUBLIC renderer_core PRIVATE renderer_pipeline renderer_platform glfw renderer_glad)
    if(WIN32)
        target_link_libraries(renderer_opengl PRIVATE opengl32 gdi32 user32)
    endif()
    target_compile_definitions(renderer_opengl PRIVATE GLFW_INCLUDE_NONE)
    renderer_warnings(renderer_opengl)

endif()

if(RENDERER_BUILD_VULKAN)
    find_package(Vulkan 1.3 REQUIRED)
    add_library(renderer_volk OBJECT third_party/volk/volk.c)
    target_include_directories(renderer_volk PRIVATE ${Vulkan_INCLUDE_DIRS} third_party/volk)
    set_target_properties(renderer_volk PROPERTIES FOLDER "core")
    add_library(renderer_vulkan STATIC src/backends/vulkan/rhi_device.cpp
        src/backends/vulkan/renderer_backend.cpp $<TARGET_OBJECTS:renderer_volk>)
    add_library(renderer::vulkan ALIAS renderer_vulkan)
    set_target_properties(renderer_vulkan PROPERTIES EXPORT_NAME vulkan FOLDER "core")
    target_include_directories(renderer_vulkan SYSTEM PRIVATE ${Vulkan_INCLUDE_DIRS} third_party/volk)
    target_compile_definitions(renderer_vulkan PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(renderer_vulkan PUBLIC renderer_core PRIVATE renderer_pipeline renderer_platform glfw ${CMAKE_DL_LIBS})
    renderer_warnings(renderer_vulkan)
endif()

if(RENDERER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(RENDERER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
else()
    # 清空旧清单，避免重新配置关闭测试后 CTest 仍运行上一配置的测试。
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake" "# Renderer tests are disabled.\n")
endif()

if(RENDERER_INSTALL_SDK)
    # 静态库的私有链接依赖仍需随 SDK 提供，使用内部名称避免污染使用者命名空间。
    set(renderer_sdk_targets renderer_core)
    foreach(module IN ITEMS animation interaction audio tilemap collision particles environment presentation physics2d)
        if(TARGET renderer_${module})
            list(APPEND renderer_sdk_targets renderer_${module})
            install(DIRECTORY modules/${module}/include/renderer DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                FILES_MATCHING PATTERN "*.hpp")
        endif()
    endforeach()
    set(renderer_sdk_dependencies glslang SPIRV glslang-default-resource-limits
        spirv-cross-glsl spirv-cross-core)
    if(RENDERER_BUILD_PHYSICS2D)
        list(APPEND renderer_sdk_dependencies box2d)
    endif()
    if(RENDERER_BUILD_OPENGL OR RENDERER_BUILD_VULKAN)
        list(APPEND renderer_sdk_dependencies renderer_platform renderer_pipeline glfw)
    endif()
    if(RENDERER_BUILD_VULKAN)
        list(APPEND renderer_sdk_targets renderer_vulkan)
    endif()
    if(RENDERER_BUILD_OPENGL)
        list(APPEND renderer_sdk_targets renderer_opengl)
        list(APPEND renderer_sdk_dependencies renderer_glad)
    endif()
    foreach(dependency IN LISTS renderer_sdk_dependencies)
        set_target_properties(${dependency} PROPERTIES EXPORT_NAME _${dependency})
        # 依赖的头文件只在构建引擎时使用；SDK 的公共头文件不包含这些头文件。
        get_target_property(dependency_includes ${dependency} INTERFACE_INCLUDE_DIRECTORIES)
        if(dependency_includes)
            set_property(TARGET ${dependency} PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                "$<BUILD_INTERFACE:${dependency_includes}>")
        endif()
    endforeach()
    install(TARGETS ${renderer_sdk_targets} ${renderer_sdk_dependencies}
        EXPORT rendererTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<CONFIG>
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<CONFIG>
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/$<CONFIG>)
    install(DIRECTORY include/renderer DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp")
    set(renderer_package_dir ${CMAKE_INSTALL_LIBDIR}/cmake/renderer)
    configure_package_config_file(cmake/rendererConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/rendererConfig.cmake
        INSTALL_DESTINATION ${renderer_package_dir}
        PATH_VARS CMAKE_INSTALL_DATADIR)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/rendererConfigVersion.cmake
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(EXPORT rendererTargets NAMESPACE renderer:: DESTINATION ${renderer_package_dir})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rendererConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/rendererConfigVersion.cmake
        DESTINATION ${renderer_package_dir})
    install(FILES README.md IMPLEMENTATION.md THIRD_PARTY.md CHANGELOG.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer)
    install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/docs
        FILES_MATCHING PATTERN "*.md")
    install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/examples
        FILES_MATCHING PATTERN "CMakeLists.txt" PATTERN "*.cpp" PATTERN "*.hpp" PATTERN "*.md")
    install(FILES tests/example_tests.cmake tests/README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/tests)
    install(DIRECTORY third_party/licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/renderer/third_party/licenses)
endif()
