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

#define VRHI_SHADER_STAGE_VERTEX        1
#define VRHI_SHADER_STAGE_PIXEL         2
#define VRHI_SHADER_STAGE_COMPUTE       3
#define VRHI_SHADER_STAGE_RAYGEN        4
#define VRHI_SHADER_STAGE_MISS          5
#define VRHI_SHADER_STAGE_CLOSEST_HIT   6
#define VRHI_SHADER_STAGE_MESH          7
#define VRHI_SHADER_STAGE_AMPLIFICATION 8
#define VRHI_SHADER_STAGE_MASK          0xF

#define VRHI_SHADER_SM_5_0              ( 1 << 4 )
#define VRHI_SHADER_SM_6_0              ( 2 << 4 )
#define VRHI_SHADER_SM_6_5              ( 3 << 4 ) // Default behaviour if 0
#define VRHI_SHADER_SM_6_6              ( 4 << 4 )
#define VRHI_SHADER_SM_MASK             0xF0

#define VRHI_SHADER_DEBUG               ( 1ULL << 8 )   // -O0 -g -embedPDB
#define VRHI_SHADER_ROW_MAJOR           ( 1ULL << 9 )   // -matrix-layout-row-major
#define VRHI_SHADER_WARNINGS_AS_ERRORS  ( 1ULL << 10 )  // -warnings-as-errors
#define VRHI_SHADER_STRIP_REFLECTION    ( 1ULL << 11 )  // --stripReflection. Good for release builds to reduce binary size.
#define VRHI_SHADER_ALL_RESOURCES_BOUND ( 1ULL << 12 )  // --allResourcesBound. Optimisation hint for the compiler.

// src: bgfx

#define VRHI_BUFFER_NONE                          UINT16_C(0x0000)
#define VRHI_BUFFER_COMPUTE_READ                  UINT16_C(0x0100) //!< Buffer will be read by shader.
#define VRHI_BUFFER_COMPUTE_WRITE                 UINT16_C(0x0200) //!< Buffer will be used for writing.
#define VRHI_BUFFER_DRAW_INDIRECT                 UINT16_C(0x0400) //!< Buffer will be used for storing draw indirect commands.
#define VRHI_BUFFER_ALLOW_RESIZE                  UINT16_C(0x0800) //!< Allow dynamic index/vertex buffer resize during update.
#define VRHI_BUFFER_INDEX32                       UINT16_C(0x1000) //!< Index buffer contains 32-bit indices.
#define VRHI_BUFFER_COMPUTE_READ_WRITE (0 \
	| VRHI_BUFFER_COMPUTE_READ \
	| VRHI_BUFFER_COMPUTE_WRITE \
	)

#define VRHI_TEXTURE_NONE                         UINT64_C(0x0000000000000000)
// #define VRHI_TEXTURE_MSAA_SAMPLE                  UINT64_C(0x0000000800000000) //!< Texture will be used for MSAA sampling.
#define VRHI_TEXTURE_RT                           UINT64_C(0x0000001000000000) //!< Render target no MSAA.
#define VRHI_TEXTURE_COMPUTE_WRITE                UINT64_C(0x0000100000000000) //!< Texture will be used for compute write.
#define VRHI_TEXTURE_SRGB                         UINT64_C(0x0000200000000000) //!< Sample texture as sRGB.
#define VRHI_TEXTURE_BLIT_DST                     UINT64_C(0x0000400000000000) //!< Texture will be used as blit destination.
//#define VRHI_TEXTURE_READ_BACK                    UINT64_C(0x0000800000000000) //!< Texture will be used for read back from GPU.

#define VRHI_SAMPLER_U_WRAP                       UINT32_C(0x00000000) //!< Wrap U mode: Wrap
#define VRHI_SAMPLER_U_MIRROR                     UINT32_C(0x00000001) //!< Wrap U mode: Mirror
#define VRHI_SAMPLER_U_CLAMP                      UINT32_C(0x00000002) //!< Wrap U mode: Clamp
#define VRHI_SAMPLER_U_BORDER                     UINT32_C(0x00000003) //!< Wrap U mode: Border
#define VRHI_SAMPLER_U_SHIFT                      0

