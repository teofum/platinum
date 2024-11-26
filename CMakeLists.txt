cmake_minimum_required(VERSION 3.29)
project(platinum VERSION 0.1.0)

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
                OUTPUT ${SHADER_AIR}
                COMMAND xcrun -sdk macosx metal -o ${SHADER_AIR} -c ${SHADER_SRC}
                DEPENDS ${SHADER_SRC}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    endforeach ()

    message(STATUS "  Building: ${SHADER_AIRS}")
    add_custom_command(
            OUTPUT ${LIB_NAME} ___
            COMMAND xcrun -sdk macosx metallib -o ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME} ${SHADER_AIRS}
            DEPENDS ${SHADER_AIRS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Set compiler flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "-Wall -fno-objc-arc")

# Build metal-cpp library
add_subdirectory(metal-cmake)

# SDL2
set(SDL2 "${CMAKE_SOURCE_DIR}/sdl2/SDL2.framework")

# ImGui
file(GLOB IMGUI_SOURCES imgui/*.cpp)
add_library(ImGui STATIC
        ${IMGUI_SOURCES}
        imgui/backends/imgui_impl_sdl2.cpp
        imgui/backends/imgui_impl_metal.mm
        imgui/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(ImGui PUBLIC imgui)
target_include_directories(ImGui PRIVATE ${SDL2}/Headers)

target_link_libraries(ImGui PRIVATE METAL_CPP)
target_link_libraries(ImGui PRIVATE ${SDL2}/SDL2)

# Build shaders
build_shaders(renderer_studio.metallib
        src/renderer_studio/shaders/main.metal
        src/renderer_studio/shaders/grid.metal
        src/renderer_studio/shaders/edge_pass.metal
)

# Main executable
add_executable(platinum
        src/main.cpp
        src/core/mesh.cpp
        src/core/scene.cpp
        src/core/store.cpp
        src/core/primitives.cpp
        src/utils/metal_utils.mm
        src/utils/matrices.cpp
        src/utils/utils.cpp
        src/frontend/frontend.cpp
        src/renderer_studio/renderer_studio.cpp
        src/renderer_studio/camera.cpp
        renderer_studio.metallib
)

target_include_directories(platinum PRIVATE src)
target_include_directories(platinum PRIVATE ${SDL2}/Headers)
target_include_directories(platinum PRIVATE ankerl)

target_link_libraries(platinum PRIVATE METAL_CPP)
target_link_libraries(platinum PRIVATE ${SDL2}/SDL2)
target_link_libraries(platinum PRIVATE ImGui)

# metal-cpp

