cmake_minimum_required(VERSION 3.10)
project(8085_clock_visualizer C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define include directories so the compiler finds your headers
include_directories(
    include
    vendor/imgui
    vendor/glad
)

# Find system OpenGL libraries
find_package(OpenGL REQUIRED)

# List all source files
set(SOURCES
    src/main.cpp
    src/emulator_core.c
    vendor/glad/glad.c
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/imgui_impl_glfw.cpp
    vendor/imgui/imgui_impl_opengl3.cpp
)

# Create the executable
add_executable(8085_clock ${SOURCES})

# Link GLFW, OpenGL, and dynamic linking loader (dl)
target_link_libraries(8085_clock PRIVATE glfw ${OPENGL_LIBRARIES} dl)