#define VRHI_SAMPLER_U_MASK                       UINT32_C(0x00000003)

#define VRHI_SAMPLER_V_WRAP                       UINT32_C(0x00000000) //!< Wrap V mode: Wrap
#define VRHI_SAMPLER_V_MIRROR                     UINT32_C(0x00000004) //!< Wrap V mode: Mirror
#define VRHI_SAMPLER_V_CLAMP                      UINT32_C(0x00000008) //!< Wrap V mode: Clamp
#define VRHI_SAMPLER_V_BORDER                     UINT32_C(0x0000000c) //!< Wrap V mode: Border
#define VRHI_SAMPLER_V_SHIFT                      2

#define VRHI_SAMPLER_V_MASK                       UINT32_C(0x0000000c)

#define VRHI_SAMPLER_W_WRAP                       UINT32_C(0x00000000) //!< Wrap W mode: Wrap
#define VRHI_SAMPLER_W_MIRROR                     UINT32_C(0x00000010) //!< Wrap W mode: Mirror
#define VRHI_SAMPLER_W_CLAMP                      UINT32_C(0x00000020) //!< Wrap W mode: Clamp
#define VRHI_SAMPLER_W_BORDER                     UINT32_C(0x00000030) //!< Wrap W mode: Border
#define VRHI_SAMPLER_W_SHIFT                      4

#define VRHI_SAMPLER_W_MASK                       UINT32_C(0x00000030)

#define VRHI_SAMPLER_MIN_LINEAR                   UINT32_C(0x00000000) //!< Min sampling mode: Linear
#define VRHI_SAMPLER_MIN_POINT                    UINT32_C(0x00000040) //!< Min sampling mode: Point
#define VRHI_SAMPLER_MIN_ANISOTROPIC              UINT32_C(0x00000080) //!< Min sampling mode: Anisotropic
#define VRHI_SAMPLER_MIN_SHIFT                    6

#define VRHI_SAMPLER_MIN_MASK                     UINT32_C(0x000000c0)

#define VRHI_SAMPLER_MAG_LINEAR                   UINT32_C(0x00000000) //!< Mag sampling mode: Linear
#define VRHI_SAMPLER_MAG_POINT                    UINT32_C(0x00000100) //!< Mag sampling mode: Point
#define VRHI_SAMPLER_MAG_ANISOTROPIC              UINT32_C(0x00000200) //!< Mag sampling mode: Anisotropic
#define VRHI_SAMPLER_MAG_SHIFT                    8

#define VRHI_SAMPLER_MAG_MASK                     UINT32_C(0x00000300)

#define VRHI_SAMPLER_MIP_LINEAR                   UINT32_C(0x00000000) //!< Mip sampling mode: Linear
#define VRHI_SAMPLER_MIP_POINT                    UINT32_C(0x00000400) //!< Mip sampling mode: Point
#define VRHI_SAMPLER_MIP_NONE                     UINT32_C(0x00000800) //!< Mip sampling mode: None
#define VRHI_SAMPLER_MIP_SHIFT                    10

#define VRHI_SAMPLER_MIP_MASK                     UINT32_C(0x00000c00)

