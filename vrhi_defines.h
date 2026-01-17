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
constexpr uint64_t VRHI_SHADER_STAGE_MAX           = 11;
constexpr uint64_t VRHI_SHADER_STAGE_MASK          = 0xF;

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

// --------------------------------------------------------------------------
// Buffers
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_BUFFER_NONE           = 0x0000;
constexpr uint64_t VRHI_BUFFER_COMPUTE_READ   = 0x0100; //!< Buffer will be read by shader.
constexpr uint64_t VRHI_BUFFER_COMPUTE_WRITE  = 0x0200; //!< Buffer will be used for writing.
constexpr uint64_t VRHI_BUFFER_DRAW_INDIRECT  = 0x0400; //!< Buffer will be used for storing draw indirect commands.
constexpr uint64_t VRHI_BUFFER_ALLOW_RESIZE   = 0x0800; //!< Allow dynamic index/vertex buffer resize during update.
constexpr uint64_t VRHI_BUFFER_INDEX32        = 0x1000; //!< Index buffer contains 32-bit indices.

constexpr uint64_t VRHI_BUFFER_COMPUTE_READ_WRITE = ( VRHI_BUFFER_COMPUTE_READ | VRHI_BUFFER_COMPUTE_WRITE );

// --------------------------------------------------------------------------
// Textures
// --------------------------------------------------------------------------

constexpr uint64_t VRHI_TEXTURE_NONE         = 0x0000000000000000;
constexpr uint64_t VRHI_TEXTURE_RT           = 0x0000001000000000; //!< Render target no MSAA.
constexpr uint64_t VRHI_TEXTURE_COMPUTE_WRITE = 0x0000100000000000; //!< Texture will be used for compute write.
constexpr uint64_t VRHI_TEXTURE_SRGB         = 0x0000200000000000; //!< Sample texture as sRGB.
constexpr uint64_t VRHI_TEXTURE_BLIT_DST     = 0x0000400000000000; //!< Texture will be used as blit destination.

// --------------------------------------------------------------------------
// Samplers
// --------------------------------------------------------------------------

constexpr uint32_t VRHI_SAMPLER_U_WRAP    = 0x00000000; //!< Wrap U mode: Wrap
constexpr uint32_t VRHI_SAMPLER_U_MIRROR  = 0x00000001; //!< Wrap U mode: Mirror
constexpr uint32_t VRHI_SAMPLER_U_CLAMP   = 0x00000002; //!< Wrap U mode: Clamp
constexpr uint32_t VRHI_SAMPLER_U_BORDER  = 0x00000003; //!< Wrap U mode: Border
constexpr uint32_t VRHI_SAMPLER_U_SHIFT   = 0;

constexpr uint32_t VRHI_SAMPLER_U_MASK    = 0x00000003;

constexpr uint32_t VRHI_SAMPLER_V_WRAP    = 0x00000000; //!< Wrap V mode: Wrap
constexpr uint32_t VRHI_SAMPLER_V_MIRROR  = 0x00000004; //!< Wrap V mode: Mirror
constexpr uint32_t VRHI_SAMPLER_V_CLAMP   = 0x00000008; //!< Wrap V mode: Clamp
constexpr uint32_t VRHI_SAMPLER_V_BORDER  = 0x0000000c; //!< Wrap V mode: Border
constexpr uint32_t VRHI_SAMPLER_V_SHIFT   = 2;

constexpr uint32_t VRHI_SAMPLER_V_MASK    = 0x0000000c;

constexpr uint32_t VRHI_SAMPLER_W_WRAP    = 0x00000000; //!< Wrap W mode: Wrap
constexpr uint32_t VRHI_SAMPLER_W_MIRROR  = 0x00000010; //!< Wrap W mode: Mirror
constexpr uint32_t VRHI_SAMPLER_W_CLAMP   = 0x00000020; //!< Wrap W mode: Clamp
constexpr uint32_t VRHI_SAMPLER_W_BORDER  = 0x00000030; //!< Wrap W mode: Border
constexpr uint32_t VRHI_SAMPLER_W_SHIFT   = 4;

