# Shim CMakeLists.txt for building SPIRV-Tools with corrected compiler flags.
# Builds source from vdeps/SPIRV-Tools submodule but overrides the hardcoded
# /WX (warnings-as-errors) that prevents Debug builds with /MTd on MSVC.

cmake_minimum_required( VERSION 3.22.1 )

# Required for CMAKE_MSVC_RUNTIME_LIBRARY to work correctly
cmake_policy( SET CMP0091 NEW )

# Propagate static runtime setting to sub-projects
# vdeps.py passes this, but we set it here as well to ensure it's respected
if( NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY )
    set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
endif()

project( spirv-tools-shim )

set( SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SPIRV-Headers" CACHE STRING "" FORCE )

set( SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE )
set( SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE )
set( SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE )
set( SPIRV_WERROR OFF CACHE BOOL "" FORCE )
set( SPIRV_BUILD_FUZZER OFF CACHE BOOL "" FORCE )
set( BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE )

add_subdirectory( ../SPIRV-Tools ${CMAKE_CURRENT_BINARY_DIR}/SPIRV-Tools )

if( MSVC )
    target_compile_options( SPIRV-Tools-opt PRIVATE /WX- )
    if( TARGET SPIRV-Tools-static )
        target_compile_options( SPIRV-Tools-static PRIVATE /WX- )
    endif()
endif()