#define VRHI_SAMPLER_COMPARE_LESS                 UINT32_C(0x00001000) //!< Compare when sampling depth texture: less.
#define VRHI_SAMPLER_COMPARE_LEQUAL               UINT32_C(0x00002000) //!< Compare when sampling depth texture: less or equal.
#define VRHI_SAMPLER_COMPARE_EQUAL                UINT32_C(0x00003000) //!< Compare when sampling depth texture: equal.
#define VRHI_SAMPLER_COMPARE_GEQUAL               UINT32_C(0x00004000) //!< Compare when sampling depth texture: greater or equal.
#define VRHI_SAMPLER_COMPARE_GREATER              UINT32_C(0x00005000) //!< Compare when sampling depth texture: greater.
#define VRHI_SAMPLER_COMPARE_NOTEQUAL             UINT32_C(0x00006000) //!< Compare when sampling depth texture: not equal.
#define VRHI_SAMPLER_COMPARE_NEVER                UINT32_C(0x00007000) //!< Compare when sampling depth texture: never.
#define VRHI_SAMPLER_COMPARE_ALWAYS               UINT32_C(0x00008000) //!< Compare when sampling depth texture: always.
#define VRHI_SAMPLER_COMPARE_SHIFT                12

#define VRHI_SAMPLER_COMPARE_MASK                 UINT32_C(0x0000f000)

#define VRHI_SAMPLER_MIPBIAS_SHIFT                16
#define VRHI_SAMPLER_MIPBIAS_MASK                 UINT32_C(0x00ff0000)
#define VRHI_SAMPLER_MIPBIAS(v) ( ( (uint32_t)((v)*16.0f) << VRHI_SAMPLER_MIPBIAS_SHIFT )&VRHI_SAMPLER_MIPBIAS_MASK)

#define VRHI_SAMPLER_BORDER_COLOUR_SHIFT           24

#define VRHI_SAMPLER_BORDER_COLOUR_MASK            UINT32_C(0x0f000000)
#define VRHI_SAMPLER_BORDER_COLOUR(v) ( ( (uint32_t)(v)<<VRHI_SAMPLER_BORDER_COLOUR_SHIFT )&VRHI_SAMPLER_BORDER_COLOUR_MASK)

#define VRHI_SAMPLER_SAMPLE_STENCIL               UINT32_C(0x10000000) //!< Sample stencil instead of depth.

#define VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT         29
#define VRHI_SAMPLER_MAX_ANISOTROPY_MASK          UINT32_C(0xe0000000)
#define VRHI_SAMPLER_MAX_ANISOTROPY(v) ( ( (uint32_t)(v)<<VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT )&VRHI_SAMPLER_MAX_ANISOTROPY_MASK)
#define VRHI_SAMPLER_ANISOTROPY_1                 VRHI_SAMPLER_MAX_ANISOTROPY(0)
#define VRHI_SAMPLER_ANISOTROPY_2                 VRHI_SAMPLER_MAX_ANISOTROPY(1)
#define VRHI_SAMPLER_ANISOTROPY_4                 VRHI_SAMPLER_MAX_ANISOTROPY(2)
#define VRHI_SAMPLER_ANISOTROPY_8                 VRHI_SAMPLER_MAX_ANISOTROPY(3)
#define VRHI_SAMPLER_ANISOTROPY_16                VRHI_SAMPLER_MAX_ANISOTROPY(4)

#define VRHI_SAMPLER_NONE                         UINT32_C(0x00000000)

#define VRHI_SAMPLER_POINT (0 \
	| VRHI_SAMPLER_MIN_POINT \
	| VRHI_SAMPLER_MAG_POINT \
	| VRHI_SAMPLER_MIP_POINT \
	)

#define VRHI_SAMPLER_UVW_MIRROR (0 \
	| VRHI_SAMPLER_U_MIRROR \
	| VRHI_SAMPLER_V_MIRROR \
	| VRHI_SAMPLER_W_MIRROR \
	)

#define VRHI_SAMPLER_UVW_CLAMP (0 \
	| VRHI_SAMPLER_U_CLAMP \
	| VRHI_SAMPLER_V_CLAMP \
	| VRHI_SAMPLER_W_CLAMP \
	)

#define VRHI_SAMPLER_UVW_BORDER (0 \
	| VRHI_SAMPLER_U_BORDER \
	| VRHI_SAMPLER_V_BORDER \
	| VRHI_SAMPLER_W_BORDER \
	)