constexpr uint32_t VRHI_SAMPLER_W_MASK    = 0x00000030;

constexpr uint32_t VRHI_SAMPLER_MIN_LINEAR      = 0x00000000; //!< Min sampling mode: Linear
constexpr uint32_t VRHI_SAMPLER_MIN_POINT       = 0x00000040; //!< Min sampling mode: Point
constexpr uint32_t VRHI_SAMPLER_MIN_ANISOTROPIC = 0x00000080; //!< Min sampling mode: Anisotropic
constexpr uint32_t VRHI_SAMPLER_MIN_SHIFT       = 6;

constexpr uint32_t VRHI_SAMPLER_MIN_MASK        = 0x000000c0;

constexpr uint32_t VRHI_SAMPLER_MAG_LINEAR      = 0x00000000; //!< Mag sampling mode: Linear
constexpr uint32_t VRHI_SAMPLER_MAG_POINT       = 0x00000100; //!< Mag sampling mode: Point
constexpr uint32_t VRHI_SAMPLER_MAG_ANISOTROPIC = 0x00000200; //!< Mag sampling mode: Anisotropic
constexpr uint32_t VRHI_SAMPLER_MAG_SHIFT       = 8;

constexpr uint32_t VRHI_SAMPLER_MAG_MASK        = 0x00000300;

constexpr uint32_t VRHI_SAMPLER_MIP_LINEAR      = 0x00000000; //!< Mip sampling mode: Linear
constexpr uint32_t VRHI_SAMPLER_MIP_POINT       = 0x00000400; //!< Mip sampling mode: Point
constexpr uint32_t VRHI_SAMPLER_MIP_NONE        = 0x00000800; //!< Mip sampling mode: None
constexpr uint32_t VRHI_SAMPLER_MIP_SHIFT       = 10;

constexpr uint32_t VRHI_SAMPLER_MIP_MASK        = 0x00000c00;

constexpr uint32_t VRHI_SAMPLER_COMPARE_LESS      = 0x00001000; //!< Compare when sampling depth texture: less.
constexpr uint32_t VRHI_SAMPLER_COMPARE_LEQUAL    = 0x00002000; //!< Compare when sampling depth texture: less or equal.
constexpr uint32_t VRHI_SAMPLER_COMPARE_EQUAL     = 0x00003000; //!< Compare when sampling depth texture: equal.
constexpr uint32_t VRHI_SAMPLER_COMPARE_GEQUAL    = 0x00004000; //!< Compare when sampling depth texture: greater or equal.
constexpr uint32_t VRHI_SAMPLER_COMPARE_GREATER   = 0x00005000; //!< Compare when sampling depth texture: greater.
constexpr uint32_t VRHI_SAMPLER_COMPARE_NOTEQUAL  = 0x00006000; //!< Compare when sampling depth texture: not equal.
constexpr uint32_t VRHI_SAMPLER_COMPARE_NEVER     = 0x00007000; //!< Compare when sampling depth texture: never.
constexpr uint32_t VRHI_SAMPLER_COMPARE_ALWAYS    = 0x00008000; //!< Compare when sampling depth texture: always.
constexpr uint32_t VRHI_SAMPLER_COMPARE_SHIFT     = 12;

constexpr uint32_t VRHI_SAMPLER_COMPARE_MASK      = 0x0000f000;

// Function-like macros (cannot be constexpr)
#define VRHI_SAMPLER_MIPBIAS_SHIFT                16
#define VRHI_SAMPLER_MIPBIAS_MASK                 0x00ff0000
#define VRHI_SAMPLER_MIPBIAS( v )                 ( ( ( uint32_t )( int32_t )( ( v ) * 16.0f ) << VRHI_SAMPLER_MIPBIAS_SHIFT ) & VRHI_SAMPLER_MIPBIAS_MASK )

