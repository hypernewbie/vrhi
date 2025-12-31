cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
    set(CMAKE_C_COMPILER clang CACHE STRING "")
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "")
endif()

project(vrhi_test CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use Static C Runtime (/MT for Release, /MTd for Debug)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W3 /wd4100 /MP) # W3: Standard warning level, wd4100: Ignore unused parameters, MP: Multi-processor compilation
else()
    add_compile_options(-w -stdlib=libc++) # Squish all warnings and use libc++
    add_link_options(-stdlib=libc++ -lc++abi)
endif()

# Find Python Interpreter (Optional)
find_package(Python3 COMPONENTS Interpreter)

set(GENERATED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/vrhi_generated.h")

# Code Generation (only if Python is found)
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${GENERATED_HEADER}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h" "${GENERATED_HEADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h"
        COMMENT "Generating vrhi_generated.h via vidl.py..."
        VERBATIM
    )
endif()

# Source files
set(SOURCES
    test.cpp
    vrhi.h
    vrhi_impl.h
    vrhi_defines.h
    vrhi_generated.h
    vrhi_utils.h
    utest.h
    
    vrhi_impl_backend.h
    test_impl_backend.cpp
    
    vrhi_impl_device.h
    test_impl_device.cpp
    
    vrhi_impl_texture.h
    test_impl_texture.cpp

    vrhi_impl_buffer.h
    test_impl_buffer.cpp

    vrhi_impl_shader.h
    test_impl_shader.cpp
    
    test_impl_definitions.cpp
)

# Add executable
add_executable(vrhi_test ${SOURCES})

target_compile_definitions(vrhi_test PRIVATE 
    $<$<CONFIG:Debug>:VRHI_SHARDED_BUILD>
)

# Include directories
target_include_directories(vrhi_test PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Library search paths
# Generator expressions select the correct path based on build configuration
target_link_directories(vrhi_test PRIVATE
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    # Linux paths
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
)

# NVRHI and helper libraries
# We link all backends (D3D11, D3D12, Vulkan) as they are available in the lib folder
set(VRHI_LIBS
    vk-bootstrap
    nvrhi_vk
    nvrhi
    rtxmu
)

target_link_libraries(vrhi_test PRIVATE ${VRHI_LIBS})

# Find and link Vulkan SDK loader (we use local headers but need the lib)
find_package(Vulkan REQUIRED)
target_link_libraries(vrhi_test PRIVATE Vulkan::Vulkan)

# System libraries required by NVRHI backends on Windows
if(WIN32)
    target_link_libraries(vrhi_test PRIVATE
        dxgi
        shlwapi
    )
endif()

# Enable CTest
enable_testing()
add_test(NAME vrhi_test COMMAND vrhi_test)