#define VRHI_SAMPLER_UVW_WRAP (0 \
	| VRHI_SAMPLER_U_WRAP \
	| VRHI_SAMPLER_V_WRAP \
	| VRHI_SAMPLER_W_WRAP \
	)

#define VRHI_SAMPLER_BITS_MASK (0 \
	| VRHI_SAMPLER_U_MASK \
	| VRHI_SAMPLER_V_MASK \
	| VRHI_SAMPLER_W_MASK \
	| VRHI_SAMPLER_MIN_MASK \
	| VRHI_SAMPLER_MAG_MASK \
	| VRHI_SAMPLER_MIP_MASK \
	| VRHI_SAMPLER_COMPARE_MASK \
	| VRHI_SAMPLER_MIPBIAS_MASK \
	| VRHI_SAMPLER_BORDER_COLOUR_MASK \
	| VRHI_SAMPLER_SAMPLE_STENCIL \
	| VRHI_SAMPLER_MAX_ANISOTROPY_MASK \
	)

// --- STATE ---

/**
* Colour RGB/alpha/depth write. When it's not specified write will be disabled.
*
*/
#define VRHI_STATE_WRITE_R                        UINT64_C(0x0000000000000001) //!< Enable R write.
#define VRHI_STATE_WRITE_G                        UINT64_C(0x0000000000000002) //!< Enable G write.
#define VRHI_STATE_WRITE_B                        UINT64_C(0x0000000000000004) //!< Enable B write.
#define VRHI_STATE_WRITE_A                        UINT64_C(0x0000000000000008) //!< Enable alpha write.
#define VRHI_STATE_WRITE_Z                        UINT64_C(0x0000004000000000) //!< Enable depth write.
/// Enable RGB write.
#define VRHI_STATE_WRITE_RGB (0 \
	| VRHI_STATE_WRITE_R \
	| VRHI_STATE_WRITE_G \
	| VRHI_STATE_WRITE_B \
	)

/// Write all channels mask.
#define VRHI_STATE_WRITE_MASK (0 \
	| VRHI_STATE_WRITE_RGB \
	| VRHI_STATE_WRITE_A \
	| VRHI_STATE_WRITE_Z \
	)


/**
* Depth test state. When `VRHI_STATE_DEPTH_` is not specified depth test will be disabled.
*
*/
#define VRHI_STATE_DEPTH_TEST_LESS                UINT64_C(0x0000000000000010) //!< Enable depth test, less.
#define VRHI_STATE_DEPTH_TEST_LEQUAL              UINT64_C(0x0000000000000020) //!< Enable depth test, less or equal.
#define VRHI_STATE_DEPTH_TEST_EQUAL               UINT64_C(0x0000000000000030) //!< Enable depth test, equal.
#define VRHI_STATE_DEPTH_TEST_GEQUAL              UINT64_C(0x0000000000000040) //!< Enable depth test, greater or equal.
#define VRHI_STATE_DEPTH_TEST_GREATER             UINT64_C(0x0000000000000050) //!< Enable depth test, greater.
#define VRHI_STATE_DEPTH_TEST_NOTEQUAL            UINT64_C(0x0000000000000060) //!< Enable depth test, not equal.
#define VRHI_STATE_DEPTH_TEST_NEVER               UINT64_C(0x0000000000000070) //!< Enable depth test, never.
#define VRHI_STATE_DEPTH_TEST_ALWAYS              UINT64_C(0x0000000000000080) //!< Enable depth test, always.
#define VRHI_STATE_DEPTH_TEST_SHIFT               4                            //!< Depth test state bit shift
#define VRHI_STATE_DEPTH_TEST_MASK                UINT64_C(0x00000000000000f0) //!< Depth test state bit mask