#define VRHI_SAMPLER_BORDER_COLOUR_SHIFT          24
#define VRHI_SAMPLER_BORDER_COLOUR_MASK           0x0f000000
#define VRHI_SAMPLER_BORDER_COLOUR( v )           ( ( ( uint32_t )( v ) << VRHI_SAMPLER_BORDER_COLOUR_SHIFT ) & VRHI_SAMPLER_BORDER_COLOUR_MASK )

constexpr uint32_t VRHI_SAMPLER_SAMPLE_STENCIL    = 0x10000000; //!< Sample stencil instead of depth.

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

constexpr uint64_t VRHI_STATE_WRITE_R      = 0x0000000000000001; //!< Enable R write.
constexpr uint64_t VRHI_STATE_WRITE_G      = 0x0000000000000002; //!< Enable G write.
constexpr uint64_t VRHI_STATE_WRITE_B      = 0x0000000000000004; //!< Enable B write.
constexpr uint64_t VRHI_STATE_WRITE_A      = 0x0000000000000008; //!< Enable alpha write.
constexpr uint64_t VRHI_STATE_WRITE_Z      = 0x0000004000000000; //!< Enable depth write.

constexpr uint64_t VRHI_STATE_WRITE_RGB = (
    VRHI_STATE_WRITE_R |
    VRHI_STATE_WRITE_G |
    VRHI_STATE_WRITE_B );

constexpr uint64_t VRHI_STATE_WRITE_MASK = (
    VRHI_STATE_WRITE_RGB |
    VRHI_STATE_WRITE_A |
    VRHI_STATE_WRITE_Z );

constexpr uint64_t VRHI_STATE_DEPTH_TEST_LESS     = 0x0000000000000010; //!< Enable depth test, less.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_LEQUAL   = 0x0000000000000020; //!< Enable depth test, less or equal.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_EQUAL    = 0x0000000000000030; //!< Enable depth test, equal.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_GEQUAL   = 0x0000000000000040; //!< Enable depth test, greater or equal.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_GREATER  = 0x0000000000000050; //!< Enable depth test, greater.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_NOTEQUAL = 0x0000000000000060; //!< Enable depth test, not equal.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_NEVER    = 0x0000000000000070; //!< Enable depth test, never.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_ALWAYS   = 0x0000000000000080; //!< Enable depth test, always.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_SHIFT    = 4; //!< Depth test state bit shift
constexpr uint64_t VRHI_STATE_DEPTH_TEST_MASK     = 0x00000000000000f0; //!< Depth test state bit mask

constexpr uint64_t VRHI_STATE_BLEND_ZERO           = 0x0000000000001000; //!< 0, 0, 0, 0
constexpr uint64_t VRHI_STATE_BLEND_ONE            = 0x0000000000002000; //!< 1, 1, 1, 1
constexpr uint64_t VRHI_STATE_BLEND_SRC_COLOUR     = 0x0000000000003000; //!< Rs, Gs, Bs, As
constexpr uint64_t VRHI_STATE_BLEND_INV_SRC_COLOUR = 0x0000000000004000; //!< 1-Rs, 1-Gs, 1-Bs, 1-As
constexpr uint64_t VRHI_STATE_BLEND_SRC_ALPHA      = 0x0000000000005000; //!< As, As, As, As
constexpr uint64_t VRHI_STATE_BLEND_INV_SRC_ALPHA  = 0x0000000000006000; //!< 1-As, 1-As, 1-As, 1-As
constexpr uint64_t VRHI_STATE_BLEND_DST_ALPHA      = 0x0000000000007000; //!< Ad, Ad, Ad, Ad
constexpr uint64_t VRHI_STATE_BLEND_INV_DST_ALPHA  = 0x0000000000008000; //!< 1-Ad, 1-Ad, 1-Ad ,1-Ad
constexpr uint64_t VRHI_STATE_BLEND_DST_COLOUR     = 0x0000000000009000; //!< Rd, Gd, Bd, Ad
constexpr uint64_t VRHI_STATE_BLEND_INV_DST_COLOUR = 0x000000000000a000; //!< 1-Rd, 1-Gd, 1-Bd, 1-Ad
constexpr uint64_t VRHI_STATE_BLEND_SRC_ALPHA_SAT  = 0x000000000000b000; //!< f, f, f, 1; f = min(As, 1-Ad)
constexpr uint64_t VRHI_STATE_BLEND_FACTOR         = 0x000000000000c000; //!< Blend factor
constexpr uint64_t VRHI_STATE_BLEND_INV_FACTOR     = 0x000000000000d000; //!< 1-Blend factor
constexpr uint64_t VRHI_STATE_BLEND_SHIFT          = 12; //!< Blend state bit shift
constexpr uint64_t VRHI_STATE_BLEND_MASK           = 0x000000000ffff000; //!< Blend state bit mask

