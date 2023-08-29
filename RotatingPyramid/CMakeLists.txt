cmake_minimum_required(VERSION 3.15.0)

find_package(Vulkan REQUIRED)
if (NOT Vulkan_FOUND)
   message(ERROR  "Could not find Vulkan SDK!")
endif()

set(${CMAKE_CURRENT_SOURCE_DIR}/shaders)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
file(GLOB Shaders ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.comp ${SHADER_DIR}/*.geom ${SHADER_DIR}/*.tesc ${SHADER_DIR}/*.tese ${SHADER_DIR}/*.mesh ${SHADER_DIR}/*.task ${SHADER_DIR}/*.rgen ${SHADER_DIR}/*.rchit ${SHADER_DIR}/*.rmiss)

foreach(SHADER IN LISTS Shaders)
    get_filename_component(FILENAME ${SHADER} NAME)
    add_custom_command(OUTPUT ${SHADER_DIR}/${FILENAME}.spv
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${SHADER_DIR}/${FILENAME}.spv
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILENAME}")
list(APPEND SPV_SHADERS ${SHADER_DIR}/${FILENAME}.spv)
endForeach()

add_custom_target(Shaders ALL DEPENDS ${SPV_SHADERS})

add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${Vulkan_INCLUDE_DIR})

set(SourceFiles 
"main.cpp" 
)

add_executable(RotatingPyramid WIN32 ${SourceFiles} ${Shaders})

target_include_directories(RotatingPyramid PRIVATE ${CMAKE_SOURCE_DIR}/deps/stb)

target_link_libraries(RotatingPyramid ${Vulkan_LIBRARY})

add_custom_target(CopyResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/RotatingPyramid/shaders ${PROJECT_BINARY_DIR}/RotatingPyramid/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/RotatingPyramid/textures ${PROJECT_BINARY_DIR}/RotatingPyramid/textures
    COMMENT "Copying resources to binary directory..."
)

add_dependencies(CopyResources Shaders)
add_dependencies(RotatingPyramid CopyResources)

if (MSVC)
    # Tell MSVC to use main instead of WinMain for Windows subsystem executables
    set_target_properties(RotatingPyramid PROPERTIES
                          LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