/**
* Use VRHI_STATE_BLEND_FUNC(_src, _dst) or VRHI_STATE_BLEND_FUNC_SEPARATE(_srcRGB, _dstRGB, _srcA, _dstA)
* helper macros.
*
*/
#define VRHI_STATE_BLEND_ZERO                     UINT64_C(0x0000000000001000) //!< 0, 0, 0, 0
#define VRHI_STATE_BLEND_ONE                      UINT64_C(0x0000000000002000) //!< 1, 1, 1, 1
#define VRHI_STATE_BLEND_SRC_COLOUR               UINT64_C(0x0000000000003000) //!< Rs, Gs, Bs, As
#define VRHI_STATE_BLEND_INV_SRC_COLOUR           UINT64_C(0x0000000000004000) //!< 1-Rs, 1-Gs, 1-Bs, 1-As
#define VRHI_STATE_BLEND_SRC_ALPHA                UINT64_C(0x0000000000005000) //!< As, As, As, As
#define VRHI_STATE_BLEND_INV_SRC_ALPHA            UINT64_C(0x0000000000006000) //!< 1-As, 1-As, 1-As, 1-As
#define VRHI_STATE_BLEND_DST_ALPHA                UINT64_C(0x0000000000007000) //!< Ad, Ad, Ad, Ad
#define VRHI_STATE_BLEND_INV_DST_ALPHA            UINT64_C(0x0000000000008000) //!< 1-Ad, 1-Ad, 1-Ad ,1-Ad
#define VRHI_STATE_BLEND_DST_COLOUR               UINT64_C(0x0000000000009000) //!< Rd, Gd, Bd, Ad
#define VRHI_STATE_BLEND_INV_DST_COLOUR           UINT64_C(0x000000000000a000) //!< 1-Rd, 1-Gd, 1-Bd, 1-Ad
#define VRHI_STATE_BLEND_SRC_ALPHA_SAT            UINT64_C(0x000000000000b000) //!< f, f, f, 1; f = min(As, 1-Ad)
#define VRHI_STATE_BLEND_FACTOR                   UINT64_C(0x000000000000c000) //!< Blend factor
#define VRHI_STATE_BLEND_INV_FACTOR               UINT64_C(0x000000000000d000) //!< 1-Blend factor
#define VRHI_STATE_BLEND_SHIFT                    12                           //!< Blend state bit shift
#define VRHI_STATE_BLEND_MASK                     UINT64_C(0x000000000ffff000) //!< Blend state bit mask

/**
* Use VRHI_STATE_BLEND_EQUATION(_equation) or VRHI_STATE_BLEND_EQUATION_SEPARATE(_equationRGB, _equationA)
* helper macros.
*
*/
#define VRHI_STATE_BLEND_EQUATION_ADD             UINT64_C(0x0000000000000000) //!< Blend add: src + dst.
#define VRHI_STATE_BLEND_EQUATION_SUB             UINT64_C(0x0000000010000000) //!< Blend subtract: src - dst.
#define VRHI_STATE_BLEND_EQUATION_REVSUB          UINT64_C(0x0000000020000000) //!< Blend reverse subtract: dst - src.
#define VRHI_STATE_BLEND_EQUATION_MIN             UINT64_C(0x0000000030000000) //!< Blend min: min(src, dst).
#define VRHI_STATE_BLEND_EQUATION_MAX             UINT64_C(0x0000000040000000) //!< Blend max: max(src, dst).
#define VRHI_STATE_BLEND_EQUATION_SHIFT           28                           //!< Blend equation bit shift
#define VRHI_STATE_BLEND_EQUATION_MASK            UINT64_C(0x00000003f0000000) //!< Blend equation bit mask

/**
* Cull state. When `VRHI_STATE_CULL_*` is not specified culling will be disabled.
*
*/
#define VRHI_STATE_CULL_CW                        UINT64_C(0x0000001000000000) //!< Cull clockwise triangles.
#define VRHI_STATE_CULL_CCW                       UINT64_C(0x0000002000000000) //!< Cull counter-clockwise triangles.
#define VRHI_STATE_CULL_SHIFT                     36                           //!< Culling mode bit shift
#define VRHI_STATE_CULL_MASK                      UINT64_C(0x0000003000000000) //!< Culling mode bit mask