constexpr uint64_t VRHI_STATE_BLEND_EQUATION_ADD    = 0x0000000000000000; //!< Blend add: src + dst.
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_SUB    = 0x0000000010000000; //!< Blend subtract: src - dst.
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_REVSUB = 0x0000000020000000; //!< Blend reverse subtract: dst - src.
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MIN    = 0x0000000030000000; //!< Blend min: min(src, dst).
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MAX    = 0x0000000040000000; //!< Blend max: max(src, dst).
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_SHIFT  = 28; //!< Blend equation bit shift
constexpr uint64_t VRHI_STATE_BLEND_EQUATION_MASK   = 0x00000003f0000000; //!< Blend equation bit mask

constexpr uint64_t VRHI_STATE_CULL_NONE   = 0x0000000000000000; //!< No culling
constexpr uint64_t VRHI_STATE_CULL_BACK   = 0x0000000000000100; //!< Cull back faces (default CW triangles)
constexpr uint64_t VRHI_STATE_CULL_FRONT  = 0x0000000000000200; //!< Cull front faces (default CCW triangles)
constexpr uint64_t VRHI_STATE_CULL_SHIFT  = 8; //!< Culling mode bit shift
constexpr uint64_t VRHI_STATE_CULL_MASK   = 0x0000000000000300; //!< Culling mode bit mask

constexpr uint64_t VRHI_STATE_FRONT_CW    = 0x0000000000000400; //!< Override: CW triangles = front faces

constexpr uint64_t VRHI_STATE_PT_TRIANGLES  = 0x0000000000000000; //!< Triangles. ( Not needed, just for completeness )
constexpr uint64_t VRHI_STATE_PT_TRISTRIP   = 0x0001000000000000; //!< Tristrip.
constexpr uint64_t VRHI_STATE_PT_LINES      = 0x0002000000000000; //!< Lines.
constexpr uint64_t VRHI_STATE_PT_LINESTRIP  = 0x0003000000000000; //!< Line strip.
constexpr uint64_t VRHI_STATE_PT_POINTS     = 0x0004000000000000; //!< Points.
constexpr uint64_t VRHI_STATE_PT_SHIFT      = 48; //!< Primitive type bit shift
constexpr uint64_t VRHI_STATE_PT_MASK       = 0x0007000000000000; //!< Primitive type bit mask

constexpr uint64_t VRHI_STATE_MSAA                    = 0x0100000000000000; //!< Enable MSAA rasterization.
constexpr uint64_t VRHI_STATE_LINEAA                  = 0x0200000000000000; //!< Enable line AA rasterization.
constexpr uint64_t VRHI_STATE_CONSERVATIVE_RASTER     = 0x0400000000000000; //!< Enable conservative rasterization.
constexpr uint64_t VRHI_STATE_NONE                    = 0x0000000000000000; //!< No state.
constexpr uint64_t VRHI_STATE_BLEND_INDEPENDENT       = 0x0000000400000000; //!< Enable blend independent.
constexpr uint64_t VRHI_STATE_BLEND_ALPHA_TO_COVERAGE = 0x0000000800000000; //!< Enable alpha to coverage.
constexpr uint64_t VRHI_STATE_DEPTH_CLIP              = 0x0001000000000000; //!< Enable depth clipping.
constexpr uint64_t VRHI_STATE_DEPTH_TEST_ENABLE       = 0x0002000000000000; //!< Explicit depth test enable.

