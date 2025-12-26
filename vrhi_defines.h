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

#define VRHI_SAMPLER_U_MIRROR                     UINT32_C(0x00000001) //!< Wrap U mode: Mirror
#define VRHI_SAMPLER_U_CLAMP                      UINT32_C(0x00000002) //!< Wrap U mode: Clamp
#define VRHI_SAMPLER_U_BORDER                     UINT32_C(0x00000003) //!< Wrap U mode: Border
#define VRHI_SAMPLER_U_SHIFT                      0

#define VRHI_SAMPLER_U_MASK                       UINT32_C(0x00000003)

#define VRHI_SAMPLER_V_MIRROR                     UINT32_C(0x00000004) //!< Wrap V mode: Mirror
#define VRHI_SAMPLER_V_CLAMP                      UINT32_C(0x00000008) //!< Wrap V mode: Clamp
#define VRHI_SAMPLER_V_BORDER                     UINT32_C(0x0000000c) //!< Wrap V mode: Border
#define VRHI_SAMPLER_V_SHIFT                      2

#define VRHI_SAMPLER_V_MASK                       UINT32_C(0x0000000c)

#define VRHI_SAMPLER_W_MIRROR                     UINT32_C(0x00000010) //!< Wrap W mode: Mirror
#define VRHI_SAMPLER_W_CLAMP                      UINT32_C(0x00000020) //!< Wrap W mode: Clamp
#define VRHI_SAMPLER_W_BORDER                     UINT32_C(0x00000030) //!< Wrap W mode: Border
#define VRHI_SAMPLER_W_SHIFT                      4

#define VRHI_SAMPLER_W_MASK                       UINT32_C(0x00000030)

#define VRHI_SAMPLER_MIN_POINT                    UINT32_C(0x00000040) //!< Min sampling mode: Point
#define VRHI_SAMPLER_MIN_ANISOTROPIC              UINT32_C(0x00000080) //!< Min sampling mode: Anisotropic
#define VRHI_SAMPLER_MIN_SHIFT                    6

#define VRHI_SAMPLER_MIN_MASK                     UINT32_C(0x000000c0)

#define VRHI_SAMPLER_MAG_POINT                    UINT32_C(0x00000100) //!< Mag sampling mode: Point
#define VRHI_SAMPLER_MAG_ANISOTROPIC              UINT32_C(0x00000200) //!< Mag sampling mode: Anisotropic
#define VRHI_SAMPLER_MAG_SHIFT                    8

#define VRHI_SAMPLER_MAG_MASK                     UINT32_C(0x00000300)

#define VRHI_SAMPLER_MIP_POINT                    UINT32_C(0x00000400) //!< Mip sampling mode: Point
#define VRHI_SAMPLER_MIP_SHIFT                    10

#define VRHI_SAMPLER_MIP_MASK                     UINT32_C(0x00000400)

#define VRHI_SAMPLER_COMPARE_LESS                 UINT32_C(0x00010000) //!< Compare when sampling depth texture: less.
#define VRHI_SAMPLER_COMPARE_LEQUAL               UINT32_C(0x00020000) //!< Compare when sampling depth texture: less or equal.
#define VRHI_SAMPLER_COMPARE_EQUAL                UINT32_C(0x00030000) //!< Compare when sampling depth texture: equal.
#define VRHI_SAMPLER_COMPARE_GEQUAL               UINT32_C(0x00040000) //!< Compare when sampling depth texture: greater or equal.
#define VRHI_SAMPLER_COMPARE_GREATER              UINT32_C(0x00050000) //!< Compare when sampling depth texture: greater.
#define VRHI_SAMPLER_COMPARE_NOTEQUAL             UINT32_C(0x00060000) //!< Compare when sampling depth texture: not equal.
#define VRHI_SAMPLER_COMPARE_NEVER                UINT32_C(0x00070000) //!< Compare when sampling depth texture: never.
#define VRHI_SAMPLER_COMPARE_ALWAYS               UINT32_C(0x00080000) //!< Compare when sampling depth texture: always.
#define VRHI_SAMPLER_COMPARE_SHIFT                16

#define VRHI_SAMPLER_COMPARE_MASK                 UINT32_C(0x000f0000)

#define VRHI_SAMPLER_BORDER_COLOR_SHIFT           24

#define VRHI_SAMPLER_BORDER_COLOR_MASK            UINT32_C(0x0f000000)
#define VRHI_SAMPLER_BORDER_COLOR(v) ( ( (uint32_t)(v)<<VRHI_SAMPLER_BORDER_COLOR_SHIFT )&VRHI_SAMPLER_BORDER_COLOR_MASK)

#define VRHI_SAMPLER_RESERVED_SHIFT               28

#define VRHI_SAMPLER_RESERVED_MASK                UINT32_C(0xf0000000)

#define VRHI_SAMPLER_NONE                         UINT32_C(0x00000000)
#define VRHI_SAMPLER_SAMPLE_STENCIL               UINT32_C(0x00100000) //!< Sample stencil instead of depth.
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

#define VRHI_SAMPLER_BITS_MASK (0 \
	| VRHI_SAMPLER_U_MASK \
	| VRHI_SAMPLER_V_MASK \
	| VRHI_SAMPLER_W_MASK \
	| VRHI_SAMPLER_MIN_MASK \
	| VRHI_SAMPLER_MAG_MASK \
	| VRHI_SAMPLER_MIP_MASK \
	| VRHI_SAMPLER_COMPARE_MASK \
	)