#define VRHI_STATE_PT_TRIANGLES                   UINT64_C(0x0000000000000000) //!< Triangles. ( Not needed, just for completeness )
#define VRHI_STATE_PT_TRISTRIP                    UINT64_C(0x0001000000000000) //!< Tristrip.
#define VRHI_STATE_PT_LINES                       UINT64_C(0x0002000000000000) //!< Lines.
#define VRHI_STATE_PT_LINESTRIP                   UINT64_C(0x0003000000000000) //!< Line strip.
#define VRHI_STATE_PT_POINTS                      UINT64_C(0x0004000000000000) //!< Points.
#define VRHI_STATE_PT_SHIFT                       48                           //!< Primitive type bit shift
#define VRHI_STATE_PT_MASK                        UINT64_C(0x0007000000000000) //!< Primitive type bit mask

/**
* Enable MSAA write when writing into MSAA frame buffer.
* This flag is ignored when not writing into MSAA frame buffer.
*
*/
#define VRHI_STATE_MSAA                           UINT64_C(0x0100000000000000) //!< Enable MSAA rasterization.
#define VRHI_STATE_LINEAA                         UINT64_C(0x0200000000000000) //!< Enable line AA rasterization.
#define VRHI_STATE_CONSERVATIVE_RASTER            UINT64_C(0x0400000000000000) //!< Enable conservative rasterization.
#define VRHI_STATE_NONE                           UINT64_C(0x0000000000000000) //!< No state.
#define VRHI_STATE_FRONT_CCW                      UINT64_C(0x0000008000000000) //!< Front counter-clockwise ( default is clockwise ).
#define VRHI_STATE_BLEND_INDEPENDENT              UINT64_C(0x0000000400000000) //!< Enable blend independent.
#define VRHI_STATE_BLEND_ALPHA_TO_COVERAGE        UINT64_C(0x0000000800000000) //!< Enable alpha to coverage.
/// Default state is write to RGB, alpha, and depth with depth test less enabled, with clockwise
/// culling and MSAA (when writing into MSAA frame buffer, otherwise this flag is ignored).
#define VRHI_STATE_DEFAULT (0 \
	| VRHI_STATE_WRITE_RGB \
	| VRHI_STATE_WRITE_A \
	| VRHI_STATE_WRITE_Z \
	| VRHI_STATE_DEPTH_TEST_LESS \
	| VRHI_STATE_CULL_CW \
	| VRHI_STATE_MSAA \
	)

#define VRHI_STATE_MASK                           UINT64_C(0xffffffffffffffff) //!< State bit mask

#define VRHI_STATE_DEBUG_NONE                     UINT64_C(0x0000000000000000) //!< No debug state.
#define VRHI_STATE_DEBUG_LOG_MISSING_BINDINGS     UINT64_C(0x0000000000000001) //!< Enable debug logging for missing bindings.
#define VRHI_STATE_DEBUG_LOG_ALL_BINDINGS         UINT64_C(0x0000000000000002) //!< Enable debug logging for all bindings.

#define VRHI_DIRTY_WORLD                          ( 1ULL << 0 )
#define VRHI_DIRTY_VERTEX_INDEX                   ( 1ULL << 1 )
#define VRHI_DIRTY_CAMERA                         ( 1ULL << 2 )
#define VRHI_DIRTY_PIPELINE                       ( 1ULL << 3 )
#define VRHI_DIRTY_VIEWPORT                       ( 1ULL << 4 )
#define VRHI_DIRTY_ATTACHMENTS                    ( 1ULL << 5 )
#define VRHI_DIRTY_TEXTURE_SAMPLERS               ( 1ULL << 6 )
#define VRHI_DIRTY_BUFFERS                        ( 1ULL << 7 )
#define VRHI_DIRTY_CONSTANTS                      ( 1ULL << 8 )
#define VRHI_DIRTY_PUSH_CONSTANTS                 ( 1ULL << 9 )
#define VRHI_DIRTY_PROGRAM                        ( 1ULL << 10 )
#define VRHI_DIRTY_UNIFORMS                       ( 1ULL << 11 )
#define VRHI_DIRTY_ALL                            ( 0xFFFFFFFFFFFFFFFF )

