cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
    set(CMAKE_C_COMPILER clang CACHE STRING "")
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "")
endif()

project(vrhi_test C CXX)

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

# Find Vulkan (REQUIRED) - moved up to ensure Vulkan_INCLUDE_DIR is set
find_package(Vulkan REQUIRED)

set(GENERATED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/vrhi_generated.h")

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

# VRHI Library Sources
set(SPIRV_REFLECT_SOURCE "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect/spirv_reflect.c")
set(SPIRV_REFLECT_HEADER "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect/spirv_reflect.h")

set(VRHI_SOURCES
    vrhi.h
    src/vrhi_internal.h
    vrhi_defines.h
    ${GENERATED_HEADER}
    src/vrhi_utils.h
    src/vrhi_backend.h

    src/vrhi_internal.cpp
    ${SPIRV_REFLECT_SOURCE}
    ${SPIRV_REFLECT_HEADER}
    src/vrhi_backend.cpp
    src/vrhi_device.cpp
    src/vrhi_texture.cpp
    src/vrhi_buffer.cpp
    src/vrhi_shader.cpp
    src/vrhi_state.cpp
)

# Test Sources
set(TEST_SOURCES
    test/test.cpp
    test/utest.h
    test/test_backend.cpp
    test/test_device.cpp
    test/test_texture.cpp
    test/test_buffer.cpp
    test/test_shader.cpp
    test/test_state.cpp
    test/test_compute.cpp
)

# Add VRHI Static Library
add_library(vrhi STATIC ${VRHI_SOURCES})

if(MSVC)
    set_source_files_properties(${SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4013")
else()
    set_source_files_properties(${SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()

target_compile_definitions(vrhi PRIVATE 
    VRHI_UNIT_TEST
)

if(WIN32)
    target_compile_definitions(vrhi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

target_include_directories(vrhi PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Add Vulkan SDK Source/SPIRV-Reflect to include paths so we can directly inline inc the .c file
target_include_directories(vrhi PRIVATE
    "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect"
)

# Library search paths
# Generator expressions select the correct path based on build configuration
target_link_directories(vrhi PUBLIC
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    # Linux paths
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    # Mac paths
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
)

# NVRHI and helper libraries
set(VRHI_LIBS
    vk-bootstrap
    nvrhi_vk
    nvrhi
    rtxmu
)

target_link_libraries(vrhi PUBLIC ${VRHI_LIBS})

target_link_libraries(vrhi PUBLIC Vulkan::Vulkan)

# System libraries required by NVRHI backends on Windows
if(WIN32)
    target_link_libraries(vrhi PUBLIC
        dxgi
        shlwapi
    )
endif()

# Add Executable
add_executable(vrhi_test ${TEST_SOURCES})

# Link VRHI Library
target_link_libraries(vrhi_test PRIVATE vrhi)

if(WIN32)
    target_compile_definitions(vrhi_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Enable CTest
enable_testing()
add_test(NAME vrhi_test COMMAND vrhi_test)