# Test driver for packaged VRHI
# Configures, builds, and runs the standalone test project

# Validate inputs
if(NOT CONFIG)
    message(FATAL_ERROR "CONFIG not specified (Debug or Release)")
endif()

if(NOT BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR not specified")
endif()

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not specified")
endif()

# Use an explicit package when provided, otherwise test the staging directory.
if(NOT PACKAGE_PATH AND DEFINED ENV{VRHI_PACKAGE_PATH} AND NOT "$ENV{VRHI_PACKAGE_PATH}" STREQUAL "")
    set(PACKAGE_PATH "$ENV{VRHI_PACKAGE_PATH}")
endif()
if(NOT PACKAGE_PATH)
    set(PACKAGE_PATH "${BINARY_DIR}/vrhi_${CONFIG}")
endif()
get_filename_component(PACKAGE_PATH "${PACKAGE_PATH}" ABSOLUTE)
if(NOT TEST_ROOT)
    set(TEST_ROOT "${BINARY_DIR}/test_pkg_external_${CONFIG}")
endif()
set(TEST_SOURCE_DIR "${TEST_ROOT}/source")
set(TEST_BUILD_DIR "${TEST_ROOT}/build")
file(MAKE_DIRECTORY "${TEST_SOURCE_DIR}")
file(COPY "${SOURCE_DIR}/test/CMakeLists.txt" "${SOURCE_DIR}/test/test_package.cpp"
    "${SOURCE_DIR}/test/package_smoke.spv" DESTINATION "${TEST_SOURCE_DIR}")

# Check package exists
if(NOT EXISTS "${PACKAGE_PATH}/include/vrhi.h")
    message(FATAL_ERROR "Package not found at: ${PACKAGE_PATH}/include/vrhi.h")
endif()

# ========= Step 1: Configure test project =========
message(STATUS "[test_package] Configuring for ${CONFIG}...")

# Set MSVC runtime library based on config
if(CONFIG STREQUAL "Debug")
    set(MSVC_RUNTIME "MultiThreadedDebug")
else()
    set(MSVC_RUNTIME "MultiThreaded")
endif()

# Build cmake configure command with optional generator/compiler passthrough
set(CONFIGURE_ARGS
    -S "${TEST_SOURCE_DIR}"
    -B "${TEST_BUILD_DIR}"
    -DVRHI_PACKAGE_PATH:PATH=${PACKAGE_PATH}
    -DCMAKE_BUILD_TYPE:STRING=${CONFIG}
    -DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${MSVC_RUNTIME}
)

# Pass through generator if specified (check for non-empty)
if(CMAKE_GENERATOR)
    list(APPEND CONFIGURE_ARGS -G "${CMAKE_GENERATOR}")
endif()

# Pass through C/C++ compilers if specified (check for non-empty)
if(CMAKE_C_COMPILER)
    list(APPEND CONFIGURE_ARGS -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER})
endif()
if(CMAKE_CXX_COMPILER)
    list(APPEND CONFIGURE_ARGS -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER})
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} ${CONFIGURE_ARGS}
    RESULT_VARIABLE config_result
    OUTPUT_QUIET
    ERROR_VARIABLE config_error
)

if(config_result AND NOT config_result EQUAL 0)
    message(FATAL_ERROR "[test_package] Configuration failed:\n${config_error}")
endif()

# ========= Step 2: Build test executable =========
message(STATUS "[test_package] Building for ${CONFIG}...")

execute_process(
    COMMAND ${CMAKE_COMMAND}
        --build "${TEST_BUILD_DIR}"
        --config "${CONFIG}"
        --parallel
    RESULT_VARIABLE build_result
    OUTPUT_QUIET
    ERROR_VARIABLE build_error
)

if(build_result AND NOT build_result EQUAL 0)
    message(FATAL_ERROR "[test_package] Build failed:\n${build_error}")
endif()

# ========= Step 3: Run test executable =========
message(STATUS "[test_package] Running test for ${CONFIG}...")

# Platform-specific executable path
# Multi-config generators (MSVC) put exe in CONFIG subdir
# Single-config generators (Ninja, Make) put it at top level
if(WIN32)
    # Try single-config (Ninja) first, fall back to multi-config (VS)
    if(EXISTS "${TEST_BUILD_DIR}/test_package_exe.exe")
        set(TEST_EXE "${TEST_BUILD_DIR}/test_package_exe.exe")
    else()
        set(TEST_EXE "${TEST_BUILD_DIR}/${CONFIG}/test_package_exe.exe")
    endif()
else()
    # Try single-config first, fall back to multi-config
    if(EXISTS "${TEST_BUILD_DIR}/test_package_exe")
        set(TEST_EXE "${TEST_BUILD_DIR}/test_package_exe")
    else()
        set(TEST_EXE "${TEST_BUILD_DIR}/${CONFIG}/test_package_exe")
    endif()
endif()

if(NOT EXISTS "${TEST_EXE}")
    message(FATAL_ERROR "[test_package] Test executable not found: ${TEST_EXE}")
endif()

# Do not use build-tree library paths when testing the package.
if(APPLE)
    set(ENV{DYLD_LIBRARY_PATH} "${PACKAGE_PATH}/lib")
elseif(UNIX)
    set(ENV{LD_LIBRARY_PATH} "${PACKAGE_PATH}/lib")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=MTL_DEBUG_LAYER "${TEST_EXE}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
)
if(NOT "${test_result}" STREQUAL "0")
    message(FATAL_ERROR "[test_package] Vulkan test failed (${test_result}):\n${test_output}\n${test_error}")
endif()
message(STATUS "[test_package] Vulkan PASSED for ${CONFIG}")

if(APPLE)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env MTL_DEBUG_LAYER=1 "${TEST_EXE}"
            --backend=metal "${TEST_SOURCE_DIR}/package_smoke.spv"
        RESULT_VARIABLE metal_result
        OUTPUT_VARIABLE metal_output
        ERROR_VARIABLE metal_error
    )
    if(NOT "${metal_result}" STREQUAL "0")
        message(FATAL_ERROR "[test_package] Metal test failed (${metal_result}):\n${metal_output}\n${metal_error}")
    endif()
    message(STATUS "[test_package] Metal dispatch PASSED for ${CONFIG}")
endif()