/// Blend function separate.
#define VRHI_STATE_BLEND_FUNC_SEPARATE(_srcRGB, _dstRGB, _srcA, _dstA) (UINT64_C(0) \
	| ( ( (uint64_t)(_srcRGB)|( (uint64_t)(_dstRGB)<<4) )   )                       \
	| ( ( (uint64_t)(_srcA  )|( (uint64_t)(_dstA  )<<4) )<<8)                       \
	)

/// Blend equation separate.
#define VRHI_STATE_BLEND_EQUATION_SEPARATE(_equationRGB, _equationA) ( (uint64_t)(_equationRGB)|( (uint64_t)(_equationA)<<3) )

/// Blend function.
#define VRHI_STATE_BLEND_FUNC(_src, _dst)    VRHI_STATE_BLEND_FUNC_SEPARATE(_src, _dst, _src, _dst)

/// Blend equation.
#define VRHI_STATE_BLEND_EQUATION(_equation) VRHI_STATE_BLEND_EQUATION_SEPARATE(_equation, _equation)

/// Utility predefined blend modes.

/// Additive blending.
#define VRHI_STATE_BLEND_ADD (0                                         \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE) \
	)

/// Alpha blend.
#define VRHI_STATE_BLEND_ALPHA (0                                                       \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_SRC_ALPHA, VRHI_STATE_BLEND_INV_SRC_ALPHA) \
	)

/// Selects darker colour of blend.
#define VRHI_STATE_BLEND_DARKEN (0                                      \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE) \
	| VRHI_STATE_BLEND_EQUATION(VRHI_STATE_BLEND_EQUATION_MIN)          \
	)

/// Selects lighter colour of blend.
#define VRHI_STATE_BLEND_LIGHTEN (0                                     \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_ONE) \
	| VRHI_STATE_BLEND_EQUATION(VRHI_STATE_BLEND_EQUATION_MAX)          \
	)

/// Multiplies colours.
#define VRHI_STATE_BLEND_MULTIPLY (0                                           \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_DST_COLOUR, VRHI_STATE_BLEND_ZERO) \
	)

/// Opaque pixels will cover the pixels directly below them without any math or algorithm applied to them.
#define VRHI_STATE_BLEND_NORMAL (0                                                \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_INV_SRC_ALPHA) \
	)

/// Multiplies the inverse of the blend and base colours.
#define VRHI_STATE_BLEND_SCREEN (0                                                \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_ONE, VRHI_STATE_BLEND_INV_SRC_COLOUR) \
	)

/// Decreases the brightness of the base colour based on the value of the blend colour.
#define VRHI_STATE_BLEND_LINEAR_BURN (0                                                 \
	| VRHI_STATE_BLEND_FUNC(VRHI_STATE_BLEND_DST_COLOUR, VRHI_STATE_BLEND_INV_DST_COLOUR) \
	| VRHI_STATE_BLEND_EQUATION(VRHI_STATE_BLEND_EQUATION_SUB)                          \
	)

///
#define VRHI_STATE_BLEND_FUNC_RT_x(_src, _dst) (0         \
	| ( (uint32_t)( (_src)>>VRHI_STATE_BLEND_SHIFT)       \
	| ( (uint32_t)( (_dst)>>VRHI_STATE_BLEND_SHIFT)<<4) ) \
	)

///
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