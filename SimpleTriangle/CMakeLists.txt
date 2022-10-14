cmake_minimum_required(VERSION 3.15.0)

find_package(Vulkan REQUIRED)
if (NOT Vulkan_FOUND)
   message(ERROR  "Could not find Vulkan SDK!")
endif()

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

add_executable(SimpleTriangle WIN32 ${SourceFiles})

target_link_libraries(SimpleTriangle ${Vulkan_LIBRARY})

if (MSVC)
    # Tell MSVC to use main instead of WinMain for Windows subsystem executables
    set_target_properties(SimpleTriangle PROPERTIES
                          LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

