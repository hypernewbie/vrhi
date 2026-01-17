cmake_minimum_required(VERSION 3.20)
project(test_package_exe)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime (/MT or /MTd)
if(MSVC)
    if(NOT CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# Find Vulkan
if(DEFINED ENV{VULKAN_SDK})
    set(Vulkan_ROOT $ENV{VULKAN_SDK})
endif()
find_package(Vulkan REQUIRED)

# VRHI Package path (passed from driver script)
if(NOT VRHI_PACKAGE_PATH)
    message(FATAL_ERROR "VRHI_PACKAGE_PATH not specified")
endif()

# Main executable
add_executable(test_package_exe test_package.cpp)

# Include paths
target_include_directories(test_package_exe PRIVATE
    "${VRHI_PACKAGE_PATH}/include"
)

# Library paths
target_link_directories(test_package_exe PRIVATE
    "${VRHI_PACKAGE_PATH}/lib"
)

# Link libraries
target_link_libraries(test_package_exe PRIVATE
    vrhi
    nvrhi_vk
    nvrhi
    vk-bootstrap
    rtxmu
    ShaderMakeBlob
    slang
    slang-rt
    Vulkan::Vulkan
)

# Windows system libraries
if(WIN32)
    target_link_libraries(test_package_exe PRIVATE
        dxgi
        shlwapi
    )
endif()

# Platform defines
if(WIN32)
    target_compile_definitions(test_package_exe PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()