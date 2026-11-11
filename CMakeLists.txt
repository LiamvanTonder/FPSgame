cmake_minimum_required(VERSION 3.20)
project(RangeFPS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(glm  GIT_REPOSITORY https://github.com/g-truc/glm.git  GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw glm)

# Vendored GL 3.3 core loader (generated with glad 0.1.36)
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(fps ${GAME_SOURCES})
target_include_directories(fps PRIVATE src external)
target_link_libraries(fps PRIVATE glfw glm::glm glad)
target_compile_definitions(fps PRIVATE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL _USE_MATH_DEFINES NOMINMAX)

if(MSVC)
    target_compile_options(fps PRIVATE /W3 /utf-8 /permissive- /Zi)
    target_link_options(fps PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(fps PRIVATE dbghelp)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT fps)
else()
    target_compile_options(fps PRIVATE -Wall -Wextra)
endif()

# Shaders live next to the executable
add_custom_command(TARGET fps POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:fps>/shaders)
