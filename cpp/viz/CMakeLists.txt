cmake_minimum_required(VERSION 3.16)
project(viz CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler flags — applied to all targets
# -fno-exceptions: per spec; GLFW/ImGui failures call std::terminate directly
# -Wconversion / -Wsign-conversion / -Werror: no-cast rule enforcement
# Third-party sources are compiled with reduced warnings (see below)
# ---------------------------------------------------------------------------
add_compile_options(
    -O2
    -march=native
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Werror
    -fno-exceptions
)

# ---------------------------------------------------------------------------
# System dependencies
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# ---------------------------------------------------------------------------
# Third-party source lists
# These are compiled with warnings suppressed: they are not our code and they
# do not obey -Wconversion / -Wsign-conversion / -Werror.
# ---------------------------------------------------------------------------

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)

set(IMPLOT_SOURCES
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

# Build third-party code as a static library with warnings disabled.
# This prevents third-party -Wconversion violations from failing our build.
add_library(third_party_imgui STATIC
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
)
target_include_directories(third_party_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMPLOT_DIR}
)
# Suppress all warnings for third-party code
target_compile_options(third_party_imgui PRIVATE
    -w
)
# third_party_imgui needs GLFW and OpenGL headers
target_link_libraries(third_party_imgui PUBLIC
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Main executable: viz
#
# Iteration 1: only src/main.cpp.  Later iterations add model/*.cpp and
# view/*.cpp sources here.
# ---------------------------------------------------------------------------

add_executable(viz
    src/main.cpp
)

target_include_directories(viz PRIVATE
    ${IMGUI_DIR}
    ${IMPLOT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view
    ${CMAKE_CURRENT_SOURCE_DIR}/../eth/cpp
)

target_link_libraries(viz PRIVATE
    third_party_imgui
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS}
    pthread
)

# ---------------------------------------------------------------------------
# Test targets
# Test builds: no -flto, no sanitizers by default (per Idiom 9).
# Add -fsanitize=undefined,address by passing -DSANITIZE=ON to cmake.
# ---------------------------------------------------------------------------

enable_testing()

# test_structs: compile-time layout verification
add_executable(test_structs
    test/test_structs.cpp
)

target_include_directories(test_structs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

# test_structs has no third-party or book dependencies — no extra link needed.

if(SANITIZE)
    target_compile_options(test_structs PRIVATE -fsanitize=undefined,address)
    target_link_options(test_structs PRIVATE -fsanitize=undefined,address)
endif()

add_test(NAME test_structs COMMAND test_structs)

# ---------------------------------------------------------------------------
# CTest configuration
# ---------------------------------------------------------------------------
# Run tests from the build directory
set_tests_properties(test_structs PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS"
)
