/*
    -- Vrhi --

    Copyright 2026 UAA Software

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
    associated documentation files (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge, publish, distribute,
    sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial
    portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
    NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
    OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

// Define this if you have these in PCH already.
#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

#include <vulkan/vulkan.h>   // core handles for native-interop structs; no VK_USE_PLATFORM_* needed

#include <nvrhi/nvrhi.h>

// --------------------------------------------------------------------------
// Init
// --------------------------------------------------------------------------

// Identifies the GPU and driver a pipeline cache was built for (the same identity the Vulkan cache
// header carries). Stamp a persisted vhGetPSOCache() blob with this (from vhGetPSOCacheKey()) and
// feed it back via vhInitData::psoCacheValidateKey so a blob from a different device is discarded.
struct vhPSOCacheKey
{
    uint8_t  pipelineCacheUUID[16] = {};
    uint32_t vendorID      = 0;
    uint32_t deviceID      = 0;
    uint32_t driverVersion = 0;
    uint32_t apiVersion    = 0;
};

struct vhInitData
{
    // Application identity for Vulkan instance creation.
    std::string appName = "VRHI_APP";
    std::string engineName = "VRHI_ENGINE";
    // Device selection. -1 for automatic (Discrete > Integrated > CPU).
    int deviceIndex = -1;
    // Initial window resolution for swapchain creation.
    glm::ivec2 resolution = glm::ivec2( 1280, 720 );
    // Logging and thread initialisation callbacks.
    std::function<void( bool error, const std::string& )> fnLogCallback = nullptr;
    std::function<void() > fnThreadInitCallback = nullptr;
    std::function<void( const char*, bool )> fnProfileCallback = nullptr;

    // Debug and feature toggles.
    bool debug = false;
    bool raytracing = true;
    bool forceVulkan12 = false;
    bool nullMode = false;
    bool logBackendCmds = false;
    bool logPSOCache = false;
    bool debugBlockWaitForBackend = false;
    bool errorOnSkippedDraw = false;
    bool shaderFloat16 = false;
    bool shaderInt16 = false;
    bool fragmentShadingRate = false;
    // Extensions enabled only if the physical device exposes them; absence is non-fatal.
    std::vector< std::string > extraDeviceExtensions;

    // Platform Window Handles
    // On Windows, set windowHandle to (void*)HWND.
    // On Linux (X11), set windowHandle to (void*)Window and displayHandle to (void*)Display.
    // On Linux (Wayland), set windowHandle to (void*)wl_surface*, displayHandle to (void*)wl_display*, and linuxUseWayland = true.
    // On macOS, set windowHandle to (void*)CAMetalLayer* by default, or (void*)NSView* with macOSWindowIsNSView = true.
    void* windowHandle = nullptr;
    void* displayHandle = nullptr;
    bool linuxUseWayland = false;
    bool macOSWindowIsNSView = false;
    bool headless = true;
    bool vsync = true;

    // Display configuration. Format and present-mode requests are best-effort; vkb falls back to its default if the surface doesn't expose them.
    bool hdr10 = false;
    bool scrgb = false;
    bool mailbox = false;
    bool presentWait = false;

    // Shader compilation configuration.
    std::string shaderCompileTempDir = "./tmp/shader_cache/";
    std::string shaderMakePath = "./tools/linux_release";
    std::string shaderMakeSlangPath = "./tools/linux_release";
    bool forceShaderRecompile = false; // Ignore cache, always recompile
    bool skipShaderCacheWrite = false; // Don't write .spirv cache files
    bool dumpShaderSource = false;      // Write .slang source files for debugging
    bool robust = false;
    bool markers = true;
    bool renderdoc = false;

    // Register binding shifts define 4 distinct ranges for descriptor types.
    //
    // * Samplers (s registers): Default shift is 100.
    // * Textures (t registers): Default shift is 200.
    // * Constant Buffers (b registers): Default shift is 300.
    // * UAVs (u registers): Default shift is 400.
    //
    // The range values can be customised, the separation pattern cannot.
    // Do not shift manual slots yourself; vhCompileShader applies these ranges automatically.
    // Must be s < t < b < u shifts, with uRegShift having highest value.
    //
    uint32_t shaderMake_sRegShift = 100;
    uint32_t shaderMake_tRegShift = 200;
    uint32_t shaderMake_bRegShift = 300;
    uint32_t shaderMake_uRegShift = 400;

    // Global uniform buffer sizes. These back VRHI's state uniform system (vhState::uniforms).
    uint32_t maxViewGlobals = 4 * 1024;           // Per-view constants (camera, projection, etc).
    uint32_t maxWorldMatrices = 16 * 1024;         // World-space transform matrices.
    uint32_t maxUserGlobals = 16 * 1024 * 1024;    // User shader parameters (material data, etc).

    // Flushes between nvrhi runGarbageCollection() runs. 0 or 1 forces every flush. vhFinish() always GCs.
    uint32_t gcFlushInterval = 16;

    // Seeds the Vulkan driver pipeline cache. Obtained from a previous vhGetPSOCache().
    std::vector< uint8_t > psoCacheInitialData;

    // Optional integrity guard for psoCacheInitialData. Stamp this with the vhGetPSOCacheKey() taken
    // when the blob was saved; vhInit() compares it against the GPU/driver it actually selects and
    // skips seeding on mismatch, so a cache from a different device is never fed to the driver. Leave
    // zero-initialised (the default) to seed unconditionally. Even when skipped the driver rejects a
    // foreign blob safely, so this only saves the wasted parse. Ignored when psoCacheInitialData is
    // empty. vhInit() never clears psoCacheInitialData; the caller still owns and frees it as usual.
    vhPSOCacheKey psoCacheValidateKey;
};

typedef uint32_t vhTexture;
typedef uint32_t vhBuffer;
typedef uint32_t vhShader;
typedef uint32_t vhUniform;
typedef uint64_t vhTimerID;
typedef uint64_t vhSwapchainID;
typedef std::vector< uint8_t > vhMem;
typedef std::vector< vhShader > vhProgram;
typedef uint32_t vhAccelStruct;
typedef uint32_t vhRTPipeline;
typedef uint32_t vhShaderTable;

constexpr vhSwapchainID VRHI_INVALID_SWAPCHAIN = 0;
typedef uint64_t vhStateId;
typedef uint32_t vhHeap; 
typedef uint32_t vhDescriptorTable;

extern vhInitData g_vhInit;
extern nvrhi::DeviceHandle g_vhDevice;
extern std::atomic<int32_t> g_vhErrorCounter;
extern std::atomic<int32_t> g_vhPSOCompileCounter;

// --------------------------------------------------------------------------
// Defines
// --------------------------------------------------------------------------

#define VRHI_VERSION_MAJOR 0
#define VRHI_VERSION_MINOR 8
#define VRHI_VERSION_PATCH 2

#define VRHI_INVALID_HANDLE 0xFFFFFFFF
#define VRHI_MIPMAP_COMPLETE -1

inline int vhGetImageNextMipmapDim( int x )
{
    return ( x > 1 ) ? ( x >> 1 ) : 1;
}

inline glm::ivec3 vhGetImageNextMipmapDim( const glm::ivec3& dimensions )
{
    return glm::ivec3(
        vhGetImageNextMipmapDim( dimensions.x ),
        vhGetImageNextMipmapDim( dimensions.y ),
        vhGetImageNextMipmapDim( dimensions.z )
    );
}

inline int vhGetImageMaxMipCount( const glm::ivec3& dimensions )
{
    if ( dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0 )
        return 0;
    glm::ivec3 dim = dimensions;
    int count = 0;
    while ( dim.x > 1 || dim.y > 1 || dim.z > 1 )
    {
        dim = vhGetImageNextMipmapDim( dim );
        count++;
    }
    return count + 1;
}

// --------------------------------------------------------------------------
// Shader Stages
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_SHADER_STAGE_VERTEX        = 1;
constexpr uint64_t VRHI_SHADER_STAGE_PIXEL         = 2;
constexpr uint64_t VRHI_SHADER_STAGE_COMPUTE       = 3;
constexpr uint64_t VRHI_SHADER_STAGE_HULL          = 4;
constexpr uint64_t VRHI_SHADER_STAGE_DOMAIN        = 5;
constexpr uint64_t VRHI_SHADER_STAGE_GEOMETRY      = 6;
constexpr uint64_t VRHI_SHADER_STAGE_RAYGEN        = 7;
constexpr uint64_t VRHI_SHADER_STAGE_MISS          = 8;
constexpr uint64_t VRHI_SHADER_STAGE_CLOSEST_HIT   = 9;
constexpr uint64_t VRHI_SHADER_STAGE_MESH          = 10;
constexpr uint64_t VRHI_SHADER_STAGE_AMPLIFICATION = 11;
constexpr uint64_t VRHI_SHADER_STAGE_ANY_HIT       = 12;
constexpr uint64_t VRHI_SHADER_STAGE_INTERSECTION  = 13;
constexpr uint64_t VRHI_SHADER_STAGE_CALLABLE      = 14;

constexpr uint64_t VRHI_SHADER_STAGE_MAX           = 14;
constexpr uint64_t VRHI_SHADER_STAGE_MASK          = 0xF;

// Shader Stage -> Descriptor Set Mapping (Fixed, Non-Merging):
// Each stage gets its own fixed set index to avoid descriptor set merging.
// Mutually exclusive stages share indices (e.g. VS/CS, PS/Miss).
// Use VRHI_STAGE_SPACE in register declarations to target these automatically.
//
constexpr uint32_t VRHI_DESCRIPTOR_SET_VERTEX        = 1; // VS default set (shared with CS)
constexpr uint32_t VRHI_DESCRIPTOR_SET_PIXEL         = 2; // PS default set
constexpr uint32_t VRHI_DESCRIPTOR_SET_HULL          = 3;
constexpr uint32_t VRHI_DESCRIPTOR_SET_DOMAIN        = 4;
constexpr uint32_t VRHI_DESCRIPTOR_SET_GEOMETRY      = 5;
constexpr uint32_t VRHI_DESCRIPTOR_SET_MESH          = 6;
constexpr uint32_t VRHI_DESCRIPTOR_SET_AMPLIFICATION = 7;

constexpr uint32_t VRHI_DESCRIPTOR_SET_COMPUTE       = 1; // CS default set (shared with VS)

constexpr uint32_t VRHI_DESCRIPTOR_SET_RAYGEN        = 1; // RT stages
constexpr uint32_t VRHI_DESCRIPTOR_SET_MISS          = 2;
constexpr uint32_t VRHI_DESCRIPTOR_SET_CLOSEST_HIT   = 3;
constexpr uint32_t VRHI_DESCRIPTOR_SET_ANY_HIT       = 4;
constexpr uint32_t VRHI_DESCRIPTOR_SET_INTERSECTION  = 5;
constexpr uint32_t VRHI_DESCRIPTOR_SET_CALLABLE      = 6;

constexpr uint32_t VRHI_DESCRIPTOR_SET_MAX           = 8;

// Declare bindless arrays in VRHI_BINDLESS_SPACE, starting at register t0/u0/b0/s0:
//     Texture2D    g_Textures[]   : register(t0, VRHI_BINDLESS_SPACE);
//     SamplerState g_Samplers[]   : register(s0, VRHI_BINDLESS_SPACE);
// Index them with NonUniformResourceIndex() when the index varies across a wave.
constexpr uint32_t VRHI_DESCRIPTOR_SET_BINDLESS      = 0;
constexpr uint32_t VRHI_BINDLESS_REGISTER_SPACE      = 8;

// Returns the fixed descriptor set index for a given shader stage.
//
// Each shader stage uses a unique, independent Descriptor Set index for its VRHI_STAGE_SPACE resources.
// Set 1: Vertex / Compute
// Set 2: Pixel
// Set 3+: Other stages
//
inline uint32_t vhGetDescriptorSetForStage( uint64_t stageFlags )
{
    uint64_t stage = stageFlags & VRHI_SHADER_STAGE_MASK;
    switch ( stage )
    {
        case VRHI_SHADER_STAGE_VERTEX:        return VRHI_DESCRIPTOR_SET_VERTEX;
        case VRHI_SHADER_STAGE_PIXEL:         return VRHI_DESCRIPTOR_SET_PIXEL;
        case VRHI_SHADER_STAGE_COMPUTE:       return VRHI_DESCRIPTOR_SET_COMPUTE;
        case VRHI_SHADER_STAGE_HULL:          return VRHI_DESCRIPTOR_SET_HULL;
        case VRHI_SHADER_STAGE_DOMAIN:        return VRHI_DESCRIPTOR_SET_DOMAIN;
        case VRHI_SHADER_STAGE_GEOMETRY:      return VRHI_DESCRIPTOR_SET_GEOMETRY;
        case VRHI_SHADER_STAGE_MESH:          return VRHI_DESCRIPTOR_SET_MESH;
        case VRHI_SHADER_STAGE_AMPLIFICATION: return VRHI_DESCRIPTOR_SET_AMPLIFICATION;
        case VRHI_SHADER_STAGE_RAYGEN:        return VRHI_DESCRIPTOR_SET_RAYGEN;
        case VRHI_SHADER_STAGE_MISS:          return VRHI_DESCRIPTOR_SET_MISS;
        case VRHI_SHADER_STAGE_CLOSEST_HIT:   return VRHI_DESCRIPTOR_SET_CLOSEST_HIT;
        case VRHI_SHADER_STAGE_ANY_HIT:       return VRHI_DESCRIPTOR_SET_ANY_HIT;
        case VRHI_SHADER_STAGE_INTERSECTION:  return VRHI_DESCRIPTOR_SET_INTERSECTION;
        case VRHI_SHADER_STAGE_CALLABLE:      return VRHI_DESCRIPTOR_SET_CALLABLE;
        default:                              assert( !"Invalid stage" ); return VRHI_DESCRIPTOR_SET_MAX;
    }
}

constexpr uint64_t VRHI_SHADER_SM_5_0              = ( 1 << 4 );
constexpr uint64_t VRHI_SHADER_SM_6_0              = ( 2 << 4 );
constexpr uint64_t VRHI_SHADER_SM_6_5              = ( 3 << 4 ); // Default behaviour if 0
constexpr uint64_t VRHI_SHADER_SM_6_6              = ( 4 << 4 );
constexpr uint64_t VRHI_SHADER_SM_MASK             = 0xF0;

constexpr uint64_t VRHI_SHADER_DEBUG               = ( 1ULL << 8 );   // -O0 -g -embedPDB
constexpr uint64_t VRHI_SHADER_ROW_MAJOR           = ( 1ULL << 9 );   // -matrix-layout-row-major
constexpr uint64_t VRHI_SHADER_WARNINGS_AS_ERRORS  = ( 1ULL << 10 );  // -warnings-as-errors
constexpr uint64_t VRHI_SHADER_STRIP_REFLECTION    = ( 1ULL << 11 );  // --stripReflection. Good for release builds to reduce binary size.
constexpr uint64_t VRHI_SHADER_ALL_RESOURCES_BOUND = ( 1ULL << 12 );  // --allResourcesBound. Optimisation hint for the compiler.
constexpr uint64_t VRHI_SHADER_PATCH_DSET0         = ( 1ULL << 13 );  // Post-compile SPIR-V patch: remaps DescriptorSet 0 to VRHI_STAGE_SPACE (stage-specific set). Useful when shaders omit explicit register spaces.

// --------------------------------------------------------------------------
// Buffers
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_BUFFER_NONE           = 0x0000;
constexpr uint64_t VRHI_BUFFER_COMPUTE_READ   = 0x0100;
constexpr uint64_t VRHI_BUFFER_COMPUTE_WRITE  = 0x0200;
constexpr uint64_t VRHI_BUFFER_DRAW_INDIRECT  = 0x0400;
constexpr uint64_t VRHI_BUFFER_ALLOW_RESIZE   = 0x0800;
constexpr uint64_t VRHI_BUFFER_INDEX32        = 0x1000;
constexpr uint64_t VRHI_BUFFER_VIRTUAL        = 0x2000;
// Buffer is intended for use as a ray tracing acceleration structure build input (e.g. BLAS
// vertex/index buffers, or raw AABB buffers for procedural geometry).
constexpr uint64_t VRHI_BUFFER_ACCEL_INPUT    = 0x4000;
constexpr uint64_t VRHI_BUFFER_COMPUTE_READ_WRITE = ( VRHI_BUFFER_COMPUTE_READ | VRHI_BUFFER_COMPUTE_WRITE );

// --------------------------------------------------------------------------
// Textures
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_TEXTURE_NONE         = 0x0000000000000000;
constexpr uint64_t VRHI_TEXTURE_RT           = 0x0000001000000000;
constexpr uint64_t VRHI_TEXTURE_COMPUTE_WRITE = 0x0000100000000000;
constexpr uint64_t VRHI_TEXTURE_SRGB         = 0x0000200000000000;
constexpr uint64_t VRHI_TEXTURE_BLIT_DST     = 0x0000400000000000;
constexpr uint64_t VRHI_TEXTURE_VIRTUAL      = 0x0000800000000000;

// --------------------------------------------------------------------------
// Samplers
// --------------------------------------------------------------------------

constexpr uint32_t VRHI_SAMPLER_U_WRAP    = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_U_MIRROR  = 0x00000001;
constexpr uint32_t VRHI_SAMPLER_U_CLAMP   = 0x00000002;
constexpr uint32_t VRHI_SAMPLER_U_BORDER  = 0x00000003;
constexpr uint32_t VRHI_SAMPLER_U_SHIFT   = 0;
constexpr uint32_t VRHI_SAMPLER_U_MASK    = 0x00000003;
constexpr uint32_t VRHI_SAMPLER_V_WRAP    = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_V_MIRROR  = 0x00000004;
constexpr uint32_t VRHI_SAMPLER_V_CLAMP   = 0x00000008;
constexpr uint32_t VRHI_SAMPLER_V_BORDER  = 0x0000000c;
constexpr uint32_t VRHI_SAMPLER_V_SHIFT   = 2;
constexpr uint32_t VRHI_SAMPLER_V_MASK    = 0x0000000c;
constexpr uint32_t VRHI_SAMPLER_W_WRAP    = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_W_MIRROR  = 0x00000010;
constexpr uint32_t VRHI_SAMPLER_W_CLAMP   = 0x00000020;
constexpr uint32_t VRHI_SAMPLER_W_BORDER  = 0x00000030;
constexpr uint32_t VRHI_SAMPLER_W_SHIFT   = 4;
constexpr uint32_t VRHI_SAMPLER_W_MASK    = 0x00000030;
constexpr uint32_t VRHI_SAMPLER_MIN_LINEAR      = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_MIN_POINT       = 0x00000040;
constexpr uint32_t VRHI_SAMPLER_MIN_ANISOTROPIC = 0x00000080;
constexpr uint32_t VRHI_SAMPLER_MIN_SHIFT       = 6;
constexpr uint32_t VRHI_SAMPLER_MIN_MASK        = 0x000000c0;
constexpr uint32_t VRHI_SAMPLER_MAG_LINEAR      = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_MAG_POINT       = 0x00000100;
constexpr uint32_t VRHI_SAMPLER_MAG_ANISOTROPIC = 0x00000200;
constexpr uint32_t VRHI_SAMPLER_MAG_SHIFT       = 8;
constexpr uint32_t VRHI_SAMPLER_MAG_MASK        = 0x00000300;
constexpr uint32_t VRHI_SAMPLER_MIP_LINEAR      = 0x00000000;
constexpr uint32_t VRHI_SAMPLER_MIP_POINT       = 0x00000400;
constexpr uint32_t VRHI_SAMPLER_MIP_NONE        = 0x00000800;
constexpr uint32_t VRHI_SAMPLER_MIP_SHIFT       = 10;
constexpr uint32_t VRHI_SAMPLER_MIP_MASK        = 0x00000c00;

constexpr uint32_t VRHI_SAMPLER_COMPARE_LESS      = 0x00001000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_LEQUAL    = 0x00002000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_EQUAL     = 0x00003000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_GEQUAL    = 0x00004000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_GREATER   = 0x00005000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_NOTEQUAL  = 0x00006000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_NEVER     = 0x00007000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_ALWAYS    = 0x00008000;
constexpr uint32_t VRHI_SAMPLER_COMPARE_SHIFT     = 12;

constexpr uint32_t VRHI_SAMPLER_COMPARE_MASK      = 0x0000f000;

#define VRHI_SAMPLER_MIPBIAS_SHIFT                16
#define VRHI_SAMPLER_MIPBIAS_MASK                 0x00ff0000
#define VRHI_SAMPLER_MIPBIAS( v )                 ( ( ( uint32_t )( int32_t )( ( v ) * 16.0f ) << VRHI_SAMPLER_MIPBIAS_SHIFT ) & VRHI_SAMPLER_MIPBIAS_MASK )

#define VRHI_SAMPLER_BORDER_COLOUR_SHIFT          24
#define VRHI_SAMPLER_BORDER_COLOUR_MASK           0x0f000000
#define VRHI_SAMPLER_BORDER_COLOUR( v )           ( ( ( uint32_t )( v ) << VRHI_SAMPLER_BORDER_COLOUR_SHIFT ) & VRHI_SAMPLER_BORDER_COLOUR_MASK )

constexpr uint32_t VRHI_SAMPLER_SAMPLE_STENCIL    = 0x10000000;
#define VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT         29
#define VRHI_SAMPLER_MAX_ANISOTROPY_MASK          0xe0000000
#define VRHI_SAMPLER_MAX_ANISOTROPY( v )          ( ( ( uint32_t )( v ) << VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT ) & VRHI_SAMPLER_MAX_ANISOTROPY_MASK )
constexpr uint32_t VRHI_SAMPLER_ANISOTROPY_1      = VRHI_SAMPLER_MAX_ANISOTROPY( 0 );
constexpr uint32_t VRHI_SAMPLER_ANISOTROPY_2      = VRHI_SAMPLER_MAX_ANISOTROPY( 1 );
constexpr uint32_t VRHI_SAMPLER_ANISOTROPY_4      = VRHI_SAMPLER_MAX_ANISOTROPY( 2 );
constexpr uint32_t VRHI_SAMPLER_ANISOTROPY_8      = VRHI_SAMPLER_MAX_ANISOTROPY( 3 );
constexpr uint32_t VRHI_SAMPLER_ANISOTROPY_16     = VRHI_SAMPLER_MAX_ANISOTROPY( 4 );
constexpr uint32_t VRHI_SAMPLER_NONE              = 0x00000000;

constexpr uint32_t VRHI_SAMPLER_POINT = (
    VRHI_SAMPLER_MIN_POINT |
    VRHI_SAMPLER_MAG_POINT |
    VRHI_SAMPLER_MIP_POINT );
constexpr uint32_t VRHI_SAMPLER_UVW_MIRROR = (
    VRHI_SAMPLER_U_MIRROR |
    VRHI_SAMPLER_V_MIRROR |
    VRHI_SAMPLER_W_MIRROR );

constexpr uint32_t VRHI_SAMPLER_UVW_CLAMP = (
    VRHI_SAMPLER_U_CLAMP |
    VRHI_SAMPLER_V_CLAMP |
    VRHI_SAMPLER_W_CLAMP );

constexpr uint32_t VRHI_SAMPLER_UVW_BORDER = (
    VRHI_SAMPLER_U_BORDER |
    VRHI_SAMPLER_V_BORDER |
    VRHI_SAMPLER_W_BORDER );

constexpr uint32_t VRHI_SAMPLER_UVW_WRAP = (
    VRHI_SAMPLER_U_WRAP |
    VRHI_SAMPLER_V_WRAP |
    VRHI_SAMPLER_W_WRAP );

constexpr uint32_t VRHI_SAMPLER_BITS_MASK = (
    VRHI_SAMPLER_U_MASK |
    VRHI_SAMPLER_V_MASK |
    VRHI_SAMPLER_W_MASK |
    VRHI_SAMPLER_MIN_MASK |
    VRHI_SAMPLER_MAG_MASK |
    VRHI_SAMPLER_MIP_MASK |
    VRHI_SAMPLER_COMPARE_MASK |
    VRHI_SAMPLER_MIPBIAS_MASK |
    VRHI_SAMPLER_BORDER_COLOUR_MASK |
    VRHI_SAMPLER_SAMPLE_STENCIL |
    VRHI_SAMPLER_MAX_ANISOTROPY_MASK );

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_STATE_WRITE_R      = 0x0000000000000001;
constexpr uint64_t VRHI_STATE_WRITE_G      = 0x0000000000000002;
constexpr uint64_t VRHI_STATE_WRITE_B      = 0x0000000000000004;
constexpr uint64_t VRHI_STATE_WRITE_A      = 0x0000000000000008;
constexpr uint64_t VRHI_STATE_WRITE_Z      = 0x0000004000000000;

constexpr uint64_t VRHI_STATE_WRITE_RGB = (
    VRHI_STATE_WRITE_R |
    VRHI_STATE_WRITE_G |
    VRHI_STATE_WRITE_B );

constexpr uint64_t VRHI_STATE_WRITE_MASK = (
    VRHI_STATE_WRITE_RGB |
    VRHI_STATE_WRITE_A |
    VRHI_STATE_WRITE_Z );

constexpr uint64_t VRHI_STATE_DEPTH_TEST_LESS     = 0x0000000000000010;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_LEQUAL   = 0x0000000000000020;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_EQUAL    = 0x0000000000000030;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_GEQUAL   = 0x0000000000000040;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_GREATER  = 0x0000000000000050;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_NOTEQUAL = 0x0000000000000060;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_NEVER    = 0x0000000000000070;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_ALWAYS   = 0x0000000000000080;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_SHIFT    = 4;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_MASK     = 0x00000000000000f0;

constexpr uint64_t VRHI_STATE_BLEND_ZERO           = 0x0000000000001000;
constexpr uint64_t VRHI_STATE_BLEND_ONE            = 0x0000000000002000;
constexpr uint64_t VRHI_STATE_BLEND_SRC_COLOUR     = 0x0000000000003000;
constexpr uint64_t VRHI_STATE_BLEND_INV_SRC_COLOUR = 0x0000000000004000;
constexpr uint64_t VRHI_STATE_BLEND_SRC_ALPHA      = 0x0000000000005000;
constexpr uint64_t VRHI_STATE_BLEND_INV_SRC_ALPHA  = 0x0000000000006000;
constexpr uint64_t VRHI_STATE_BLEND_DST_ALPHA      = 0x0000000000007000;
constexpr uint64_t VRHI_STATE_BLEND_INV_DST_ALPHA  = 0x0000000000008000;
constexpr uint64_t VRHI_STATE_BLEND_DST_COLOUR     = 0x0000000000009000;
constexpr uint64_t VRHI_STATE_BLEND_INV_DST_COLOUR = 0x000000000000a000;
constexpr uint64_t VRHI_STATE_BLEND_SRC_ALPHA_SAT  = 0x000000000000b000;
constexpr uint64_t VRHI_STATE_BLEND_FACTOR         = 0x000000000000c000;
constexpr uint64_t VRHI_STATE_BLEND_INV_FACTOR     = 0x000000000000d000;
constexpr uint64_t VRHI_STATE_BLEND_SHIFT          = 12;
constexpr uint64_t VRHI_STATE_BLEND_MASK           = 0x000000000ffff000;

constexpr uint64_t VRHI_STATE_BLEND_EQUATION_ADD    = 0x0000000000000000;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_SUB    = 0x0000000010000000;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_REVSUB = 0x0000000020000000;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MIN    = 0x0000000030000000;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MAX    = 0x0000000040000000;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_SHIFT  = 28;
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MASK   = 0x00000003f0000000;

constexpr uint64_t VRHI_STATE_CULL_NONE   = 0x0000000000000000;
constexpr uint64_t VRHI_STATE_CULL_BACK   = 0x0000000000000100;
constexpr uint64_t VRHI_STATE_CULL_FRONT  = 0x0000000000000200;
constexpr uint64_t VRHI_STATE_CULL_SHIFT  = 8;
constexpr uint64_t VRHI_STATE_CULL_MASK   = 0x0000000000000300;

constexpr uint64_t VRHI_STATE_FRONT_CW    = 0x0000000000000400;

constexpr uint64_t VRHI_STATE_PT_TRIANGLES  = 0x0000000000000000;
constexpr uint64_t VRHI_STATE_PT_TRISTRIP   = 0x0001000000000000;
constexpr uint64_t VRHI_STATE_PT_LINES      = 0x0002000000000000;
constexpr uint64_t VRHI_STATE_PT_LINESTRIP  = 0x0003000000000000;
constexpr uint64_t VRHI_STATE_PT_POINTS     = 0x0004000000000000;
constexpr uint64_t VRHI_STATE_PT_SHIFT      = 48;
constexpr uint64_t VRHI_STATE_PT_MASK       = 0x0007000000000000;

constexpr uint64_t VRHI_STATE_MSAA                    = 0x0100000000000000;
constexpr uint64_t VRHI_STATE_LINEAA                  = 0x0200000000000000;
constexpr uint64_t VRHI_STATE_CONSERVATIVE_RASTER     = 0x0400000000000000;
constexpr uint64_t VRHI_STATE_NONE                    = 0x0000000000000000;
constexpr uint64_t VRHI_STATE_BLEND_INDEPENDENT       = 0x0000000400000000;
constexpr uint64_t VRHI_STATE_BLEND_ALPHA_TO_COVERAGE = 0x0000000800000000;
constexpr uint64_t VRHI_STATE_DEPTH_CLIP              = 0x0000010000000000;
constexpr uint64_t VRHI_STATE_DEPTH_TEST_ENABLE       = 0x0000020000000000;

constexpr uint64_t VRHI_STATE_DEFAULT = (
    VRHI_STATE_WRITE_RGB |
    VRHI_STATE_WRITE_A |
    VRHI_STATE_WRITE_Z |
    VRHI_STATE_DEPTH_TEST_LESS |
    VRHI_STATE_CULL_BACK |
    VRHI_STATE_MSAA );

constexpr uint64_t VRHI_STATE_MASK            = 0xffffffffffffffff;
constexpr uint64_t VRHI_STATE_DEBUG_NONE      = 0x0000000000000000;
constexpr uint64_t VRHI_STATE_DEBUG_LOG_MISSING_BINDINGS = 0x0000000000000001;
constexpr uint64_t VRHI_STATE_DEBUG_LOG_ALL_BINDINGS     = 0x0000000000000002;
constexpr uint64_t VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH = 0x0000000000000004;
constexpr uint64_t VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH = 0x0000000000000008;
constexpr uint64_t VRHI_STATE_DEBUG_ALL = 0xffffffffffffffff;

// Blend helper macros
#define VRHI_STATE_BLEND_FUNC_SEPARATE(_srcRGB, _dstRGB, _srcA, _dstA) ( UINT64_C( 0 ) \
    | ( ( ( uint64_t )( _srcRGB ) | ( ( uint64_t )( _dstRGB ) << 4 ) ) ) \
    | ( ( ( uint64_t )( _srcA ) | ( ( uint64_t )( _dstA ) << 4 ) ) << 8 ) )

#define VRHI_STATE_BLEND_EQUATION_SEPARATE(_equationRGB, _equationA) ( ( uint64_t )( _equationRGB ) | ( ( uint64_t )( _equationA ) << 3 ) )

#define VRHI_STATE_BLEND_FUNC(_src, _dst)    VRHI_STATE_BLEND_FUNC_SEPARATE( _src, _dst, _src, _dst )
#define VRHI_STATE_BLEND_EQUATION(_equation) VRHI_STATE_BLEND_EQUATION_SEPARATE( _equation, _equation )

// Predefined blend modes
constexpr uint64_t VRHI_STATE_BLEND_ADD = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE ) );

constexpr uint64_t VRHI_STATE_BLEND_ALPHA = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_SRC_ALPHA, VRHI_STATE_BLEND_INV_SRC_ALPHA ) );

constexpr uint64_t VRHI_STATE_BLEND_DARKEN = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE ) |
    VRHI_STATE_BLEND_EQUATION( VRHI_STATE_BLEND_EQUATION_MIN ) );

constexpr uint64_t VRHI_STATE_BLEND_LIGHTEN = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE ) |
    VRHI_STATE_BLEND_EQUATION( VRHI_STATE_BLEND_EQUATION_MAX ) );

constexpr uint64_t VRHI_STATE_BLEND_MULTIPLY = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_DST_COLOUR, VRHI_STATE_BLEND_ZERO ) );

constexpr uint64_t VRHI_STATE_BLEND_NORMAL = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_INV_SRC_ALPHA ) );

constexpr uint64_t VRHI_STATE_BLEND_SCREEN = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_INV_SRC_COLOUR ) );

constexpr uint64_t VRHI_STATE_BLEND_LINEAR_BURN = (
    VRHI_STATE_BLEND_FUNC( VRHI_STATE_BLEND_DST_COLOUR, VRHI_STATE_BLEND_INV_DST_COLOUR ) |
    VRHI_STATE_BLEND_EQUATION( VRHI_STATE_BLEND_EQUATION_SUB ) );

// --------------------------------------------------------------------------
// Stencil
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_STENCIL_NONE   = 0x0000000000000000;
constexpr uint64_t VRHI_STENCIL_MASK   = 0xffffffffffffffff;
constexpr uint64_t VRHI_STENCIL_DEFAULT = 0x0000000000000000;

constexpr uint64_t VRHI_STENCIL_FUNC_REF_SHIFT = 0;
constexpr uint64_t VRHI_STENCIL_FUNC_REF_MASK  = 0x00000000000000ff;
#define VRHI_STENCIL_FUNC_REF(v) ( ( ( uint64_t )( v ) << VRHI_STENCIL_FUNC_REF_SHIFT ) & VRHI_STENCIL_FUNC_REF_MASK )

constexpr uint64_t VRHI_STENCIL_FUNC_RMASK_SHIFT = 8;
constexpr uint64_t VRHI_STENCIL_FUNC_RMASK_MASK  = 0x000000000000ff00;
#define VRHI_STENCIL_FUNC_RMASK(v) ( ( ( uint64_t )( v ) << VRHI_STENCIL_FUNC_RMASK_SHIFT ) & VRHI_STENCIL_FUNC_RMASK_MASK )

constexpr uint64_t VRHI_STENCIL_FUNC_WMASK_SHIFT = 16;
constexpr uint64_t VRHI_STENCIL_FUNC_WMASK_MASK  = 0x0000000000ff0000;
#define VRHI_STENCIL_FUNC_WMASK(v) ( ( ( uint64_t )( v ) << VRHI_STENCIL_FUNC_WMASK_SHIFT ) & VRHI_STENCIL_FUNC_WMASK_MASK )

constexpr uint64_t VRHI_STENCIL_TEST_LESS     = 0x0000000001000000;
constexpr uint64_t VRHI_STENCIL_TEST_LEQUAL   = 0x0000000002000000;
constexpr uint64_t VRHI_STENCIL_TEST_EQUAL    = 0x0000000003000000;
constexpr uint64_t VRHI_STENCIL_TEST_GEQUAL   = 0x0000000004000000;
constexpr uint64_t VRHI_STENCIL_TEST_GREATER  = 0x0000000005000000;
constexpr uint64_t VRHI_STENCIL_TEST_NOTEQUAL = 0x0000000006000000;
constexpr uint64_t VRHI_STENCIL_TEST_NEVER    = 0x0000000007000000;
constexpr uint64_t VRHI_STENCIL_TEST_ALWAYS   = 0x0000000008000000;
constexpr uint64_t VRHI_STENCIL_TEST_SHIFT    = 24;
constexpr uint64_t VRHI_STENCIL_TEST_MASK     = 0x000000000f000000;

constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_ZERO     = 0x0000000000000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_KEEP     = 0x0000000010000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_REPLACE  = 0x0000000020000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INCR     = 0x0000000030000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INCRSAT  = 0x0000000040000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_DECR     = 0x0000000050000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_DECRSAT  = 0x0000000060000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INVERT   = 0x0000000070000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_SHIFT    = 28;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_MASK     = 0x00000000f0000000;

constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_ZERO     = 0x0000000000000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_KEEP     = 0x0000000100000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_REPLACE  = 0x0000000200000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INCR     = 0x0000000300000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INCRSAT  = 0x0000000400000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_DECR     = 0x0000000500000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_DECRSAT  = 0x0000000600000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INVERT   = 0x0000000700000000;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_SHIFT    = 32;
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_MASK     = 0x0000000f00000000;

constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_ZERO     = 0x0000000000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_KEEP     = 0x0000001000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_REPLACE  = 0x0000002000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INCR     = 0x0000003000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INCRSAT  = 0x0000004000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_DECR     = 0x0000005000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_DECRSAT  = 0x0000006000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INVERT   = 0x0000007000000000;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_SHIFT    = 36;
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_MASK     = 0x000000f000000000;

constexpr uint64_t VRHI_STENCIL_BACK_TEST_SHIFT      = 40;
constexpr uint64_t VRHI_STENCIL_BACK_TEST_MASK       = 0x00000f0000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_S_SHIFT = 44;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_S_MASK  = 0x0000f00000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_Z_SHIFT = 48;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_Z_MASK  = 0x000f000000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT = 52;
constexpr uint64_t VRHI_STENCIL_BACK_OP_PASS_Z_MASK  = 0x00f0000000000000;
constexpr uint64_t VRHI_STENCIL_BACK_MASK            = 0x00ffff0000000000;

#define VRHI_STENCIL_BACK_TEST(v)      ( ( ( uint64_t )( v ) << ( VRHI_STENCIL_BACK_TEST_SHIFT      - VRHI_STENCIL_TEST_SHIFT      ) ) & VRHI_STENCIL_BACK_TEST_MASK )
#define VRHI_STENCIL_BACK_OP_FAIL_S(v) ( ( ( uint64_t )( v ) << ( VRHI_STENCIL_BACK_OP_FAIL_S_SHIFT - VRHI_STENCIL_OP_FAIL_S_SHIFT ) ) & VRHI_STENCIL_BACK_OP_FAIL_S_MASK )
#define VRHI_STENCIL_BACK_OP_FAIL_Z(v) ( ( ( uint64_t )( v ) << ( VRHI_STENCIL_BACK_OP_FAIL_Z_SHIFT - VRHI_STENCIL_OP_FAIL_Z_SHIFT ) ) & VRHI_STENCIL_BACK_OP_FAIL_Z_MASK )
#define VRHI_STENCIL_BACK_OP_PASS_Z(v) ( ( ( uint64_t )( v ) << ( VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT - VRHI_STENCIL_OP_PASS_Z_SHIFT ) ) & VRHI_STENCIL_BACK_OP_PASS_Z_MASK )

// --------------------------------------------------------------------------
// Clear
// --------------------------------------------------------------------------

constexpr uint16_t VRHI_CLEAR_NONE            = 0x0000;
constexpr uint16_t VRHI_CLEAR_COLOR           = 0x0001;
constexpr uint16_t VRHI_CLEAR_DEPTH           = 0x0002;
constexpr uint16_t VRHI_CLEAR_STENCIL         = 0x0004;
constexpr uint16_t VRHI_CLEAR_UINT            = 0x2000;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_0 = 0x0008;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_1 = 0x0010;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_2 = 0x0020;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_3 = 0x0040;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_4 = 0x0080;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_5 = 0x0100;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_6 = 0x0200;
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_7 = 0x0400;
constexpr uint16_t VRHI_CLEAR_DISCARD_DEPTH   = 0x0800;
constexpr uint16_t VRHI_CLEAR_DISCARD_STENCIL = 0x1000;

constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_MASK = (
    VRHI_CLEAR_DISCARD_COLOR_0 |
    VRHI_CLEAR_DISCARD_COLOR_1 |
    VRHI_CLEAR_DISCARD_COLOR_2 |
    VRHI_CLEAR_DISCARD_COLOR_3 |
    VRHI_CLEAR_DISCARD_COLOR_4 |
    VRHI_CLEAR_DISCARD_COLOR_5 |
    VRHI_CLEAR_DISCARD_COLOR_6 |
    VRHI_CLEAR_DISCARD_COLOR_7 );

constexpr uint16_t VRHI_CLEAR_DISCARD_MASK = (
    VRHI_CLEAR_DISCARD_COLOR_MASK |
    VRHI_CLEAR_DISCARD_DEPTH |
    VRHI_CLEAR_DISCARD_STENCIL );

// --------------------------------------------------------------------------
// Variable Rate Shading
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_VRS_1X1 = 0x0; // Full resolution (default)
constexpr uint64_t VRHI_VRS_1X2 = 0x1;
constexpr uint64_t VRHI_VRS_2X1 = 0x2;
constexpr uint64_t VRHI_VRS_2X2 = 0x3;
constexpr uint64_t VRHI_VRS_2X4 = 0x4;
constexpr uint64_t VRHI_VRS_4X2 = 0x5;
constexpr uint64_t VRHI_VRS_4X4 = 0x6;

constexpr uint64_t VRHI_VRS_COMBINER_PASSTHROUGH = 0x00;
constexpr uint64_t VRHI_VRS_COMBINER_OVERRIDE    = 0x10;
constexpr uint64_t VRHI_VRS_COMBINER_MIN         = 0x20;
constexpr uint64_t VRHI_VRS_COMBINER_MAX         = 0x30;
constexpr uint64_t VRHI_VRS_COMBINER_SUM         = 0x40;

// --------------------------------------------------------------------------
// Dirty Flags & Draw Flags
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_DIRTY_WORLD            = ( 1ULL << 0 );
constexpr uint64_t VRHI_DIRTY_VERTEX_INDEX     = ( 1ULL << 1 );
constexpr uint64_t VRHI_DIRTY_CAMERA           = ( 1ULL << 2 );
constexpr uint64_t VRHI_DIRTY_PIPELINE         = ( 1ULL << 3 );
constexpr uint64_t VRHI_DIRTY_VIEWPORT         = ( 1ULL << 4 );
constexpr uint64_t VRHI_DIRTY_ATTACHMENTS      = ( 1ULL << 5 );
constexpr uint64_t VRHI_DIRTY_TEXTURE_SAMPLERS = ( 1ULL << 6 );
constexpr uint64_t VRHI_DIRTY_BUFFERS          = ( 1ULL << 7 );
constexpr uint64_t VRHI_DIRTY_CONSTANTS        = ( 1ULL << 8 );
constexpr uint64_t VRHI_DIRTY_PUSH_CONSTANTS   = ( 1ULL << 9 );
constexpr uint64_t VRHI_DIRTY_PROGRAM          = ( 1ULL << 10 );
constexpr uint64_t VRHI_DIRTY_UNIFORMS         = ( 1ULL << 11 );
constexpr uint64_t VRHI_DIRTY_VRS              = ( 1ULL << 12 );
constexpr uint64_t VRHI_DIRTY_INDIRECT         = ( 1ULL << 13 );
constexpr uint64_t VRHI_DIRTY_DEPTH_BIAS       = ( 1ULL << 14 );
constexpr uint64_t VRHI_DIRTY_ACCEL_STRUCT     = ( 1ULL << 15 );
constexpr uint64_t VRHI_DIRTY_DESCRIPTOR_TABLES = ( 1ULL << 16 );
constexpr uint64_t VRHI_DIRTY_INDIRECT_COUNT  = ( 1ULL << 17 );
constexpr uint64_t VRHI_DIRTY_ALL              = 0xFFFFFFFFFFFFFFFF;

constexpr uint32_t VRHI_DRAW_INDEXED  = ( 1u << 0 );
constexpr uint32_t VRHI_DRAW_INDIRECT = ( 1u << 1 );
constexpr uint32_t VRHI_DRAW_INDIRECT_COUNT = ( 1u << 2 );

// Render target blend helper macros (cannot be constexpr)
#define VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst) (0         \
    | ( (uint32_t)( (_src)>>VRHI_STATE_BLEND_SHIFT)       \
    | ( (uint32_t)( (_dst)>>VRHI_STATE_BLEND_SHIFT)<<4) ) \
    )

#define VRHI_STATE_BLEND_FUNC_RT_xE(_src, _dst, _equation) (0         \
    | VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst)                          \
    | ( (uint32_t)( (_equation)>>VRHI_STATE_BLEND_EQUATION_SHIFT)<<8) \
    )

#define VRHI_STATE_BLEND_FUNC_RT_1(_src, _dst)  (VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst)<< 0)
#define VRHI_STATE_BLEND_FUNC_RT_2(_src, _dst)  (VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst)<<11)
#define VRHI_STATE_BLEND_FUNC_RT_3(_src, _dst)  (VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst)<<22)

#define VRHI_STATE_BLEND_FUNC_RT_1E(_src, _dst, _equation) (VRHI_STATE_BLEND_FUNC_RT_xE(_src, _dst, _equation)<< 0)
#define VRHI_STATE_BLEND_FUNC_RT_2E(_src, _dst, _equation) (VRHI_STATE_BLEND_FUNC_RT_xE(_src, _dst, _equation)<<11)
#define VRHI_STATE_BLEND_FUNC_RT_3E(_src, _dst, _equation) (VRHI_STATE_BLEND_FUNC_RT_xE(_src, _dst, _equation)<<22)

// --------------------------------------------------------------------------
// Interface
// --------------------------------------------------------------------------

// Manually Regenerate this with py vidl.py vrhi.h src/vrhi_generated.h
// Cmake should automatically do this already.

// ************************************************************
// *** WARNING: POINTER INPUT MEMORY MANAGEMENT REQUIRED! ***
// ************************************************************
//
// VRHI uses a threaded backend. Pointer inputs (especially const char*) 
// MUST be either:
//   1. 3-frame managed delayed free, OR
//   2. Static/literal strings, OR  
//   3. Memory held until next flush/frame of backend.
//
// PASSING POINTERS TO STACK MEMORY WILL CAUSE CORRUPTION!
// const char* is NOT vmem managed!
// ************************************************************

// ------------ Device ------------

// Initialises the Vulkan RHI and starts the backend command thread.
//
// Must be called before any other RHI functions. Uses `g_vhInit` for configuration.
// If windowHandle is provided in g_vhInit, a swapchain will be created.
void vhInit( bool quiet = false );

// Shuts down the Vulkan RHI and stops the backend command thread.
//
// Cleans up all resources and waits for the GPU to finish.
void vhShutdown( bool quiet = false );

// Extracts the Vulkan driver pipeline cache into `outData`.
// Marshalled to the RHI backend thread that owns the device, so it is safe to call from any other
// thread while rendering continues; it blocks until the backend processes it, like vhFinish().
// With `flush` (the default) drains pending work and waits for the GPU first, for shutdown and
// load-time saves. Do not call from a backend or native callback. Returns false in null mode or on
// driver error; outData is cleared on failure.
bool vhGetPSOCache( std::vector< uint8_t >& outData, bool flush = true );

// Serialised size of the driver pipeline cache in bytes, without copying it.
// Marshalled to the RHI backend thread; safe from any other thread, blocks until processed.
// Diff against the last saved size to skip redundant vhGetPSOCache() saves. Returns 0 on error.
size_t vhGetPSOCacheSize();

// Returns the vhPSOCacheKey for the active device. Valid only after vhInit(); returns a zero key in
// null mode or before init. Save it alongside a vhGetPSOCache() blob and pass it back next run via
// vhInitData::psoCacheValidateKey.
vhPSOCacheKey vhGetPSOCacheKey();

// Presents the current frame and advances the swapchain.
// Returns false if the swapchain is invalid or window is resized.
// If running in headless mode, this simply flushes commands and waits if vsync-like behaviour is desired, always returning true.
bool vhFrame();

// Returns the texture handle for the current backbuffer of the swapchain.
// Returns VRHI_INVALID_HANDLE if in headless mode.
vhTexture vhGetBackbuffer();

// Waits up to timeoutNs for the present identified by presentId to complete on vrhi's swapchain.
// Returns VK_ERROR_EXTENSION_NOT_PRESENT if VK_KHR_present_wait was not enabled at init.
VkResult vhWaitForPresentKHR( uint64_t presentId, uint64_t timeoutNs );

struct vhMemoryStats
{
    uint64_t heapBudget[16] = {};    // Available budget per heap (bytes)
    uint64_t heapUsage[16] = {};     // Current usage per heap (bytes)
    uint64_t heapSize[16] = {};      // Total heap size per heap (bytes)
    uint32_t heapCount = 0;          // Number of valid heaps
    bool supported = false;          // True if VK_EXT_memory_budget is available
};

struct vhRenderStats
{
    uint64_t drawCalls = 0;      // Accumulated draw calls (direct draws count instances, indirect draws count as 1)
    uint64_t dispatchCalls = 0;  // Accumulated dispatch calls (both direct and indirect)
};

struct vhResourceBreakdown
{
    uint32_t textureCount         = 0;
    uint32_t bufferCount          = 0;
    uint32_t shaderCount          = 0;
    uint32_t accelStructCount     = 0;
    uint32_t rtPipelineCount      = 0;
    uint32_t shaderTableCount     = 0;
    uint32_t descriptorTableCount = 0;
    uint32_t timerQueryCount      = 0;
    uint32_t heapCount            = 0;

    uint32_t topLevelASCount      = 0;
    uint32_t bottomLevelASCount   = 0;

    uint64_t textureMemory        = 0;     // bytes
    uint64_t bufferMemory         = 0;     // bytes
    uint64_t accelStructMemory    = 0;     // bytes
    uint64_t heapMemory           = 0;     // bytes

    uint32_t descriptorTableSlots       = 0;
    uint32_t descriptorTableMaxCapacity = 0;
};

// Hot-path diagnostic counters. Bumped via relaxed atomics; vhPerfCheck() dumps + clears.
struct vhPerfCounters
{
    std::atomic< uint64_t > arenaOverflows       = 0;
    std::atomic< uint64_t > arenaMallocBytes     = 0;
    std::atomic< uint64_t > enqueueYields        = 0;
    std::atomic< uint64_t > enqueueRetryFloor    = 0;
    std::atomic< uint64_t > resolveCacheRebuilds = 0;
    std::atomic< uint64_t > resolveCacheHits     = 0;
    std::atomic< uint64_t > psoCacheHits         = 0;
    std::atomic< uint64_t > psoCacheMisses       = 0;
};
extern vhPerfCounters g_vhPerf;

// Logs perf counters via vhLog. No-op if all zero. reset=true zeroes them after.
void vhPerfCheck( bool reset = true );

constexpr uint32_t VRHI_UAB_SAMPLED_IMAGE               = 0x01;
constexpr uint32_t VRHI_UAB_STORAGE_IMAGE               = 0x02;
constexpr uint32_t VRHI_UAB_STORAGE_BUFFER              = 0x04;
constexpr uint32_t VRHI_UAB_UNIFORM_BUFFER              = 0x08;
constexpr uint32_t VRHI_UAB_UNIFORM_TEXEL_BUFFER        = 0x10;
constexpr uint32_t VRHI_UAB_STORAGE_TEXEL_BUFFER        = 0x20;
constexpr uint32_t VRHI_UAB_UPDATE_UNUSED_WHILE_PENDING = 0x40;

struct vhDeviceInfo
{
    std::string name;           // "NVIDIA GeForce RTX 4090 - Discrete GPU"
    std::string driver;         // "NVIDIA 546.33"
    std::string apiVersion;     // "1.3.295"
    std::string queues;         // "Graphics:0 Compute:1 Transfer:2"
    std::string summary;        // Full device information string

    bool raytracing = false;
    bool bindless = false;
    bool vrs = false;
    bool asyncCompute = false;
    bool memoryBudget = false;
    bool shaderFloat16 = false;
    bool shaderInt16 = false;
    bool presentWait = false;

    uint32_t maxTextureSize = 0;
    uint32_t maxColorAttachments = 0;
    uint32_t maxBindlessSampledImages  = 0;   // Texture_SRV, TypedBuffer_SRV
    uint32_t maxBindlessStorageImages  = 0;   // Texture_UAV, TypedBuffer_UAV
    uint32_t maxBindlessStorageBuffers = 0;   // structured / raw buffers
    uint32_t maxBindlessUniformBuffers = 0;   // ConstantBuffer
    uint32_t maxBindlessSamplers       = 0;   // Sampler
    uint32_t maxBoundDescriptorSets    = 0;   // max tables bindable at once
    uint32_t maxPerStageResources      = 0;   // aggregate across all types per stage

    uint32_t bindlessUpdateAfterBind      = 0;   // VRHI_UAB_* mask of supported update-after-bind types
    uint32_t maxBindlessSampledImagesUAB  = 0;   // Texture_SRV, TypedBuffer_SRV
    uint32_t maxBindlessStorageImagesUAB  = 0;   // Texture_UAV, TypedBuffer_UAV
    uint32_t maxBindlessStorageBuffersUAB = 0;   // structured / raw buffers
    uint32_t maxBindlessUniformBuffersUAB = 0;   // ConstantBuffer
    uint32_t maxBindlessSamplersUAB       = 0;   // Sampler
    uint64_t totalVRAM = 0;
};

extern vhDeviceInfo g_vhDeviceInfo;

// Queries GPU memory statistics using VK_EXT_memory_budget extension.
// Returns memory budget and usage per heap. If the extension is unavailable,
// only heapSize/heapCount are populated (from basic Vulkan properties).
vhMemoryStats vhStatsMemory();

// Returns current frame or previous frame completed statistics.
// Counters are reset at the start of vhFrame().
vhRenderStats vhGetStats();

// Aggregates live resource counts and per-type GPU memory totals across the backend.
// Requires a preceding vhFlush() to reflect recently enqueued create/destroy commands.
vhResourceBreakdown vhStatsBreakdown();

// Returns the total number of frames presented (or flushed in headless mode) since initialisation.
// This counter starts at 0 and increments at the end of every vhFrame() call.
uint64_t vhGetFrameNumber();

// Queries whether a specific device feature is supported.
// Returns true if the feature is supported on the current device.
//
// For features that require additional information (e.g. VRS tile size), pass an optional
// output struct via `pInfo` with size `infoSize`. See nvrhi::Feature for available features.
bool vhQueryFeatureSupport( nvrhi::Feature feature, void* pInfo = nullptr, size_t infoSize = 0 );

// Queries the supported operations for a specific texture/buffer format.
// Returns a bitmask of nvrhi::FormatSupport flags indicating what operations
// the format can be used for (e.g. RenderTarget, ShaderSample, UAV, etc).
nvrhi::FormatSupport vhQueryFormatSupport( nvrhi::Format format );

// Flushes the command queue to the backend.
//
// If `wait` is true, blocks until the backend has processed all queued commands.
// If `wait` is false, returns immediately after submitting the flush.
//
// This does not wait for the GPU to finish executing the work; use `vhFinish()` for that.
void vhFlush( bool wait = true );

// Flushes all commands and blocks until the GPU has completed all queued work.
// Use this for synchronisation points, readbacks, or before accessing GPU results.
void vhFinish();

// Enumerates every literal profile scope name VRHI may emit via fnProfileCallback.
// Use at init to pre-register names with your profiler and avoid first-call lookups on the hot path.
// Scopes emitted via VRHI_PROFILE_FUNCTION() use __FUNCTION__ and are not enumerated here.
void vhEnumerateProfileScopes( void (*cb)( const char* name, void* user ), void* user = nullptr );

// Clears backend caches (e.g. framebuffers). Call this after a window resize.
// VIDL_GENERATE
void vhResizeCleanup();

// Resizes the window swapchain to the specified dimensions.
// This performs a full GPU flush for safety. Performance is not critical during resize operations.
void vhResize( int width, int height );

// Creates an additional swapchain for a secondary native window. Returns VRHI_INVALID_SWAPCHAIN on failure.
// Inherits vsync/present-mode/format from g_vhInit. A successful call does an initial acquire so
// vhGetSwapchainBackbuffer is immediately usable. The primary swapchain (created by vhInit) is unaffected.
vhSwapchainID vhCreateSwapchain( void* windowHandle, void* displayHandle, int width, int height );

// Destroys a secondary swapchain. Refuses (asserts) on the primary swapchain.
void vhDestroySwapchain( vhSwapchainID swapchain );

// Returns the texture handle for the current backbuffer of the given swapchain.
// Returns VRHI_INVALID_HANDLE if invalid, minimized, or the swapchain is in a mode without a real surface.
vhTexture vhGetSwapchainBackbuffer( vhSwapchainID swapchain );

// Presents the current backbuffer of the given swapchain and acquires the next image.
// Independent of vhFrame(). Returns false if the swapchain is invalid or the window is in a bad state.
bool vhPresentSwapchain( vhSwapchainID swapchain );

// Resizes a secondary swapchain. width/height <= 0 marks the swapchain as minimized;
// all subsequent calls are no-ops until a positive resize is issued.
void vhResizeSwapchain( vhSwapchainID swapchain, int width, int height );

// Returns the current size of a swapchain. Returns (0,0) if the swapchain is invalid.
glm::uvec2 vhGetSwapchainSize( vhSwapchainID swapchain );

// Returns the current window size.
// This is distinct from the initial resolution and reflects the actual swapchain dimensions.
glm::uvec2 vhGetWindowSize();

// Begin GPU timing measurement for the given timer ID.
// If the timer ID has not been seen before, it will be automatically created.
// VIDL_GENERATE
void vhBeginTimerQuery( vhTimerID timerID );

// End GPU timing measurement for the given timer ID.
// VIDL_GENERATE
void vhEndTimerQuery( vhTimerID timerID );

// Get the GPU time in seconds for the given timer ID.
// Returns 0.0f if the query results are not yet available or if the timer ID is invalid.
// Note: Due to GPU/CPU latency, you should read results from N frames ago (ring buffer of VRHI_MAX_FRAMES_INFLIGHT).
float vhGetTimerQueryTime( vhTimerID timerID );

// Begins a debug marker region for GPU capture tools (RenderDoc, etc).
// `name` is the marker name to display in profiling tools.
// If g_vhInit.markers is false, this call is ignored.
// VIDL_GENERATE
void vhBeginMarker( const std::string& name );

// Ends a debug marker region.
// If g_vhInit.markers is false, this call is ignored.
// VIDL_GENERATE
void vhEndMarker();

// Begins a frame capture for GPU debugging tools.
// If g_vhInit.markers is false, this call is ignored.
// VIDL_GENERATE
void vhCaptureStart();

// Ends the frame capture.
// If g_vhInit.markers is false, this call is ignored.
// VIDL_GENERATE
void vhCaptureEnd();

// Helper to allocate memory for data upload or download.
// The caller is responsible for allocating data to feed into vh* API functions, but not responsible for freeing it.
// This is freed by the backend every flush when the commands are processed.
inline vhMem* vhAllocMem( uint64_t size )
{
    return new vhMem( size );
}

// Helper to allocate memory for data upload or download, copying the data from the provided vector.
// The caller is responsible for allocating data to feed into vh* API functions, but not responsible for freeing it.
// This is freed by the backend every flush when the commands are processed.
inline vhMem* vhAllocMem( const std::vector< uint8_t >& data )
{
    return new vhMem( data );
}

// ------------ Texture ------------

struct vhTextureMipInfo
{
    glm::ivec3 dimensions;
    int64_t size;
    int64_t offset;
    int64_t slice_size;
    int32_t pitch;
};

struct vhTexInfo
{
    nvrhi::TextureDimension target = nvrhi::TextureDimension::Texture2D;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    glm::ivec3 dimensions = glm::ivec3( 0, 0, 0 );
    int32_t arrayLayers = 0;
    int32_t mipLevels = 0;
    int32_t samples = 0;
};

// Allocates a unique texture handle.
//
// Returns a valid `vhTexture` handle, or `VRHI_INVALID_HANDLE` on failure.
vhTexture vhAllocTexture();

// Resets internal texture state without destroying the handle.
// VIDL_GENERATE
void vhResetTexture( vhTexture texture );

// Allocates a unique buffer handle.
//
// Returns a valid `vhBuffer` handle, or `VRHI_INVALID_HANDLE` on failure.
vhBuffer vhAllocBuffer();

// Resets internal buffer state without destroying the handle.
// VIDL_GENERATE
void vhResetBuffer( vhBuffer buffer );

// Enqueues a command to destroy the texture associated with `texture`.
//
// `texture` is the handle to the texture to be destroyed.
// VIDL_GENERATE
void vhDestroyTexture( vhTexture texture );

// Enqueues a command to create a texture with the specified parameters.
//
// `texture` must be a handle allocated via `vhAllocTexture`.
// `target` specifies the texture dimensionality.
// `dimensions` specifies width, height, and depth.
// `numMips` and `numLayers` specify mip count and array size.
// `numMips` can be VRHI_MIPMAP_COMPLETE to create a full mip chain down to 1x1x1.
// Use vhGetImageMaxMipCount(dimensions) to compute the resolved value synchronously.
// `format` is the pixel format.
// `flag` specifies usage and sampling options.
// `data` is optional initial pixel data. Takes ownership of the memory.
// VIDL_GENERATE
vhTexture vhCreateTexture(
    vhTexture texture,
    const char* name,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
);

// Helper to create a 2D texture.
inline vhTexture vhCreateTexture2D(
    vhTexture texture,
    const char* name,
    glm::ivec2 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    return vhCreateTexture( texture, name, nvrhi::TextureDimension::Texture2D, glm::ivec3( dimensions, 1 ), numMips, 1, format, flag, data );
}

// Helper to create a 3D texture.
inline vhTexture vhCreateTexture3D(
    vhTexture texture,
    const char* name,
    glm::ivec3 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    return vhCreateTexture( texture, name, nvrhi::TextureDimension::Texture3D, dimensions, numMips, 1, format, flag, data );
}

// Helper to create a Cube texture.
inline vhTexture vhCreateTextureCube(
    vhTexture texture,
    const char* name,
    int dimension,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    return vhCreateTexture( texture, name, nvrhi::TextureDimension::TextureCube, glm::ivec3( dimension, dimension, 1 ), numMips, 6, format, flag, data );
}

// Helper to create a 2D texture array.
inline vhTexture vhCreateTexture2DArray(
    vhTexture texture,
    const char* name,
    glm::ivec2 dimensions,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    return vhCreateTexture( texture, name, nvrhi::TextureDimension::Texture2DArray, glm::ivec3( dimensions, 1 ), numMips, numLayers, format, flag, data );
}

// Helper to create a Cube texture array.
inline vhTexture vhCreateTextureCubeArray(
    vhTexture texture,
    const char* name,
    int dimension,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    return vhCreateTexture( texture, name, nvrhi::TextureDimension::TextureCubeArray, glm::ivec3( dimension, dimension, 1 ), numMips, numLayers, format, flag, data );
}

// Enqueues a command to update a subresource range of a texture.
//
// `texture` is the handle to the texture to update.
// `startMips` and `startLayers` define the beginning of the range.
// `numMips` and `numLayers` define the size of the range.
// `numMips` can be VRHI_MIPMAP_COMPLETE to update all remaining mips from `startMips`.
// `data` contains the pixel data for the entire texture. Takes ownership of the memory.
// VIDL_GENERATE
void vhUpdateTexture(
    vhTexture texture,
    int startMips = 0, int startLayers = 0,
    int numMips = 1, int numLayers = 1,
    const vhMem* data = nullptr
);

// Enqueues a command to read a subresource range of a texture.
// WARNING: This is a slow path operation, generally for debugging or screenshot purposes.
//
// `texture` is the handle to the texture to read.
// `mip` and `layer` define the subresource to read.
// `outData` is the destination for the pixel data. DOES NOT take ownership of the memory.
// VIDL_GENERATE
void vhReadTextureSlow(
    vhTexture texture,
    int mip = 0, int layer = 0,
    vhMem* outData = nullptr
);

// Enqueues a command to blit (copy/resize/convert) a region from one texture to another.
//
// `dst` and `src` are the destination and source texture handles.
// `dstMip` and `srcMip` specify the mip levels to use.
// `dstLayer` and `srcLayer` specify the array layers to use.
// `dstOffset` and `srcOffset` are the starting coordinates in the respective textures.
// `extent` is the size of the region to blit. If width or height are <= 0, the full source mip size is used.
// Note: Destination texture must have been created with the `VRHI_TEXTURE_BLIT_DST` flag.
// VIDL_GENERATE
void vhBlitTexture(
    vhTexture dst, vhTexture src,
    int dstMip = 0, int srcMip = 0,
    int dstLayer = 0, int srcLayer = 0,
    glm::ivec3 dstOffset = glm::ivec3( 0 ),
    glm::ivec3 srcOffset = glm::ivec3( 0 ),
    glm::ivec3 extent = glm::ivec3( 0 )
);

struct vhFormatInfo
{
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    const char* name = nullptr;
    int32_t elementSize = 0;
    int32_t compressionBlockWidth = 0;
    int32_t compressionBlockHeight = 0;
};

// Returns metadata for the specified `format`.
inline vhFormatInfo vhGetFormat( nvrhi::Format format )
{
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo( format );
    vhFormatInfo out =
    {
        .format = format,
        .name = info.name,
        .elementSize = ( int32_t ) info.bytesPerBlock,
        .compressionBlockWidth = ( int32_t ) info.blockSize,
        .compressionBlockHeight = ( int32_t ) info.blockSize
    };
    return out;
}

// Returns base texture info. Optional outMipInfo for detailed layout.
vhTexInfo vhGetTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo = nullptr );

// Returns the raw NVRHI handle (nvrhi::ITexture*).
nvrhi::TextureHandle vhGetTextureNvrhiHandle( vhTexture texture );

// ------------ Buffer ------------

// Vertex layouts are defined as standard strings.
// Supported base types: float, half, int, uint, short, ushort, byte, ubyte, unorm, snorm
// Supported suffixes: 2, 3, 4
// Format: "TYPE[COUNT] [ATTRn]"
// Examples:
//   "float3" -> Location 0 (Implicit)
//   "float3 ATTR5" -> Location 5 (Explicit)
//   "float3 float2" -> Loc 0, Loc 1
//
typedef std::string vhVertexLayout;

// Validates a vertex layout string.
bool vhValidateVertexLayout( const vhVertexLayout& layout );

// Returns the stride in bytes for a valid vertex layout string.
// Returns 0 if the layout is invalid.
uint32_t vhGetVertexLayoutStride( const vhVertexLayout& layout );

// Allocates a unique buffer handle.
//
// Returns a valid `vhBuffer` handle, or `VRHI_INVALID_HANDLE` on failure.
vhBuffer vhAllocBuffer();

// `numVerts` is the number of vertices in the buffer. It is ignored if `data` is not null.
// VIDL_GENERATE
vhBuffer vhCreateVertexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    const vhVertexLayout layout,
    uint64_t numVerts = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// Enqueues a command to update a buffer with the specified data.
//
// `buffer` is the handle to the buffer to update.
// `data` is the source data. Takes ownership of the memory.
// `offsetVerts` is the vertex offset within the buffer to start writing (in vertices, not bytes).
// `numVerts` is the number of vertices in the buffer. It is ignored if `data` is not null.
// NOTE: If updating with data larger than current buffer size, VRHI_BUFFER_ALLOW_RESIZE
// must have been set during buffer creation. Without it, oversized updates will error.
// VIDL_GENERATE
void vhUpdateVertexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetVerts = 0,
    uint64_t numVerts = 0
);

// Enqueues a command to create an index buffer with the specified parameters.
//
// `buffer` must be a handle allocated via `vhAllocBuffer`.
// `name` is an optional debug name for the buffer.
// `data` is optional initial data. Takes ownership of the memory.
// `numIndices` is the number of indices. It is ignored if `data` is not null.
// `flags` specifies usage options.
// VIDL_GENERATE
vhBuffer vhCreateIndexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t numIndices = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// Enqueues a command to update an index buffer with the specified data.
//
// `buffer` is the handle to the buffer to update.
// `data` is the source data. Takes ownership of the memory.
// `offsetIndices` is the index offset within the buffer to start writing (in indices, not bytes).
// `numIndices` is the number of indices in the buffer. It is ignored if `data` is not null.
// NOTE: If updating with data larger than current buffer size, VRHI_BUFFER_ALLOW_RESIZE
// must have been set during buffer creation. Without it, oversized updates will error.
// VIDL_GENERATE
void vhUpdateIndexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetIndices = 0,
    uint64_t numIndices = 0
);

// Enqueues a command to create a uniform buffer with the specified parameters.
//
// `buffer` must be a handle allocated via `vhAllocBuffer`.
// `name` is an optional debug name for the buffer.
// `data` is optional initial data. Takes ownership of the memory.
// `size` is the size in bytes. It is ignored if `data` is not null.
// `flags` specifies usage options.
// VIDL_GENERATE
vhBuffer vhCreateUniformBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// Enqueues a command to update a uniform buffer with the specified data.
//
// NOTE: Do not use this for per-drawcall CPU updated data. While it would work, it will stall the GPU pipeline hard and you will get bad performance.
//       Either implement a ring buffer system, or use vhState's uniform mechanism which does exactly that. GPU -> GPU transfers are OK.
//
// `buffer` is the handle to the buffer to update.
// `data` is the source data. Takes ownership of the memory.
// `offset` is the byte offset within the buffer to start writing.
// `size` is the size in bytes. It is ignored if `data` is not null.
// Note: While byte-level access is supported, using 4-byte aligned offsets/sizes is recommended for performance (hits the fast-path).
// VIDL_GENERATE
void vhUpdateUniformBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t size = 0
);

// Enqueues a command to create a storage buffer with the specified parameters.
//
// `buffer` must be a handle allocated via `vhAllocBuffer`.
// `name` is an optional debug name for the buffer.
// `data` is optional initial data. Takes ownership of the memory.
// `size` is the size in bytes. It is ignored if `data` is not null.
// `flags` specifies usage options.
// `stride` is the structure stride for Structured Buffers (default 0 for Raw/ByteAddress).
// `format` is the format for Typed Buffers (default UNKNOWN).
// VIDL_GENERATE
vhBuffer vhCreateStorageBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size = 0,
    uint16_t flags = VRHI_BUFFER_COMPUTE_READ_WRITE,
    uint32_t stride = 0,
    nvrhi::Format format = nvrhi::Format::UNKNOWN
);

// Convenience wrapper for creating a structured storage buffer.
// Calls vhCreateStorageBuffer with format = UNKNOWN.
inline vhBuffer vhCreateStorageStructuredBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    uint32_t stride,
    uint16_t flags = VRHI_BUFFER_COMPUTE_READ_WRITE
)
{
    return vhCreateStorageBuffer( buffer, name, data, size, flags, stride, nvrhi::Format::UNKNOWN );
}

// Convenience wrapper for creating a typed storage buffer.
// Calls vhCreateStorageBuffer with stride = 0.
inline vhBuffer vhCreateStorageTypedBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    nvrhi::Format format,
    uint16_t flags = VRHI_BUFFER_COMPUTE_READ_WRITE
)
{
    return vhCreateStorageBuffer( buffer, name, data, size, flags, 0, format );
}

// Enqueues a command to update a storage buffer with the specified data.
//
// `buffer` is the handle to the buffer to update.
// `data` is the source data. Takes ownership of the memory.
// `offset` is the byte offset within the buffer to start writing.
// `size` is the size in bytes. It is ignored if `data` is not null.
// Note: While byte-level access is supported, using 4-byte aligned offsets/sizes is recommended for performance (hits the fast-path).
// VIDL_GENERATE
void vhUpdateStorageBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t size = 0
);

// Enqueues a command to blit (copy) a region from one buffer to another.
//
// `dst` and `src` are the destination and source buffer handles.
// `dstOffset` is the byte offset in the destination buffer to start writing.
// `srcOffset` is the byte offset in the source buffer to start reading.
// `size` is the size in bytes to copy. If 0, copies the entire source buffer.
// VIDL_GENERATE
void vhBlitBuffer(
    vhBuffer dst, vhBuffer src,
    uint64_t dstOffset = 0,
    uint64_t srcOffset = 0,
    uint64_t size = 0
);

// Enqueues a command to destroy the buffer associated with `buffer`.
//
// `buffer` is the handle to the buffer to be destroyed.
// VIDL_GENERATE
void vhDestroyBuffer( vhBuffer buffer );

// Enqueues a GPU->CPU copy via a staging buffer, then blocks until the copy is done.
// WARNING: This is a slow path operation, generally for debugging.
//
// `offset` is the byte offset within the buffer to start reading.
// `size` is the number of bytes to read. Pass 0 to read the entire buffer.
// `outData` is the destination; ownership is NOT transferred (caller holds it).
// VIDL_GENERATE
void vhReadBufferSlow( vhBuffer buffer, uint64_t offset, uint64_t size, vhMem* outData );

// Returns buffer size in bytes. 
// Options: outStride (structure stride), outFlags (usage flags).
uint64_t vhGetBufferInfo( vhBuffer buffer, uint32_t* outStride = nullptr, uint64_t* outFlags = nullptr );

// Returns the raw NVRHI handle (nvrhi::IBuffer*).
nvrhi::BufferHandle vhGetBufferNvrhiHandle( vhBuffer buffer );

// ------------ Heap Management ------------

// Allocates a unique heap handle.
// Returns VRHI_INVALID_HANDLE on failure.
vhHeap vhAllocHeap();

// Create the actual NVRHI heap.
// `size` in bytes. Returns true on success.
// VIDL_GENERATE
bool vhCreateHeap( vhHeap heap, uint64_t size, const char* name = nullptr );

// Destroy heap.
// VIDL_GENERATE
void vhDestroyHeap( vhHeap heap );

// Returns memory requirements for a texture.
// x = size, y = alignment.
// Using u64vec2 to support >2GB resources.
// Requires the texture to exist on the backend (call vhFlush after vhCreateTexture).
// Returns {0, 0} if texture not found or invalid.
glm::u64vec2 vhGetTextureMemoryRequirements( vhTexture texture );

// Binds a virtual texture to a specific heap at a specific offset.
// VIDL_GENERATE
void vhBindTextureMemory( vhTexture texture, vhHeap heap, uint64_t offset );

// Allocates from heap with specified size and alignment.
// Returns { offset, size } where offset is aligned to the alignment.
// Requires the heap to exist on the backend (call vhFlush first).
// Returns {0, 0} on failure (heap full, invalid handles, etc).
glm::u64vec2 vhHeapAlloc( vhHeap heap, uint64_t size, uint64_t alignment );

// Frees a previous heap allocation by offset.
void vhHeapFree( vhHeap heap, uint64_t offset );

// Allocates from heap and binds texture in one call.
// Equivalent to calling vhGetTextureMemoryRequirements then vhHeapAlloc and vhBindTextureMemory.
// Returns { offset, size } or {0, 0} on failure.
glm::u64vec2 vhAllocBindTextureMemory( vhTexture texture, vhHeap heap );

// Returns memory requirements for a buffer.
// x = size, y = alignment.
// Using u64vec2 to support >2GB resources.
// Requires the buffer to exist on the backend (call vhFlush after vhCreate*Buffer).
// Returns {0, 0} if buffer not found or invalid.
glm::u64vec2 vhGetBufferMemoryRequirements( vhBuffer buffer );

// Returns memory requirements for an acceleration structure.
// x = size, y = alignment.
// Requires the AS to exist on the backend (call vhFlush after vhCreateAS).
// Returns {0, 0} if as not found or invalid.
glm::u64vec2 vhGetAccelStructMemoryRequirements( vhAccelStruct as );

// Binds a virtual buffer to a specific heap at a specific offset.
// The buffer must have been created with VRHI_BUFFER_VIRTUAL flag.
// VIDL_GENERATE
void vhBindBufferMemory( vhBuffer buffer, vhHeap heap, uint64_t offset );

// Allocates from heap and binds buffer in one call.
// Equivalent to calling vhGetBufferMemoryRequirements then vhHeapAlloc and vhBindBufferMemory.
// Returns { offset, size } or {0, 0} on failure.
glm::u64vec2 vhAllocBindBufferMemory( vhBuffer buffer, vhHeap heap );

// ------------ Raytracing ------------

// Allocates a unique acceleration structure handle.
//
// Returns a valid `vhAccelStruct` handle, or `VRHI_INVALID_HANDLE` on failure.
vhAccelStruct vhAllocAS();

// Allocates a unique raytracing pipeline handle.
//
// Returns a valid `vhRTPipeline` handle, or `VRHI_INVALID_HANDLE` on failure.
vhRTPipeline vhAllocRTPipeline();


// Allocates a unique shader table handle.
//
// Returns a valid `vhShaderTable` handle, or `VRHI_INVALID_HANDLE` on failure.
vhShaderTable vhAllocShaderTable();

// Enqueues a command to create an acceleration structure from a NVRHI descriptor.
//
// `as` must be a handle allocated via `vhAllocAS`.
// `desc` is the NVRHI acceleration structure descriptor (BLAS or TLAS).
// VIDL_GENERATE
vhAccelStruct vhCreateAS( vhAccelStruct as, const nvrhi::rt::AccelStructDesc& desc );

// Enqueues a command to destroy the acceleration structure.
//
// `as` is the handle to the acceleration structure to be destroyed.
// VIDL_GENERATE
void vhDestroyAS( vhAccelStruct as );

// Enqueues a command to build or rebuild a bottom-level acceleration structure.
//
// `blas` is the handle to the BLAS.
// `geometries` is an array of NVRHI geometry descriptors.
// `geometryCount` is the number of geometries.
// VIDL_GENERATE
void vhBuildBLAS( vhAccelStruct blas, std::vector< nvrhi::rt::GeometryDesc > geometries );

// Enqueues a command to build a bottom-level acceleration structure from indexed triangle geometry.
//
// Carries vhBuffer handles through the command queue; the backend thread resolves them after FIFO
// processing. No vhFlush() barrier is needed between buffer/AS creation and this call.
//
// `blas` is the handle to the BLAS (must be created with vhCreateAS as non-top-level).
// `vertexBuffer` is a GPU buffer containing vertex data.
// `indexBuffer` is a GPU buffer containing index data, or VRHI_INVALID_HANDLE for non-indexed geometry.
//   When VRHI_INVALID_HANDLE, `indexFormat` and `indexCount` are ignored (treated as UNKNOWN / 0).
// `vertexFormat` is the format of the vertex position (e.g. nvrhi::Format::RGB32_FLOAT).
// `indexFormat` is the format of indices (e.g. nvrhi::Format::R32_UINT); ignored if non-indexed.
// `vertexStride` is the byte stride between vertices in the vertex buffer.
// `vertexCount` is the number of vertices.
// `indexCount` is the number of indices; ignored if non-indexed.
// `flags` are geometry flags (e.g. nvrhi::rt::GeometryFlags::Opaque).
//
// Vertex/index buffer offsets default to 0; to build from a sub-range of a shared buffer use
// vhBuildBLAS with an explicit nvrhi::rt::GeometryDesc instead.
// VIDL_GENERATE
void vhBuildIndexedTriangleBLAS( vhAccelStruct blas, vhBuffer vertexBuffer, vhBuffer indexBuffer, nvrhi::Format vertexFormat, nvrhi::Format indexFormat, uint32_t vertexStride, uint32_t vertexCount, uint32_t indexCount, nvrhi::rt::GeometryFlags flags );

// Enqueues a command to build or rebuild a top-level acceleration structure.
//
// `tlas` is the handle to the TLAS.
// `instances` is an array of NVRHI instance descriptors.
// `buildFlags` are optional per-build flags ORed with the flags stored in the AccelStructDesc.
// VIDL_GENERATE
void vhBuildTLAS( vhAccelStruct tlas, std::vector< nvrhi::rt::InstanceDesc > instances, nvrhi::rt::AccelStructBuildFlags buildFlags = nvrhi::rt::AccelStructBuildFlags::None );

// Enqueues a command to compact all eligible bottom-level acceleration structures.
//
// Only BLASes created with AccelStructBuildFlags::AllowCompaction are eligible.
// VIDL_GENERATE
void vhCompactBLAS();

// Enqueues a command to build a top-level acceleration structure from a GPU instance buffer.
//
// `tlas` is the handle to the TLAS.
// `instanceBuffer` is a GPU buffer containing nvrhi::rt::InstanceDesc entries.
// `numInstances` is the number of instances in the buffer.
// VIDL_GENERATE
void vhBuildTLASFromBuffer( vhAccelStruct tlas, vhBuffer instanceBuffer, uint32_t numInstances );

// Enqueues a command to create a raytracing pipeline from a NVRHI descriptor.
//
// `pipeline` must be a handle allocated via `vhAllocRTPipeline`.
// `desc` is the NVRHI raytracing pipeline descriptor.
// VIDL_GENERATE
vhRTPipeline vhCreateRTPipeline( vhRTPipeline pipeline, const nvrhi::rt::PipelineDesc& desc );

// VIDL_GENERATE
vhRTPipeline vhCreateRTPipelineSimple( vhRTPipeline pipeline, vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit, vhShader intersection, uint32_t maxPayloadSize, uint32_t maxAttributeSize, uint32_t maxRecursionDepth );

inline void vhCreateRTPipeline( vhRTPipeline pipeline, vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit = VRHI_INVALID_HANDLE, vhShader intersection = VRHI_INVALID_HANDLE, uint32_t maxPayloadSize = 16, uint32_t maxAttributeSize = 8, uint32_t maxRecursionDepth = 1 )
{
    vhCreateRTPipelineSimple( pipeline, rayGen, miss, closestHit, anyHit, intersection, maxPayloadSize, maxAttributeSize, maxRecursionDepth );
}

// Enqueues a command to destroy the raytracing pipeline.
//
// `pipeline` is the handle to the raytracing pipeline to be destroyed.
// VIDL_GENERATE
void vhDestroyRTPipeline( vhRTPipeline pipeline );

// --------------------------------------------------------------------------
// Bindless Descriptor Tables
// --------------------------------------------------------------------------

// Allocate a handle, create the table, populate slots, bind via vhState::SetDescriptorTable. Shader side: see VRHI_BINDLESS_SPACE.
vhDescriptorTable vhAllocDescriptorTable();

// VIDL_GENERATE
vhDescriptorTable vhCreateDescriptorTable( vhDescriptorTable table, const nvrhi::BindlessLayoutDesc& desc );

// VIDL_GENERATE
void vhDestroyDescriptorTable( vhDescriptorTable table );

// VIDL_GENERATE
void vhResizeDescriptorTable( vhDescriptorTable table, uint32_t newSize, bool keepContents = true );

void vhDescriptorTableSetTexture( vhDescriptorTable table, uint32_t index, vhTexture texture,
    nvrhi::Format format = nvrhi::Format::UNKNOWN,
    nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources,
    bool computeUAV = false );

void vhDescriptorTableSetBuffer( vhDescriptorTable table, uint32_t index, vhBuffer buffer,
    nvrhi::ResourceType type,
    uint64_t byteOffset = 0, uint64_t byteSize = 0,
    nvrhi::Format format = nvrhi::Format::UNKNOWN );

void vhDescriptorTableClear( vhDescriptorTable table, uint32_t index, nvrhi::ResourceType type );

uint32_t vhDescriptorTableCapacity( vhDescriptorTable table );
nvrhi::DescriptorTableHandle vhGetDescriptorTableNvrhiHandle( vhDescriptorTable table );
nvrhi::BindingLayoutHandle vhGetDescriptorTableLayoutNvrhiHandle( vhDescriptorTable table );

// Internal: writes one slot. Use the vhDescriptorTableSet* wrappers instead.
// VIDL_GENERATE
void vhCmdWriteDescriptorTable( vhDescriptorTable table, uint32_t resource, bool isBuffer, nvrhi::BindingSetItem item );

// Creates a table holding one unbounded array of `type` at register `slot`, visible to all stages.
inline vhDescriptorTable vhCreateDescriptorTableSimple( nvrhi::ResourceType type, uint32_t maxCapacity, uint32_t slot = 0, bool updateAfterBind = false )
{
    vhDescriptorTable table = vhAllocDescriptorTable();
    if ( table == VRHI_INVALID_HANDLE ) return VRHI_INVALID_HANDLE;

    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = maxCapacity;
    desc.updateAfterBind = updateAfterBind;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem{}.setType( type ).setSlot( slot ) );
    return vhCreateDescriptorTable( table, desc );
}

// Optional slot index allocator over a table's free list. When growable, Alloc() doubles the table
// (up to maxCapacity) on overflow; otherwise it returns VRHI_INVALID_HANDLE when full.
// Free() defers reuse until Step() has run kDeferredDepth times; call Step() once per frame.
struct vhBindlessAllocator
{
    static constexpr uint32_t kDeferredDepth = 3; // frames-in-flight; a freed slot is held this many Step()s

    vhDescriptorTable table = VRHI_INVALID_HANDLE;
    uint32_t capacity = 0;
    uint32_t maxCapacity = 0;
    uint32_t highWater = 0;
    std::vector< uint32_t > freeList;
    std::vector< uint32_t > deferredFrees[kDeferredDepth];
    uint32_t frameIndex = 0;
    bool growable = false;

    uint32_t Alloc()
    {
        if ( !freeList.empty() )
        {
            uint32_t idx = freeList.back();
            freeList.pop_back();
            return idx;
        }
        if ( highWater >= capacity )
        {
            if ( !growable || capacity >= maxCapacity ) return VRHI_INVALID_HANDLE;
            uint32_t newCap = capacity ? capacity * 2 : 16;
            if ( newCap > maxCapacity ) newCap = maxCapacity;
            vhResizeDescriptorTable( table, newCap, true );
            capacity = newCap;
        }
        return highWater++;
    }

    void Free( uint32_t index ) { deferredFrees[frameIndex].push_back( index ); }

    void Step()
    {
        frameIndex = ( frameIndex + 1 ) % kDeferredDepth;
        std::vector< uint32_t >& bucket = deferredFrees[frameIndex];
        freeList.insert( freeList.end(), bucket.begin(), bucket.end() );
        bucket.clear();
    }

    void Reset()
    {
        highWater = 0;
        freeList.clear();
        for ( uint32_t i = 0; i < kDeferredDepth; i++ ) deferredFrees[i].clear();
        frameIndex = 0;
    }

    uint32_t Allocated() const
    {
        uint32_t deferred = 0;
        for ( uint32_t i = 0; i < kDeferredDepth; i++ ) deferred += ( uint32_t ) deferredFrees[i].size();
        return highWater - ( uint32_t ) freeList.size() - deferred;
    }
};

// --------------------------------------------------------------------------
// Native Vulkan Interop (FSR / DLSS / etc.)
// --------------------------------------------------------------------------

// Pre-resolved Vulkan objects handed to the callback. cmdbuf is vrhi's open graphics primary — record into it, do not submit or close it.
struct vhNativeContext
{
    VkCommandBuffer         cmdbuf;
    VkDevice                device;
    VkPhysicalDevice        physicalDevice;
    VkInstance              instance;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
    VkQueue                 graphicsQueue;
    uint32_t                graphicsQueueFamily;
    VkQueue                 computeQueue;
    uint32_t                computeQueueFamily;
    uint32_t                frameIndex;     // verbatim from vhExecuteNative call
};

// Per-resource entry. Declare stateBefore/stateAfter; backend fills image/view/format/dims. Texture must not be in a permanent NVRHI state. UAV outputs require VRHI_TEXTURE_COMPUTE_WRITE.
struct vhNativeResource
{
    vhTexture             texture     = VRHI_INVALID_HANDLE;
    nvrhi::ResourceStates stateBefore = nvrhi::ResourceStates::ShaderResource;
    nvrhi::ResourceStates stateAfter  = nvrhi::ResourceStates::ShaderResource;
    // backend-filled:
    VkImage               image       = VK_NULL_HANDLE;
    VkImageView           view        = VK_NULL_HANDLE;
    VkFormat              format      = VK_FORMAT_UNDEFINED;
    uint32_t              width = 0, height = 0, mipLevels = 0, arraySize = 0;
};

typedef void (*vhNativeExecuteFn)( const vhNativeContext& ctx, vhNativeResource* resources, uint32_t resourceCount, void* user );

// Borrows vrhi's open graphics command buffer, transitions resources to stateBefore, invokes fn, then re-baselines NVRHI's tracker to stateAfter.
// VIDL_GENERATE
// VIDL_STORAGE: resources = vhArenaSpan< vhNativeResource >
void vhExecuteNative( vhNativeExecuteFn fn, void* user, uint32_t frameIndex, const std::vector< vhNativeResource >& resources );

VkDevice vhGetVkDevice();
VkPhysicalDevice vhGetVkPhysicalDevice();
VkInstance vhGetVkInstance();

// Lock if you're using VkDevice, VkPhysicalDevice or VkInstance outside a native callback. Otherwise dont touch.
struct vhNativeDeviceLock
{
    vhNativeDeviceLock();
    ~vhNativeDeviceLock();
    vhNativeDeviceLock( const vhNativeDeviceLock& ) = delete;
    vhNativeDeviceLock& operator=( const vhNativeDeviceLock& ) = delete;
};

// Allocates a client-side shader table handle.
//
// Returns a `vhShaderTable` handle.
vhShaderTable vhAllocShaderTable();

// Enqueues a command to create a shader table for a raytracing pipeline.
//
// `table` is the handle allocated via `vhAllocShaderTable`.
// `pipeline` is the raytracing pipeline handle.
// VIDL_GENERATE
vhShaderTable vhCreateShaderTable( vhShaderTable table, vhRTPipeline pipeline );

// Enqueues a command to destroy a shader table.
//
// `table` is the handle to the shader table to be destroyed.
// VIDL_GENERATE
void vhDestroyShaderTable( vhShaderTable table );

// Enqueues a command to set the ray generation shader in a shader table.
//
// `table` is the shader table handle.
// `exportName` is the raygen shader export name from the pipeline.
// Shader resources are bound via the state's texture, buffer, and sampler bindings.
// VIDL_GENERATE
void vhShaderTableSetRayGen( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet = nullptr );

// Enqueues a command to add a miss shader to a shader table.
//
// `table` is the shader table handle.
// `exportName` is the miss shader export name from the pipeline.
// Shader resources are bound via the state's texture, buffer, and sampler bindings.
// VIDL_GENERATE
void vhShaderTableAddMiss( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet = nullptr );

// Enqueues a command to add a hit group to a shader table.
//
// `table` is the shader table handle.
// `exportName` is the hit group export name from the pipeline.
// Shader resources are bound via the state's texture, buffer, and sampler bindings.
// VIDL_GENERATE
void vhShaderTableAddHitGroup( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet = nullptr );

// Enqueues a command to add a callable shader to a shader table.
//
// `table` is the shader table handle.
// `exportName` is the callable shader export name from the pipeline.
// Shader resources are bound via the state's texture, buffer, and sampler bindings.
// VIDL_GENERATE
void vhShaderTableAddCallable( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet = nullptr );

// Enqueues a command to dispatch rays.
//
// `stateID` is the state to use for binding resources.
// `table` is the shader table handle.
// `args` is the NVRHI dispatch rays arguments (dimensions, etc).
// VIDL_GENERATE
void vhDispatchRays( vhStateId stateID, vhShaderTable table, const nvrhi::rt::DispatchRaysArguments& args );

// Returns the raw NVRHI handle (nvrhi::rt::IAccelStruct*).
nvrhi::rt::AccelStructHandle vhGetASNvrhiHandle( vhAccelStruct as );

// Returns the raw NVRHI handle (nvrhi::rt::IPipeline*).
nvrhi::rt::PipelineHandle vhGetRTPipelineNvrhiHandle( vhRTPipeline pipeline );

// Returns the raw NVRHI handle (nvrhi::rt::IShaderTable*).
nvrhi::rt::ShaderTableHandle vhGetShaderTableNvrhiHandle( vhShaderTable table );

// ------------ Shaders ------------

// Allocates a unique shader handle.
//
// Returns a valid `vhShader` handle, or `VRHI_INVALID_HANDLE` on failure.
vhShader vhAllocShader();

struct vhReflectionMember
{
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0; 
};

struct vhShaderReflectionResource
{
    std::string name;
    uint32_t slot = 0;
    uint32_t set = 0;
    nvrhi::ResourceType type = nvrhi::ResourceType::None;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    nvrhi::TextureDimension dim = nvrhi::TextureDimension::Unknown;
    uint32_t arraySize = 0;
    bool bindless = false;
    uint32_t sizeInBytes = 0; // Validation
    std::vector< vhReflectionMember > members;
    uint64_t membersHash = 0; // Cached hash of members, computed at shader load time
};

struct vhPushConstantRange
{
    uint32_t offset = 0;
    uint32_t size = 0;
    std::string name;
};

struct vhSpecConstant
{
    uint32_t id = 0;
    std::string name;
};

// Returns thread group size (for Compute/Mesh/Amp) via out pointer.
// Optional out pointers to populate full reflection data.
void vhGetShaderInfo(
    vhShader shader,
    glm::uvec3* outGroupSize = nullptr,
    std::vector< vhShaderReflectionResource >* outResources = nullptr,
    std::vector< vhPushConstantRange >* outPushConstants = nullptr,
    std::vector< vhSpecConstant >* outSpecConstants = nullptr
);

// Returns the raw NVRHI handle (nvrhi::IShader*).
nvrhi::ShaderHandle vhGetShaderNvrhiHandle( vhShader shader );

// Compiles shader source code to SPIR-V bytecode using Slang.
//
// Optimise a SPIRV binary using SPIRV-Tools from the Vulkan SDK.
// This is automatically called by vhCompileShader() when compiling
// with optimisation enabled (i.e., without VRHI_SHADER_DEBUG flag).
// 
// Parameters:
//   spirv - Vector containing SPIRV binary. Will be replaced with 
//           optimised version on success.
//
// Returns:
//   true if optimisation succeeded, false otherwise.
//   On failure, spirv is unchanged.
//
// Note: This function requires SPIRV-Tools libraries from the Vulkan SDK.
bool vhOptimiseSpirv( std::vector< uint32_t >& spirv );

// `name` is an optional debug name for the shader.
// `source` is the HLSL/GLSL source code to compile.
// `flags` specifies shader stage and compile options (VRHI_SHADER_STAGE_*, VRHI_SHADER_DEBUG, etc.).
// `outSpirv` receives the compiled SPIR-V bytecode.
// `entry` is the entry point function name (default: "main").
// `defines` are preprocessor definitions to pass to the compiler.
// `includes` are additional include directories for shader compilation.
// `outError` receives error messages if compilation fails.
// Returns true on success, false on failure.
bool vhCompileShader(
    const char* name,
    const char* source,
    uint64_t flags,
    std::vector< uint32_t >& outSpirv,
    const char* entry = "main",
    const std::vector< std::string >& defines = {},
    const std::vector< std::string >& includes = {},
    std::string* outError = nullptr
);

// Enqueues a command to create a shader from SPIR-V bytecode.
//
// `flags` is one of VRHI_SHADER_STAGE_*.
// VIDL_GENERATE
vhShader vhCreateShader(
    vhShader shader,
    const char* name,
    uint64_t flags,
    const std::vector< uint32_t >& spirv,
    const char* entry = "main"
);

// Destroy shader.
// VIDL_GENERATE
void vhDestroyShader( vhShader shader );

// Graphics: Standard (Vertex + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader pixelShader )
{
    return { vertexShader, pixelShader };
}

// Graphics: Geometry (Vertex + Geometry + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader geometryShader, vhShader pixelShader )
{
    return { vertexShader, geometryShader, pixelShader };
}

// Graphics: Tessellation (Vertex + Hull + Domain + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader hullShader, vhShader domainShader, vhShader pixelShader )
{
    return { vertexShader, hullShader, domainShader, pixelShader };
}

// Graphics: Full Pipeline (Vertex + Hull + Domain + Geometry + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader hullShader, vhShader domainShader, vhShader geometryShader, vhShader pixelShader )
{
    return { vertexShader, hullShader, domainShader, geometryShader, pixelShader };
}

// Compute
inline vhProgram vhCreateComputeProgram( vhShader computeShader )
{
    return { computeShader };
}

// Mesh Shading: Basic (Mesh + Pixel)
inline vhProgram vhCreateMeshProgram( vhShader meshShader, vhShader pixelShader )
{
    return { meshShader, pixelShader };
}

// Mesh Shading: Amplified (Amplification + Mesh + Pixel)
inline vhProgram vhCreateMeshProgram( vhShader amplificationShader, vhShader meshShader, vhShader pixelShader )
{
    return { amplificationShader, meshShader, pixelShader };
}

// Raytracing: Simple (RayGen + Miss + ClosestHit)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit )
{
    return { rayGen, miss, closestHit };
}

// Raytracing: With AnyHit (RayGen + Miss + ClosestHit + AnyHit)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit )
{
    return { rayGen, miss, closestHit, anyHit };
}

// Raytracing: Full Hit Group (RayGen + Miss + ClosestHit + AnyHit + Intersection)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit, vhShader intersection )
{
    return { rayGen, miss, closestHit, anyHit, intersection };
}

// Pre-compile a graphics PSO into the internal PSO cache. No draw required.
// stateFlags: VRHI_STATE_* bitmask (depth, blend, cull, prim type, etc.)
// vertexLayout: layout string e.g. "float3 half4 half4 half4 half4 half4".
//               Empty for vertex shaders with no inputs (SV_VertexID only).
// colorFormats: render target formats (empty for depth-only).
// depthFormat: depth attachment format (nvrhi::Format::UNKNOWN for no depth).
// sampleCount: MSAA sample count (must be > 0, 1 for no MSAA).
// Fire-and-forget. Call vhFinish() to ensure commands are processed.
// VIDL_GENERATE
void vhPrecompilePSO(
    vhProgram program,
    uint64_t stateFlags,
    const vhVertexLayout& vertexLayout,
    const std::vector< nvrhi::Format >& colorFormats,
    nvrhi::Format depthFormat,
    uint32_t sampleCount
);

// Pre-compile a compute PSO into the internal PSO cache. No dispatch required.
// Fire-and-forget. Call vhFinish() to ensure commands are processed.
inline void vhPrecompilePSO( vhProgram program )
{
    vhPrecompilePSO( std::move( program ), 0, "", {}, nvrhi::Format::UNKNOWN, 1 );
}

// ------------ State ------------

typedef uint64_t vhStateId;
typedef uint64_t vhFramebuffer;

// This represents the entire draw state.
// You can submit multiple draw calls or compute dispatches with the same state.
// This is intended to be created globally and stored for the duration of the application.
//
// WARNING: Modifying the members of this struct directly will NOT set the appropriate 
// dirty flags. Use the provided Set* helper functions to ensure the backend is 
// notified of changes. Direct modification is only recommended if you manually 
// call DirtyAll() before use.
//
struct vhState
{
    glm::vec4 viewRect = glm::vec4( 0.0f, 0.0f, 1.0f, 1.0f );
    glm::vec4 viewScissor = glm::vec4( 0.0f, 0.0f, -1.0f, -1.0f );
    glm::mat4 viewMatrix = glm::mat4( 1.0f );
    glm::mat4 projMatrix = glm::mat4( 1.0f );
    std::vector< glm::mat4 > worldMatrix; // worldMatrix[0] is copied into pushConstants[0] if worldMatrix is non-empty.
    uint64_t stateFlags = 0;
    uint64_t debugFlags = 0;
    uint64_t dirty = 0;

    uint16_t clearFlags = 0;
    glm::vec4 clearColor = glm::vec4( 0.0f );
    glm::u8vec4 clearColorUInt = glm::u8vec4( 0 );
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
    uint64_t stencilState = 0;

    glm::vec4 pushConstants = glm::vec4( 0.0f, 0.0f, 0.0f, 0.0f );
    
    // Blend constant colour (for blend modes that reference constant colour)
    glm::vec4 blendConstantColor = glm::vec4( 0.0f );

    // Viewport depth range (minZ, maxZ) - defaults to standard 0.0 to 1.0 range
    glm::vec2 viewDepthRange = glm::vec2( 0.0f, 1.0f );

    // Depth bias for shadow mapping and depth fighting prevention
    int depthBias = 0;
    float depthBiasClamp = 0.0f;
    float slopeScaledDepthBias = 0.0f;

    // Variable Rate Shading
    uint32_t shadingRateFlags = VRHI_VRS_1X1;
    vhTexture shadingRateImage = VRHI_INVALID_HANDLE;

    // Indirect draw parameters
    struct IndirectParams
    {
        vhBuffer buffer = VRHI_INVALID_HANDLE;
        uint64_t byteOffset = 0;
    };
    IndirectParams indirectParams;

    struct IndirectCountBufferParams
    {
        vhBuffer buffer = VRHI_INVALID_HANDLE;
        uint64_t byteOffset = 0;
    };
    IndirectCountBufferParams indirectCountBuffer;

    struct VertexBinding
    {
        vhBuffer buffer = VRHI_INVALID_HANDLE;
        uint8_t stream = 0;
        uint32_t startVertex = 0;
        uint32_t numVertices = UINT32_MAX;
        uint64_t byteOffset = 0;

        // Advanced: optional vertex layout override for this binding.
        // This override lets you specify a different vertex layout at bind time,
        // but you must manage stride and memory layout yourself. The buffer's original stride is ignored;
        // ensure your data matches the override layout.
        vhVertexLayout layoutOverride = "";

        // Set to true if this buffer contains per-instance data (VK_VERTEX_INPUT_RATE_INSTANCE).
        // All attributes in this buffer slot will be treated as instanced.
        // Must match the :i suffix in the layout string if both are specified.
        bool isInstanced = false;
    };
    std::vector< VertexBinding > vertexBindings;

    struct IndexBinding
    {
        vhBuffer buffer = VRHI_INVALID_HANDLE;
        uint32_t firstIndex = 0;
        uint32_t numIndices = UINT32_MAX;
        uint64_t byteOffset = 0;
    };
    IndexBinding indexBinding;

    struct TextureBinding
    {
        const char* name = nullptr; // Setting this will autofill slot and computeUAV.
        int32_t slot = -1;
        vhTexture texture = VRHI_INVALID_HANDLE;
        nvrhi::Format formatOverride = nvrhi::Format::UNKNOWN;
        nvrhi::TextureSubresourceSet subresources = nvrhi::TextureSubresourceSet( 0, nvrhi::TextureSubresourceSet::AllMipLevels, 0, nvrhi::TextureSubresourceSet::AllArraySlices );
        nvrhi::TextureDimension dimensionOverride = nvrhi::TextureDimension::Unknown;
        bool computeUAV = false;
    };
    std::vector< TextureBinding > textures;

    struct BufferBinding
    {
        const char* name = nullptr; // Setting this will autofill slot and computeUAV.
        int32_t slot = -1;
        vhBuffer buffer = VRHI_INVALID_HANDLE;
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
        bool computeUAV = false;
    };
    std::vector< BufferBinding > buffers;

    struct AccelStructBinding
    {
        vhAccelStruct as = VRHI_INVALID_HANDLE;
        int32_t slot = -1;
        const char* name = nullptr;
    };
    std::vector< AccelStructBinding > accelStructs;

    struct DescriptorTableBinding
    {
        vhDescriptorTable table = VRHI_INVALID_HANDLE;
        uint32_t registerSpace = 0;
    };
    std::vector< DescriptorTableBinding > descriptorTables;

    struct SamplerDefinition
    {
        const char* name = nullptr; // Setting this will autofill slot.
        int32_t slot = -1;
        uint64_t flags = 0;
    };
    std::vector< SamplerDefinition > samplers;

    // WARNING: These are global values. You cannot write them mid-frame, behaviour is undefined.
    struct ConstantBufferValue
    {
        const char* name = nullptr;
        std::vector< glm::vec4 > data;
    };
    std::vector< ConstantBufferValue > constants;

    // These are per-drawcall values. You can write them mid-frame and they are efficiently uploaded.
    // This is probably what you're looking for.
    struct UniformBufferValue
    {
        const char* name = nullptr;
        std::vector< glm::vec4 > data;
    };
    std::vector< UniformBufferValue > uniforms;

    vhProgram program;


    struct RenderTarget
    {
        vhTexture texture = VRHI_INVALID_HANDLE;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
        nvrhi::Format formatOverride = nvrhi::Format::UNKNOWN;
        bool readOnly = false;

        RenderTarget() {}
    };

    std::vector< RenderTarget > colourAttachment;
    RenderTarget depthAttachment;

    vhState& SetViewRect( const glm::vec4& rect ) { viewRect = rect; dirty |= VRHI_DIRTY_VIEWPORT; return *this; }
    vhState& SetViewScissor( const glm::vec4& scissor ) { viewScissor = scissor; dirty |= VRHI_DIRTY_VIEWPORT; return *this; }
    vhState& SetViewClear( uint16_t clearFlags_, const glm::vec4& color = glm::vec4( 0.0f ), float depth = 1.0f, uint8_t stencil = 0 )
    {
        clearFlags = clearFlags_;
        clearColor = color;
        clearDepth = depth;
        clearStencil = stencil;
        clearColorUInt = glm::u8vec4( 0 );
        dirty |= VRHI_DIRTY_VIEWPORT;
        return *this;
    }

    vhState& SetViewClear( uint16_t clearFlags_, const glm::u8vec4& color, float depth = 1.0f, uint8_t stencil = 0 )
    {
        clearFlags = clearFlags_;
        clearColorUInt = color;
        clearDepth = depth;
        clearStencil = stencil;
        clearColor = glm::vec4( 0.0f );
        dirty |= VRHI_DIRTY_VIEWPORT;
        return *this;
    }

    vhState& SetClearColor( const glm::vec4& color )
    {
        clearColor = color;
        dirty |= VRHI_DIRTY_VIEWPORT;
        return *this;
    }

    vhState& SetClearColorUInt( const glm::u8vec4& color )
    {
        clearColorUInt = color;
        dirty |= VRHI_DIRTY_VIEWPORT;
        return *this;
    }
    vhState& SetViewTransform( const glm::mat4& view, const glm::mat4& proj )
    {
        viewMatrix = view;
        projMatrix = proj;
        dirty |= VRHI_DIRTY_CAMERA;
        return *this;
    }
    vhState& SetWorldTransform( const glm::mat4& mtx, uint16_t num = 1 )
    {
        worldMatrix.assign( num, mtx );
        dirty |= VRHI_DIRTY_WORLD;
        return *this;
    }
    vhState& SetStateFlags( uint64_t flags ) { stateFlags = flags; dirty |= VRHI_DIRTY_PIPELINE; return *this; }
    vhState& SetDebugFlags( uint64_t flags ) { debugFlags = flags; dirty |= VRHI_DIRTY_PIPELINE; return *this; }
    vhState& SetStencil( uint64_t state )
    {
        stencilState = state;
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetStencil( uint8_t ref, uint8_t readMask, uint8_t writeMask, uint64_t frontTest, uint64_t frontFailOp, uint64_t frontDepthFailOp, uint64_t frontDepthPassOp )
    {
        stencilState = VRHI_STENCIL_FUNC_REF( ref ) |
                       VRHI_STENCIL_FUNC_RMASK( readMask ) |
                       VRHI_STENCIL_FUNC_WMASK( writeMask ) |
                       frontTest | frontFailOp | frontDepthFailOp | frontDepthPassOp;
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetStencil( uint8_t ref, uint8_t readMask, uint8_t writeMask,
        uint64_t frontTest, uint64_t frontFailOp, uint64_t frontDepthFailOp, uint64_t frontDepthPassOp,
        uint64_t backTest, uint64_t backFailOp, uint64_t backDepthFailOp, uint64_t backDepthPassOp )
    {
        stencilState = VRHI_STENCIL_FUNC_REF( ref ) |
                       VRHI_STENCIL_FUNC_RMASK( readMask ) |
                       VRHI_STENCIL_FUNC_WMASK( writeMask ) |
                       frontTest | frontFailOp | frontDepthFailOp | frontDepthPassOp |
                       VRHI_STENCIL_BACK_TEST( backTest ) |
                       VRHI_STENCIL_BACK_OP_FAIL_S( backFailOp ) |
                       VRHI_STENCIL_BACK_OP_FAIL_Z( backDepthFailOp ) |
                       VRHI_STENCIL_BACK_OP_PASS_Z( backDepthPassOp );
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetViewDepthRange( float minZ, float maxZ )
    {
        viewDepthRange = glm::vec2( minZ, maxZ );
        dirty |= VRHI_DIRTY_VIEWPORT;
        return *this;
    }
    vhState& SetDepthBias( int bias, float clamp = 0.0f, float slopeScaled = 0.0f )
    {
        depthBias = bias;
        depthBiasClamp = clamp;
        slopeScaledDepthBias = slopeScaled;
        dirty |= VRHI_DIRTY_DEPTH_BIAS;
        return *this;
    }
    vhState& SetVertexBuffer( vhBuffer buffer, uint8_t stream, uint64_t offset = 0, uint32_t startVertex = 0, uint32_t numVertices = UINT32_MAX )
    {
        if ( stream >= vertexBindings.size() ) vertexBindings.resize( stream + 1 );
        vertexBindings[stream] = { buffer, stream, startVertex, numVertices, offset };
        dirty |= VRHI_DIRTY_VERTEX_INDEX;
        return *this;
    }
    vhState& SetVertexBuffer( vhBuffer buffer, uint8_t stream, uint64_t offset, uint32_t startVertex, uint32_t numVertices, const char* layoutOverride )
    {
        if ( stream >= vertexBindings.size() ) vertexBindings.resize( stream + 1 );
        vertexBindings[stream] = { buffer, stream, startVertex, numVertices, offset };
        vertexBindings[stream].layoutOverride = layoutOverride ? layoutOverride : "";
        vertexBindings[stream].isInstanced = ( layoutOverride && strstr( layoutOverride, ":i" ) ) ? true : false;
        dirty |= VRHI_DIRTY_VERTEX_INDEX;
        return *this;
    }
    vhState& ClearVertexBindings()
    {
        vertexBindings.clear();
        dirty |= VRHI_DIRTY_VERTEX_INDEX;
        return *this;
    }

    // WARNING: This clears all resource bindings and forces a full re-upload of lots of state 
    // to the GPU on the next draw. Use this sparingly, mostly when transitioning between 
    // entirely different rendering sub-systems. For performance, prefer targeted clears 
    // or simply overwriting specific bindings.
    vhState& ClearAllBindings()
    {
        vertexBindings.clear();
        indexBinding = {};
        textures.clear();
        samplers.clear();
        buffers.clear();
        constants.clear();
        uniforms.clear();
        accelStructs.clear();
        descriptorTables.clear();
        
        dirty |= ( VRHI_DIRTY_VERTEX_INDEX | VRHI_DIRTY_TEXTURE_SAMPLERS | VRHI_DIRTY_BUFFERS | VRHI_DIRTY_CONSTANTS | VRHI_DIRTY_UNIFORMS | VRHI_DIRTY_ACCEL_STRUCT | VRHI_DIRTY_DESCRIPTOR_TABLES );
        return *this;
    }
    vhState& SetIndexBuffer( vhBuffer buffer, uint64_t offset = 0, uint32_t firstIndex = 0, uint32_t numIndices = UINT32_MAX )
    {
        indexBinding = { buffer, firstIndex, numIndices, offset };
        dirty |= VRHI_DIRTY_VERTEX_INDEX;
        return *this;
    }
    vhState& SetTextures( const std::vector< TextureBinding >& textures_ )
    {
        textures = textures_;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    vhState& SetTexture( uint32_t idx, const TextureBinding& texture )
    {
        if ( idx >= textures.size() ) textures.resize( idx + 1 );
        textures[idx] = texture;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    vhState& SetTexture( uint32_t idx, vhTexture texture, int32_t slot = -1, const char* name = nullptr, bool computeUAV = false )
    {
        if ( idx >= textures.size() ) textures.resize( idx + 1 );
        auto& t = textures[idx];
        t.texture = texture;
        t.slot = slot;
        t.name = name;
        t.computeUAV = computeUAV;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    TextureBinding& GetTexture( uint32_t idx )
    {
        if ( idx >= textures.size() ) textures.resize( idx + 1 );
        return textures[idx];
    }
    vhState& SetBlendConstColor( const glm::vec4& color )
    {
        blendConstantColor = color;
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetShadingRate( uint32_t flags, vhTexture image = VRHI_INVALID_HANDLE )
    {
        shadingRateFlags = flags;
        shadingRateImage = image;
        dirty |= VRHI_DIRTY_VRS;
        return *this;
    }
    vhState& SetIndirectParams( vhBuffer buffer, uint64_t offset = 0 )
    {
        indirectParams.buffer = buffer;
        indirectParams.byteOffset = offset;
        dirty |= VRHI_DIRTY_INDIRECT;
        return *this;
    }
    vhState& SetIndirectCountBuffer( vhBuffer buffer, uint64_t offset = 0 )
    {
        indirectCountBuffer.buffer = buffer;
        indirectCountBuffer.byteOffset = offset;
        dirty |= VRHI_DIRTY_INDIRECT_COUNT;
        return *this;
    }
    vhState& SetSamplers( const std::vector< SamplerDefinition >& samplers_ )
    {
        samplers = samplers_;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    vhState& SetSampler( uint32_t idx, const SamplerDefinition& sampler )
    {
        if ( idx >= samplers.size() ) samplers.resize( idx + 1 );
        samplers[idx] = sampler;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    vhState& SetSampler( uint32_t idx, uint64_t flags, int32_t slot = -1, const char* name = nullptr )
    {
        if ( idx >= samplers.size() ) samplers.resize( idx + 1 );
        auto& s = samplers[idx];
        s.flags = flags;
        s.slot = slot;
        s.name = name;
        dirty |= VRHI_DIRTY_TEXTURE_SAMPLERS;
        return *this;
    }
    SamplerDefinition& GetSampler( uint32_t idx )
    {
        if ( idx >= samplers.size() ) samplers.resize( idx + 1 );
        return samplers[idx];
    }
    vhState& SetBuffers( const std::vector< BufferBinding >& buffers_ )
    {
        buffers = buffers_;
        dirty |= VRHI_DIRTY_BUFFERS;
        return *this;
    }
    vhState& SetBuffer( uint32_t idx, const BufferBinding& buffer )
    {
        if ( idx >= buffers.size() ) buffers.resize( idx + 1 );
        buffers[idx] = buffer;
        dirty |= VRHI_DIRTY_BUFFERS;
        return *this;
    }
    vhState& SetBuffer( uint32_t idx, vhBuffer buffer, int32_t slot = -1, const char* name = nullptr, uint64_t offset = 0, uint64_t size = 0, bool computeUAV = false )
    {
        if ( idx >= buffers.size() ) buffers.resize( idx + 1 );
        auto& b = buffers[idx];
        b.buffer = buffer;
        b.slot = slot;
        b.name = name;
        b.byteOffset = offset;
        b.byteSize = size;
        b.computeUAV = computeUAV;
        dirty |= VRHI_DIRTY_BUFFERS;
        return *this;
    }
    BufferBinding& GetBuffer( uint32_t idx )
    {
        if ( idx >= buffers.size() ) buffers.resize( idx + 1 );
        return buffers[idx];
    }
    vhState& SetAccelStructs( const std::vector< AccelStructBinding >& accelStructs_ )
    {
        accelStructs = accelStructs_;
        dirty |= VRHI_DIRTY_ACCEL_STRUCT;
        return *this;
    }
    vhState& SetAccelStruct( uint32_t idx, vhAccelStruct as, int32_t slot = -1, const char* name = nullptr )
    {
        if ( idx >= accelStructs.size() ) accelStructs.resize( idx + 1 );
        accelStructs[idx] = { as, slot, name };
        dirty |= VRHI_DIRTY_ACCEL_STRUCT;
        return *this;
    }
    vhState& SetDescriptorTables( const std::vector< DescriptorTableBinding >& tables_ )
    {
        descriptorTables = tables_;
        dirty |= VRHI_DIRTY_DESCRIPTOR_TABLES;
        return *this;
    }
    vhState& SetDescriptorTable( uint32_t idx, vhDescriptorTable table, uint32_t registerSpace = 0 )
    {
        if ( idx >= descriptorTables.size() ) descriptorTables.resize( idx + 1 );
        descriptorTables[idx] = { table, registerSpace };
        dirty |= VRHI_DIRTY_DESCRIPTOR_TABLES;
        return *this;
    }
    AccelStructBinding& GetAccelStruct( uint32_t idx )
    {
        if ( idx >= accelStructs.size() ) accelStructs.resize( idx + 1 );
        return accelStructs[idx];
    }
    vhState& SetConstants( const std::vector< ConstantBufferValue >& constants_ )
    {
        constants = constants_;
        dirty |= VRHI_DIRTY_CONSTANTS;
        return *this;
    }
    vhState& SetConstant( uint32_t idx, const ConstantBufferValue& constant )
    {
        if ( idx >= constants.size() ) constants.resize( idx + 1 );
        constants[idx] = constant;
        dirty |= VRHI_DIRTY_CONSTANTS;
        return *this;
    }
    vhState& SetConstant( uint32_t idx, const char* name, const glm::vec4* data, uint32_t num = 1 )
    {
        if ( idx >= constants.size() ) constants.resize( idx + 1 );
        auto& c = constants[idx];
        c.name = name;
        c.data.assign( data, data + num );
        dirty |= VRHI_DIRTY_CONSTANTS;
        return *this;
    }
    ConstantBufferValue& GetConstant( uint32_t idx )
    {
        if ( idx >= constants.size() ) constants.resize( idx + 1 );
        return constants[idx];
    }
    vhState& SetPushConstants( const glm::vec4& data )
    {
        pushConstants = data;
        dirty |= VRHI_DIRTY_PUSH_CONSTANTS;
        return *this;
    }
    vhState& SetUniforms( const std::vector< UniformBufferValue >& uniforms_ )
    {
        uniforms = uniforms_;
        dirty |= VRHI_DIRTY_UNIFORMS;
        return *this;
    }
    vhState& SetUniform( uint32_t idx, const UniformBufferValue& uniform )
    {
        if ( idx >= uniforms.size() ) uniforms.resize( idx + 1 );
        uniforms[idx] = uniform;
        dirty |= VRHI_DIRTY_UNIFORMS;
        return *this;
    }
    vhState& SetUniform( uint32_t idx, const char* name, const glm::vec4* data, uint32_t num = 1 )
    {
        if ( idx >= uniforms.size() ) uniforms.resize( idx + 1 );
        auto& u = uniforms[idx];
        u.name = name;
        u.data.assign( data, data + num );
        dirty |= VRHI_DIRTY_UNIFORMS;
        return *this;
    }
    UniformBufferValue& GetUniform( uint32_t idx )
    {
        if ( idx >= uniforms.size() ) uniforms.resize( idx + 1 );
        return uniforms[idx];
    }
    vhState& SetProgram( vhProgram prog )
    {
        program = prog;
        dirty |= VRHI_DIRTY_PROGRAM;
        return *this;
    }
    vhProgram GetProgram() const { return program; }

    vhState& SetColourAttachment( uint32_t idx, vhTexture texture, uint32_t mipLevel = 0, uint32_t arrayLayer = 0, nvrhi::Format formatOverride = nvrhi::Format::UNKNOWN, bool readOnly = false )
    {
        if ( idx >= colourAttachment.size() ) colourAttachment.resize( idx + 1 );
        auto& rt = colourAttachment[idx];
        rt.texture = texture;
        rt.mipLevel = mipLevel;
        rt.arrayLayer = arrayLayer;
        rt.formatOverride = formatOverride;
        rt.readOnly = readOnly;
        dirty |= VRHI_DIRTY_ATTACHMENTS;
        return *this;
    }
    vhState& SetDepthAttachment( vhTexture texture, uint32_t mipLevel = 0, uint32_t arrayLayer = 0, nvrhi::Format formatOverride = nvrhi::Format::UNKNOWN, bool readOnly = false )
    {
        depthAttachment.texture = texture;
        depthAttachment.mipLevel = mipLevel;
        depthAttachment.arrayLayer = arrayLayer;
        depthAttachment.formatOverride = formatOverride;
        depthAttachment.readOnly = readOnly;
        dirty |= VRHI_DIRTY_ATTACHMENTS;
        return *this;
    }

    vhState& SetAttachments( const std::vector< RenderTarget >& colours, RenderTarget depth = {} )
    {
        colourAttachment = colours;
        depthAttachment = depth;
        dirty |= VRHI_DIRTY_ATTACHMENTS;
        return *this;
    }
    // Marks all state as dirty, forcing full re-upload on next vhSetState.
    // Required when reusing a vhState across multiple state IDs or consecutive draws.
    vhState& DirtyAll()
    {
        dirty = VRHI_DIRTY_ALL;
        return *this;
    }
};

// Some global states created for you to use.
extern vhState g_state0;
extern vhState g_state1;

// Query state from backend.
//
// `id` is the state ID to query.
// `outState` receives the current state values.
// WARNING: This is slow and should be used mostly for debugging.
bool vhGetState( vhStateId id, vhState& outState );

// Set state on backend.
//
// `id` is the state ID to set.
// `state` contains the new state values.
// `dirtyForceMask` forces specified dirty bits to be set regardless of state's internal dirty tracking.
// This automatically uploads via dirty flags, making it efficient to call multiple times.
// IMPORTANT: vhSetState clears the dirty bits after upload. If you reuse the same vhState
// for multiple draw calls, call state.DirtyAll() before subsequent vhSetState calls.
bool vhSetState( vhStateId id, vhState& state, uint64_t dirtyForceMask = 0 );

// ------------ Submits ------------

// Dispatches a compute shader workload.
//
// `stateID` is the state ID containing the compute program and bindings.
// `workGroupCount` is the number of work groups to dispatch (x, y, z).
// VIDL_GENERATE
void vhDispatch( vhStateId stateID, glm::uvec3 workGroupCount );

// Dispatches a compute shader using indirect parameters from a buffer.
//
// `stateID` is the state ID containing the compute program and bindings.
// `indirectBuffer` contains the dispatch arguments (x, y, z as 3 uint32s).
// `byteOffset` is the offset in bytes into the indirect buffer.
// VIDL_GENERATE
void vhDispatchIndirect( vhStateId stateID, vhBuffer indirectBuffer, uint64_t byteOffset = 0 );

// Draws non-indexed primitives.
//
// `state` is the state ID containing render state.
// `vertexCount` is the number of vertices to draw.
// `instanceCount` is the number of instances to draw (default 1).
// `startVertexLocation` is the first vertex to draw (default 0).
// `startInstanceLocation` is the first instance to draw (default 0).
void vhDraw( vhStateId state, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0 );

// Draws indexed primitives.
//
// `state` is the state ID containing render state.
// `indexCount` is the number of indices to draw.
// `instanceCount` is the number of instances to draw (default 1).
// `startIndexLocation` is the first index to draw (default 0).
// `baseVertexLocation` is the value added to each index (default 0).
// `startInstanceLocation` is the first instance to draw (default 0).
void vhDrawIndexed( vhStateId state, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0, uint32_t startInstanceLocation = 0 );

// Draws non-indexed primitives using indirect buffer.
//
// `state` is the state ID containing render state.
// Indirect buffer must be set via state.SetIndirectParams().
// `drawCount` is the number of draw calls in the indirect buffer (default 1).
void vhDrawIndirect( vhStateId state, uint32_t drawCount = 1 );

// Draws indexed primitives using indirect buffer.
//
// `state` is the state ID containing render state.
// Indirect buffer must be set via state.SetIndirectParams().
// `drawCount` is the number of draw calls in the indirect buffer (default 1).
void vhDrawIndexedIndirect( vhStateId state, uint32_t drawCount = 1 );

// Draws indexed primitives using an indirect buffer with a GPU-driven draw count.
// Indirect buffer must be set via state.SetIndirectParams(). Count buffer via state.SetIndirectCountBuffer().
// `maxDrawCount` caps the number of draws actually issued (default 1).
void vhDrawIndexedIndirectCount( vhStateId state, uint32_t maxDrawCount = 1 );

// Clears the attachments bound in the state.
// `state` is the state ID containing bound colour and depth attachments to clear.
// `clearFlags` specifies what to clear (VRHI_CLEAR_COLOR | VRHI_CLEAR_DEPTH | VRHI_CLEAR_STENCIL).
// Clear values are taken from the state.
// VIDL_GENERATE
void vhClear( vhStateId state, uint16_t clearFlags = VRHI_CLEAR_COLOR );

// --------------------------------------------------------------------------
// Sub-Allocators
// --------------------------------------------------------------------------

// Triple-buffered ring allocator.
//
// Use this to prevent CPU-GPU sync stalls by maintaining a separate buffer for each frame-in-flight.
// Step() must be called once per frame to advance the ring.
// Step() advances to the next frame's buffer but does NOT reset the allocation offset.
// Call Reset() at frame start to reuse buffer space.
//
// Typical usage pairs with three vhBuffer handles, each sized for per-frame transient data:
//   vhBuffer g_transientInstanceBuffers[3] = { VRHI_INVALID_HANDLE, VRHI_INVALID_HANDLE, VRHI_INVALID_HANDLE };
//   vhTransientAllocator g_transientInstanceBufferAlloc;
//
//   // Initialise after buffer creation:
//   g_transientInstanceBufferAlloc.Init( g_transientInstanceBuffers, bufferSize );
//
//   // Per-frame:
//   g_transientInstanceBufferAlloc.Reset();
//   g_transientInstanceBufferAlloc.Step();
//   int64_t offset = g_transientInstanceBufferAlloc.Alloc( dataSize );
//   if ( offset >= 0 )
//   {
//       vhUpdateUniformBuffer( g_transientInstanceBufferAlloc.GetBuffer(), data, offset );
//   }
//
class vhTransientAllocator
{
    vhBuffer m_buffers[3];
    uint64_t m_size;
    uint64_t m_offset;
    uint32_t m_alignment;
    uint32_t m_frameIndex;

public:
    vhTransientAllocator()
        : m_size( 0 )
        , m_offset( 0 )
        , m_alignment( 256 )
        , m_frameIndex( 0 )
    {
        m_buffers[0] = VRHI_INVALID_HANDLE;
        m_buffers[1] = VRHI_INVALID_HANDLE;
        m_buffers[2] = VRHI_INVALID_HANDLE;
    }

    void Init( vhBuffer buffers[3], uint64_t size, uint32_t alignment = 256 )
    {
        m_buffers[0] = buffers[0];
        m_buffers[1] = buffers[1];
        m_buffers[2] = buffers[2];
        m_size = size;
        m_alignment = alignment;
        m_frameIndex = 0;
        Reset();
    }
    
    // Convenience helper to init with one array
    void Init( const std::vector< vhBuffer >& buffers, uint64_t size, uint32_t alignment = 256 )
    {
        if ( buffers.size() < 3 ) return;
        vhBuffer arr[3] = { buffers[0], buffers[1], buffers[2] };
        Init( arr, size, alignment );
    }

    void Reset()
    {
        m_offset = 0;
    }
    
    void Step()
    {
        m_frameIndex = ( m_frameIndex + 1 ) % 3;
    }

    int64_t Alloc( uint64_t size )
    {
        // Align
        uint64_t aligned = ( m_offset + m_alignment - 1 ) & ~( ( uint64_t ) m_alignment - 1 );
        if ( aligned + size > m_size ) return -1;
        
        m_offset = aligned + size;
        return ( int64_t ) aligned;
    }

    vhBuffer GetBuffer() const
    {
        return m_buffers[m_frameIndex];
    }
    
    uint32_t GetFrameIndex() const
    {
        return m_frameIndex;
    }

    uint64_t GetCurrentUsage() const
    {
        return m_offset;
    }
};

// --------------------------------------------------------------------------
// vhSubAllocator - Non-transient sub-allocator with deferred freeing
// Frees are deferred by 3 frames to avoid GPU sync issues.
// The memory won't be available for reallocation until Step() has been called 3 times.
// --------------------------------------------------------------------------

class vhSubAllocator
{
    vhBuffer m_buffer;
    uint64_t m_size;
    uint32_t m_alignment;
    uint32_t m_frameIndex;
    
    struct DeferredFree
    {
        uint64_t offset;
        uint32_t metadata; // OffsetAllocator::Allocation metadata
    };
    
    void* m_allocator; // Opaque pointer to OffsetAllocator::Allocator
    std::unordered_map< uint64_t, uint32_t > m_allocations;
    std::vector< DeferredFree > m_deferredFrees[3];

public:
    vhSubAllocator()
        : m_buffer( VRHI_INVALID_HANDLE )
        , m_size( 0 )
        , m_alignment( 256 )
        , m_frameIndex( 0 )
        , m_allocator( nullptr )
    {
    }

    ~vhSubAllocator();
    
    void Init( vhBuffer buffer, uint64_t size, uint32_t alignment = 256 );
    
    int64_t Alloc( uint64_t size );
    
    void Free( int64_t offset );
    
    void Step();
    
    void Reset();
    
    vhBuffer GetBuffer() const
    {
        return m_buffer;
    }
    
    uint32_t GetAlignment() const
    {
        return m_alignment;
    }
    
    uint64_t GetUsedSpace() const;
    
    uint64_t GetAvailableSpace() const;
};

// --------------------------------------------------------------------------
// Internals. DO NOT USE.
// --------------------------------------------------------------------------

// VIDL_GENERATE
void vhFlushInternal( std::atomic<bool>* fence, bool waitForGPU = false );
// VIDL_GENERATE
void vhGetPSOCacheInternal( std::atomic<bool>* fence, std::vector<uint8_t>* outData, size_t* outSize );
// VIDL_GENERATE
void vhCmdSetStateViewRect( vhStateId id, glm::vec4 rect );
// VIDL_GENERATE
void vhCmdSetStateViewScissor( vhStateId id, glm::vec4 scissor );
// VIDL_GENERATE
void vhCmdSetStateViewClear( vhStateId id, uint16_t flags, glm::vec4 color, glm::u8vec4 colorUInt, float depth, uint8_t stencil );
// VIDL_GENERATE
void vhCmdSetStateProgram( vhStateId id, vhProgram program );
// VIDL_GENERATE
void vhCmdSetStateViewTransform( vhStateId id, glm::mat4 view, glm::mat4 proj );
// VIDL_GENERATE
// VIDL_STORAGE: matrices = vhArenaSpan< glm::mat4 >
void vhCmdSetStateWorldTransform( vhStateId id, const std::vector< glm::mat4 >& matrices );
// VIDL_GENERATE
void vhCmdSetStateFlags( vhStateId id, uint64_t flags );
// VIDL_GENERATE
void vhCmdSetStateDebugFlags( vhStateId id, uint64_t flags );
// VIDL_GENERATE
void vhCmdSetStateStencil( vhStateId id, uint64_t stencilState );
// VIDL_GENERATE
void vhCmdSetStateDepthBias( vhStateId id, int bias, float clamp, float slopeScaled );
// VIDL_GENERATE
void vhCmdSetStateVertexBindings( vhStateId id, const std::vector< vhState::VertexBinding >& bindings );
// VIDL_GENERATE
void vhCmdSetStateVertexBuffer( vhStateId id, uint8_t stream, vhBuffer buffer, uint64_t offset, uint32_t start, uint32_t num, const vhVertexLayout& layoutOverride, bool isInstanced );
// VIDL_GENERATE
void vhCmdSetStateIndexBuffer( vhStateId id, vhBuffer buffer, uint64_t offset, uint32_t first, uint32_t num );
// VIDL_GENERATE
// VIDL_STORAGE: textures = vhArenaSpan< vhState::TextureBinding >
void vhCmdSetStateTextures( vhStateId id, const std::vector< vhState::TextureBinding >& textures );
// VIDL_GENERATE
// VIDL_STORAGE: samplers = vhArenaSpan< vhState::SamplerDefinition >
void vhCmdSetStateSamplers( vhStateId id, const std::vector< vhState::SamplerDefinition >& samplers );
// VIDL_GENERATE
// VIDL_STORAGE: buffers = vhArenaSpan< vhState::BufferBinding >
void vhCmdSetStateBuffers( vhStateId id, const std::vector< vhState::BufferBinding >& buffers );
// VIDL_GENERATE
// VIDL_STORAGE: constants = vhArenaSpan< vhArenaConstantValue >
void vhCmdSetStateConstants( vhStateId id, const std::vector< vhState::ConstantBufferValue >& constants );
// VIDL_GENERATE
void vhCmdSetStatePushConstants( vhStateId id, glm::vec4 data );
// VIDL_GENERATE
// VIDL_STORAGE: uniforms = vhArenaSpan< vhArenaUniformValue >
void vhCmdSetStateUniforms( vhStateId id, const std::vector< vhState::UniformBufferValue >& uniforms );
// VIDL_GENERATE
// VIDL_STORAGE: colours = vhArenaSpan< vhState::RenderTarget >
void vhCmdSetStateAttachments( vhStateId id, const std::vector< vhState::RenderTarget >& colours, vhState::RenderTarget depth );
// VIDL_GENERATE
void vhCmdSetStateBlendConstants( vhStateId id, glm::vec4 blendConst );
// VIDL_GENERATE
void vhCmdSetStateViewDepthRange( vhStateId id, float minZ, float maxZ );
// VIDL_GENERATE
void vhCmdSetStateShadingRate( vhStateId id, uint32_t flags, vhTexture image );
// VIDL_GENERATE
void vhCmdSetStateIndirectParams( vhStateId id, vhBuffer buffer, uint64_t offset );
// VIDL_GENERATE
void vhCmdSetStateIndirectCountBuffer( vhStateId id, vhBuffer buffer, uint64_t offset );
// VIDL_GENERATE
// VIDL_STORAGE: accelStructs = vhArenaSpan< vhState::AccelStructBinding >
void vhCmdSetStateAccelStructs( vhStateId id, const std::vector< vhState::AccelStructBinding >& accelStructs );
// VIDL_GENERATE
// VIDL_STORAGE: tables = vhArenaSpan< vhState::DescriptorTableBinding >
void vhCmdSetStateDescriptorTables( vhStateId id, const std::vector< vhState::DescriptorTableBinding >& tables );

// Internal omni-draw command. Do not call directly.
// VIDL_GENERATE
void vhDrawCommonInternal(
    vhStateId state,
    uint32_t flags,
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t startVertexLocation,
    uint32_t startIndexLocation,
    uint32_t startInstanceLocation,
    uint32_t drawCount
);

// --------------------------------------------------------------------------
// Slang Shaders
// --------------------------------------------------------------------------
//
// VRHI compiles shaders with Slang. Each shader stage gets its own descriptor set
// to avoid binding conflicts between stages (VS uses Set 1, PS uses Set 2, etc).
//
// VRHI_STAGE_SPACE:
//   A define injected at compile time that resolves to the stage's descriptor set.
//   Use it in register declarations to place resources in the correct set:
//
//     Texture2D    g_Tex : register( t0, VRHI_STAGE_SPACE );
//     SamplerState g_Sam : register( s0, VRHI_STAGE_SPACE );
//     cbuffer Data : register( b2, VRHI_STAGE_SPACE ) { float4 u_val; };
//     RWTexture2D<float> g_Out : register( u0, VRHI_STAGE_SPACE );
//
// Register Shifts:
//   Register numbers are shifted by type to avoid collisions within a set:
//     s100+ = Samplers,  t200+ = Textures,  b300+ = Constant Buffers,  u400+ = UAVs
//   So s0 becomes binding 100, t0 becomes 200, b0 becomes 300, u0 becomes 400.
//   These shifts are configurable via g_vhInit.shaderMake_*RegShift.
//
// Bare Uniforms (globalParams):
//   Bare uniform variables without a ConstantBuffer<> wrapper:
//     float4 u_colour;
//     float  u_time;
//   Slang collects these into an implicit constant buffer (globalParams / $Globals).
//   Update them via vhState::SetUniform() which uses an efficient ring buffer.
//
// Built-in Uniform Buffers (matched by name):
//   GlobalUniforms and WorldUniforms are automatically bound when shaders declare them.
//   They are identified by cbuffer name, not by fixed slots.
//   GlobalUniforms contains camera/projection state, WorldUniforms contains transforms.
//   Use vhState::SetViewTransform() and vhState::SetWorldTransform() to update these.
//   Shader-side definitions:
//
//     struct GlobalUniforms : register( b0, VRHI_STAGE_SPACE )
//     {
//         glm::vec4 u_viewRect;
//         glm::vec4 u_viewTexel;
//         glm::mat4  u_view;
//         glm::mat4  u_invView;
//         glm::mat4  u_proj;
//         glm::mat4  u_invProj;
//         glm::mat4  u_viewProj;
//         glm::mat4  u_invViewProj;
//         glm::vec4 u_alphaRef4;
//         glm::vec4 u_global[21];
//     };
//
//     struct WorldUniforms : register( b1, VRHI_STAGE_SPACE )
//     {
//         glm::mat4 u_world[4];
//         glm::mat4 u_worldView;
//         glm::mat4 u_worldViewProj;
//         glm::vec4 _pad[8];
//     };
//
// VRHI_SHADER_PATCH_DSET0:
//   Shaders that omit explicit register spaces default to DescriptorSet 0.
//   Set this flag to automatically remap those to the stage's descriptor set.
//   Useful for simple shaders where you don't want to annotate every resource.
//
