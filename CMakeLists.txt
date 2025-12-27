cmake_minimum_required(VERSION 3.20)
project(VoxelEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Fetch dependencies
include(FetchContent)

# GLFW - Windowing and input
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM - Math library
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# stb - Image loading (header-only)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# FastNoiseLite - Terrain generation (header-only)
FetchContent_Declare(
    fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG master
)
FetchContent_MakeAvailable(fastnoiselite)

# meshoptimizer - GPU-friendly mesh optimization
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v0.21
)
FetchContent_MakeAvailable(meshoptimizer)

# ============================================================================
# VULKAN DEPENDENCIES
# ============================================================================

# Vulkan-Headers - Official Vulkan API headers
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.3.275.0
)
FetchContent_MakeAvailable(vulkan_headers)

# volk - Meta-loader for Vulkan (loads all functions dynamically)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.3.275.0
)
set(VOLK_STATIC_DEFINES ON CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(volk)

# VulkanMemoryAllocator - Efficient GPU memory management (header-only)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.0.1
)
# VMA is header-only, just fetch the content without building
FetchContent_GetProperties(vma)
if(NOT vma_POPULATED)
    FetchContent_Populate(vma)
endif()

# Source files for main game (exclude launcher)
file(GLOB_RECURSE GAME_SOURCES
    "src/*.cpp"
)
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*launcher/.*")

# GLAD (OpenGL loader) - we'll add this manually
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/vendor/glad)

# Main game executable
add_executable(${PROJECT_NAME}
    ${GAME_SOURCES}
    ${GLAD_DIR}/src/gl.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${GLAD_DIR}/include
    ${stb_SOURCE_DIR}
    ${fastnoiselite_SOURCE_DIR}/Cpp
    # Vulkan includes
    ${vulkan_headers_SOURCE_DIR}/include
    ${volk_SOURCE_DIR}
    ${vma_SOURCE_DIR}/include
)

# Vulkan platform defines
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glm::glm
    opengl32
    meshoptimizer
)

# OpenGL Launcher executable (Windows only)
if(WIN32)
    add_executable(VoxelLauncher WIN32
        src/launcher/GLLauncher.cpp
        ${GLAD_DIR}/src/gl.c
    )

    target_include_directories(VoxelLauncher PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${GLAD_DIR}/include
        ${stb_SOURCE_DIR}
    )

    target_link_libraries(VoxelLauncher PRIVATE
        glfw
        glm::glm
        opengl32
    )

    # Put launcher in same directory as game
    set_target_properties(VoxelLauncher PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Copy assets for launcher too
    add_custom_command(TARGET VoxelLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:VoxelLauncher>/assets
    )
endif()

# Copy assets to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)

# Compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# VULKAN TEST EXECUTABLE
# ============================================================================
add_executable(VulkanTest
    tests/VulkanTest.cpp
)

target_include_directories(VulkanTest PRIVATE
    ${vulkan_headers_SOURCE_DIR}/include
    ${volk_SOURCE_DIR}
)

target_link_libraries(VulkanTest PRIVATE
    glfw
)

# volk needs to link against the system's Vulkan loader on Windows
if(WIN32)
    target_compile_definitions(VulkanTest PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

set_target_properties(VulkanTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
