cmake_minimum_required(VERSION 3.29)
project(platinum VERSION 0.1.0)

include(FetchContent)

# Build shaders using external tools
function(build_shaders LIB_NAME)
    message(STATUS "Building shader lib: ${LIB_NAME}")

    set(SHADER_AIRS "")
    foreach (SHADER_PATH IN LISTS ARGN)
        get_filename_component(SHADER_FILENAME ${SHADER_PATH} NAME_WLE)
        message(STATUS "  Compiling: ${SHADER_FILENAME}")

        set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_PATH}")
        get_filename_component(LIB_NAME_WLE ${LIB_NAME} NAME_WLE)
        set(SHADER_AIR "${LIB_NAME_WLE}_${SHADER_FILENAME}.air")

        list(APPEND SHADER_AIRS ${SHADER_AIR})
        add_custom_command(
                TARGET platinum POST_BUILD
                COMMAND xcrun -sdk macosx metal -o ${SHADER_AIR} -c ${SHADER_SRC} -I "${CMAKE_CURRENT_SOURCE_DIR}/src"
                DEPENDS ${SHADER_SRC}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    endforeach ()

    message(STATUS "  Building: ${SHADER_AIRS}")
    add_custom_command(
            TARGET platinum POST_BUILD
            COMMAND xcrun -sdk macosx metallib -o ${LIB_NAME} ${SHADER_AIRS}
            DEPENDS ${SHADER_AIRS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Set compiler flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "-Wall -fno-objc-arc")

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build metal-cpp library
add_subdirectory(deps/metal-cmake)

# SDL2
set(SDL2 "${CMAKE_SOURCE_DIR}/deps/sdl2/SDL2.framework")

# fastgltf
set(fastgltf "${CMAKE_SOURCE_DIR}/deps/fastgltf/lib")

# Native file dialog
set(NFD "${CMAKE_SOURCE_DIR}/deps/nfd")

# ImGui
file(GLOB IMGUI_SOURCES deps/imgui/*.cpp)
add_library(ImGui STATIC
        ${IMGUI_SOURCES}
        deps/imgui/backends/imgui_impl_sdl2.cpp
        deps/imgui/backends/imgui_impl_metal.mm
        deps/imgui/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(ImGui PUBLIC deps/imgui)
target_include_directories(ImGui PRIVATE ${SDL2}/Headers)

target_link_libraries(ImGui PRIVATE METAL_CPP)
target_link_libraries(ImGui PRIVATE ${SDL2}/SDL2)

# ImPlot
file(GLOB IMPLOT_SOURCES deps/implot/*.cpp)
add_library(ImPlot STATIC
        ${IMPLOT_SOURCES}
)

target_include_directories(ImPlot PUBLIC deps/implot)
target_link_libraries(ImPlot PRIVATE ImGui)

# mikktspace
add_library(mikktspace STATIC
        deps/mikkt/mikktspace.c
)

target_include_directories(mikktspace PUBLIC deps/mikkt)

# miniz
add_library(miniz STATIC
        deps/miniz/miniz.c
)

target_include_directories(miniz PUBLIC deps/miniz)

# tinyexr
add_library(tinyexr STATIC
        deps/tinyexr/tinyexr.cpp
)

target_include_directories(tinyexr PUBLIC deps/tinyexr)
target_link_libraries(tinyexr PRIVATE miniz)

# Main executable
file(GLOB_RECURSE PLATINUM_SOURCES src/*.cpp)
add_executable(platinum
        ${PLATINUM_SOURCES}
        src/utils/cocoa_utils.mm
        src/utils/metal_utils.mm
        # renderer_studio.metallib
        # renderer_pt.metallib
        # tools.metallib
        # loaders.metallib
        # viewport.metallib
)

target_include_directories(platinum PRIVATE src)
target_include_directories(platinum PRIVATE ${SDL2}/Headers)
target_include_directories(platinum PRIVATE deps/ankerl)
target_include_directories(platinum PRIVATE deps/fastgltf/include)
target_include_directories(platinum PRIVATE deps/nfd)
target_include_directories(platinum PRIVATE deps/entt)
target_include_directories(platinum PRIVATE deps/json)

target_link_libraries(platinum PRIVATE METAL_CPP)
target_link_libraries(platinum PRIVATE ${SDL2}/SDL2)
target_link_libraries(platinum PRIVATE ImGui)
target_link_libraries(platinum PRIVATE ImPlot)
target_link_libraries(platinum PRIVATE mikktspace)
target_link_libraries(platinum PRIVATE tinyexr)
target_link_libraries(platinum PRIVATE ${fastgltf}/libfastgltf)
target_link_libraries(platinum PRIVATE ${NFD}/libnfd)

# Build shaders
build_shaders(renderer_studio.metallib
        src/renderer_studio/shaders/main.metal
        src/renderer_studio/shaders/camera.metal
        src/renderer_studio/shaders/grid.metal
        src/renderer_studio/shaders/edge_pass.metal
)

build_shaders(renderer_pt.metallib
        src/renderer_pt/shaders/bsdf.metal
        src/renderer_pt/shaders/intersections.metal
        src/renderer_pt/shaders/samplers.metal
        src/renderer_pt/shaders/kernel.metal
        src/renderer_pt/shaders/postprocess.metal
        src/renderer_pt/shaders/gmon.metal
)

build_shaders(tools.metallib
        src/frontend/windows/tools/shaders/ms_lut_gen.metal
)

build_shaders(loaders.metallib
        src/loaders/texture_converter.metal
)

build_shaders(viewport.metallib
        src/renderer_pt/shaders/viewport.metal
)

# Copy resource files
add_custom_command(
        TARGET platinum POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resource
        ${CMAKE_CURRENT_BINARY_DIR}/resource
)

# #test
# file(GLOB_RECURSE METAL_SOURCES src/*.metal)
# add_library(metal_dummy
#         src/main.cpp
#         ${METAL_SOURCES}
# )
#
# #set_target_properties(metal_dummy PROPERTIES LINKER_LANGUAGE CXX)
# target_include_directories(metal_dummy PRIVATE /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/metal/32023/lib/clang/32023.404/include/metal)
# target_compile_definitions(metal_dummy PUBLIC device= constant= thread= ray_data= object_data= __METAL__ __HAVE_RAYTRACING__ __HAVE_RAYTRACING_MULTI_LEVEL_INSTANCING__ __HAVE_RAYTRACING_INTERSECTION_QUERY__ __HAVE_RAYTRACING_USER_INSTANCE_ID__ __HAVE_RAYTRACING_MOTION__ __HAVE_RAYTRACING_INDIRECT_INSTANCE_BUILD__ __HAVE_RAYTRACING_CURVES__ __HAVE_INDIRECT_ARGUMENT_BUFFER__ __HAVE_MESH__ __METAL_VERSION__= METAL_FUNC=)