constexpr uint64_t VRHI_STATE_DEFAULT = (
    VRHI_STATE_WRITE_RGB |
    VRHI_STATE_WRITE_A |
    VRHI_STATE_WRITE_Z |
    VRHI_STATE_DEPTH_TEST_LESS |
    VRHI_STATE_CULL_BACK |
    VRHI_STATE_MSAA );

constexpr uint64_t VRHI_STATE_MASK            = 0xffffffffffffffff; //!< State bit mask
constexpr uint64_t VRHI_STATE_DEBUG_NONE      = 0x0000000000000000; //!< No debug state.
constexpr uint64_t VRHI_STATE_DEBUG_LOG_MISSING_BINDINGS = 0x0000000000000001; //!< Enable debug logging for missing bindings.
constexpr uint64_t VRHI_STATE_DEBUG_LOG_ALL_BINDINGS     = 0x0000000000000002; //!< Enable debug logging for all bindings.
constexpr uint64_t VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH = 0x0000000000000004; //!< Enable debug logging for vertex attribute mismatches.
constexpr uint64_t VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH = 0x0000000000000008; //!< Enable debug logging for binding mismatches.
constexpr uint64_t VRHI_STATE_DEBUG_ALL = 0xffffffffffffffff; //!< All debug flags.

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

constexpr uint64_t VRHI_STENCIL_TEST_LESS     = 0x0000000001000000; //!< Enable stencil test, less.
constexpr uint64_t VRHI_STENCIL_TEST_LEQUAL   = 0x0000000002000000; //!< Enable stencil test, less or equal.
constexpr uint64_t VRHI_STENCIL_TEST_EQUAL    = 0x0000000003000000; //!< Enable stencil test, equal.
constexpr uint64_t VRHI_STENCIL_TEST_GEQUAL   = 0x0000000004000000; //!< Enable stencil test, greater or equal.
constexpr uint64_t VRHI_STENCIL_TEST_GREATER  = 0x0000000005000000; //!< Enable stencil test, greater.
constexpr uint64_t VRHI_STENCIL_TEST_NOTEQUAL = 0x0000000006000000; //!< Enable stencil test, not equal.
constexpr uint64_t VRHI_STENCIL_TEST_NEVER    = 0x0000000007000000; //!< Enable stencil test, never.
constexpr uint64_t VRHI_STENCIL_TEST_ALWAYS   = 0x0000000008000000; //!< Enable stencil test, always.
constexpr uint64_t VRHI_STENCIL_TEST_SHIFT    = 24; //!< Stencil test bit shift
constexpr uint64_t VRHI_STENCIL_TEST_MASK     = 0x000000000f000000; //!< Stencil test bit mask

constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_ZERO     = 0x0000000000000000; //!< Zero.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_KEEP     = 0x0000000010000000; //!< Keep.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_REPLACE  = 0x0000000020000000; //!< Replace.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INCR     = 0x0000000030000000; //!< Increment and wrap.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INCRSAT  = 0x0000000040000000; //!< Increment and clamp.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_DECR     = 0x0000000050000000; //!< Decrement and wrap.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_DECRSAT  = 0x0000000060000000; //!< Decrement and clamp.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_INVERT   = 0x0000000070000000; //!< Invert.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_SHIFT    = 28; //!< Stencil operation fail bit shift
constexpr uint64_t VRHI_STENCIL_OP_FAIL_S_MASK     = 0x00000000f0000000; //!< Stencil operation fail bit mask

constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_ZERO     = 0x0000000000000000; //!< Zero.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_KEEP     = 0x0000000100000000; //!< Keep.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_REPLACE  = 0x0000000200000000; //!< Replace.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INCR     = 0x0000000300000000; //!< Increment and wrap.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INCRSAT  = 0x0000000400000000; //!< Increment and clamp.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_DECR     = 0x0000000500000000; //!< Decrement and wrap.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_DECRSAT  = 0x0000000600000000; //!< Decrement and clamp.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_INVERT   = 0x0000000700000000; //!< Invert.
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_SHIFT    = 32; //!< Stencil operation depth fail bit shift
constexpr uint64_t VRHI_STENCIL_OP_FAIL_Z_MASK     = 0x0000000f00000000; //!< Stencil operation depth fail bit mask

constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_ZERO     = 0x0000000000000000; //!< Zero.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_KEEP     = 0x0000001000000000; //!< Keep.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_REPLACE  = 0x0000002000000000; //!< Replace.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INCR     = 0x0000003000000000; //!< Increment and wrap.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INCRSAT  = 0x0000004000000000; //!< Increment and clamp.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_DECR     = 0x0000005000000000; //!< Decrement and wrap.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_DECRSAT  = 0x0000006000000000; //!< Decrement and clamp.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_INVERT   = 0x0000007000000000; //!< Invert.
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_SHIFT    = 36; //!< Stencil operation depth pass bit shift
constexpr uint64_t VRHI_STENCIL_OP_PASS_Z_MASK     = 0x000000f000000000; //!< Stencil operation depth pass bit mask

constexpr uint64_t VRHI_STENCIL_BACK_TEST_SHIFT    = 40;
constexpr uint64_t VRHI_STENCIL_BACK_TEST_MASK     = 0x00000f0000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_S_SHIFT = 44;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_S_MASK  = 0x000f000000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_Z_SHIFT = 48;
constexpr uint64_t VRHI_STENCIL_BACK_OP_FAIL_Z_MASK  = 0x00f0000000000000;
constexpr uint64_t VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT = 52;
constexpr uint64_t VRHI_STENCIL_BACK_OP_PASS_Z_MASK  = 0x00f0000000000000;

// --------------------------------------------------------------------------
// Clear
// --------------------------------------------------------------------------

constexpr uint16_t VRHI_CLEAR_NONE            = 0x0000; //!< No clear flags.
constexpr uint16_t VRHI_CLEAR_COLOR           = 0x0001; //!< Clear color.
constexpr uint16_t VRHI_CLEAR_DEPTH           = 0x0002; //!< Clear depth.
constexpr uint16_t VRHI_CLEAR_STENCIL         = 0x0004; //!< Clear stencil.
constexpr uint16_t VRHI_CLEAR_UINT            = 0x2000; //!< Clear as integer.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_0 = 0x0008; //!< Discard frame buffer attachment 0.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_1 = 0x0010; //!< Discard frame buffer attachment 1.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_2 = 0x0020; //!< Discard frame buffer attachment 2.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_3 = 0x0040; //!< Discard frame buffer attachment 3.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_4 = 0x0080; //!< Discard frame buffer attachment 4.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_5 = 0x0100; //!< Discard frame buffer attachment 5.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_6 = 0x0200; //!< Discard frame buffer attachment 6.
constexpr uint16_t VRHI_CLEAR_DISCARD_COLOR_7 = 0x0400; //!< Discard frame buffer attachment 7.
constexpr uint16_t VRHI_CLEAR_DISCARD_DEPTH   = 0x0800; //!< Discard frame buffer depth attachment.
constexpr uint16_t VRHI_CLEAR_DISCARD_STENCIL = 0x1000; //!< Discard frame buffer stencil attachment.

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
constexpr uint64_t VRHI_DIRTY_ALL              = 0xFFFFFFFFFFFFFFFF;

constexpr uint32_t VRHI_DRAW_INDEXED  = ( 1u << 0 );
constexpr uint32_t VRHI_DRAW_INDIRECT = ( 1u << 1 );

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
