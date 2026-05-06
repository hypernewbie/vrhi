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

#include "vrhi_internal.h"

#if defined( _MSC_VER ) && !defined( __clang__ )
#include <intrin.h>
static inline uint32_t vhCtzll( uint64_t v )
{
    unsigned long idx;
    _BitScanForward64( &idx, v );
    return ( uint32_t ) idx;
}
#else
static inline uint32_t vhCtzll( uint64_t v ) { return ( uint32_t ) __builtin_ctzll( v ); }
#endif

struct vhBackendTexture;
struct vhBackendBuffer;
struct vhBackendShader;
struct vhBackendAccelStruct;

// Per-draw resolved binding state. Cleared between draws via Clear().
struct vhStateResolveCache
{
    bool init = false;

    std::vector< vhBackendTexture* > btex;
    std::vector< vhBackendBuffer* > bbuf;
    std::vector< vhBackendShader* > bshaders;
    std::vector< vhBackendAccelStruct* > baccel;

    struct ResolvedTexture
    {
        nvrhi::ITexture* handle = nullptr;
        const vhState::TextureBinding* binding = nullptr;
    };

    struct ResolvedBuffer
    {
        nvrhi::IBuffer* handle = nullptr;
        const vhState::BufferBinding* binding = nullptr;
    };

    struct ResolvedAccelStruct
    {
        nvrhi::rt::IAccelStruct* handle = nullptr;
        const vhState::AccelStructBinding* binding = nullptr;
    };

    struct ShaderStageBindingSlotState
    {
        static constexpr uint32_t MAX_SAMPLERS = 64;
        static constexpr uint32_t MAX_TEXTURES = 64;
        static constexpr uint32_t MAX_BUFFERS  = 64;
        static constexpr uint32_t MAX_UAVS     = 64;

        std::vector< nvrhi::ISampler* > samplerTable;
        std::vector< ResolvedTexture > textureTable;
        std::vector< ResolvedBuffer > bufferTable;
        std::vector< std::pair< ResolvedTexture, ResolvedBuffer > > uavTable;
        std::vector< ResolvedAccelStruct > accelStructTable;

        // Bit N corresponds to absolute slot (regShift + N) when N < 64; higher slots overflow to writtenSlots.
        uint64_t samplerUsed = 0;
        uint64_t textureUsed = 0;
        uint64_t bufferUsed = 0;
        uint64_t uavTextureUsed = 0;
        uint64_t uavBufferUsed = 0;
        uint64_t accelStructUsed = 0;

        struct WrittenSlot
        {
            enum Kind : uint8_t { Sampler, Texture, BufferB, BufferT, UavTexture, UavBuffer, AccelStruct };
            Kind kind;
            uint32_t slot;
        };
        std::vector< WrittenSlot > writtenSlots;

        uint32_t userGlobalsSlot = UINT32_MAX;
        uint64_t userGlobalsHash = 0;
        const vhShaderReflectionResource* userGlobalsReflection = nullptr;
        uint32_t globalUniformsSlot = UINT32_MAX;
        uint32_t worldUniformsSlot = UINT32_MAX;

        inline void MarkSlotUsed( WrittenSlot::Kind kind, uint32_t bit, uint32_t slot )
        {
            if ( bit >= 64 )
            {
                writtenSlots.push_back( { kind, slot } );
                return;
            }
            const uint64_t one = 1ULL << bit;
            switch ( kind )
            {
                case WrittenSlot::Sampler:     samplerUsed     |= one; break;
                case WrittenSlot::Texture:     textureUsed     |= one; break;
                case WrittenSlot::BufferB:     bufferUsed      |= one; break;
                case WrittenSlot::BufferT:     writtenSlots.push_back( { kind, slot } ); break; // t-shift has no mask
                case WrittenSlot::UavTexture:  uavTextureUsed  |= one; break;
                case WrittenSlot::UavBuffer:   uavBufferUsed   |= one; break;
                case WrittenSlot::AccelStruct: accelStructUsed |= one; break;
            }
        }
    };

    ShaderStageBindingSlotState stageBindingStorage[ VRHI_SHADER_STAGE_MAX + 1 ];
    bool stageBindingActive[ VRHI_SHADER_STAGE_MAX + 1 ] = {};

    struct UserGlobalUniformsBufferInfo
    {
        nvrhi::BufferHandle buffer;
        nvrhi::BufferRange range;
    };
    std::unordered_map< uint64_t, UserGlobalUniformsBufferInfo > userGlobalUniformsBufferCache;

    inline void Clear()
    {
        init = false;
        btex.clear();
        bbuf.clear();
        bshaders.clear();
        baccel.clear();

        const uint32_t sShift = g_vhInit.shaderMake_sRegShift;
        const uint32_t tShift = g_vhInit.shaderMake_tRegShift;
        const uint32_t bShift = g_vhInit.shaderMake_bRegShift;
        const uint32_t uShift = g_vhInit.shaderMake_uRegShift;

        for ( int i = 1; i <= VRHI_SHADER_STAGE_MAX; i++ )
        {
            if ( !stageBindingActive[i] ) continue;
            auto& s = stageBindingStorage[i];

            // Walk only entries we wrote this draw — bitmasks for slot < 64, vector for the rest.
            while ( s.samplerUsed )
            {
                uint32_t b = vhCtzll( s.samplerUsed );
                s.samplerUsed &= s.samplerUsed - 1;
                uint32_t idx = sShift + b;
                if ( idx < s.samplerTable.size() ) s.samplerTable[idx] = nullptr;
            }
            while ( s.textureUsed )
            {
                uint32_t b = vhCtzll( s.textureUsed );
                s.textureUsed &= s.textureUsed - 1;
                uint32_t idx = tShift + b;
                if ( idx < s.textureTable.size() ) s.textureTable[idx] = {};
            }
            while ( s.bufferUsed )
            {
                uint32_t b = vhCtzll( s.bufferUsed );
                s.bufferUsed &= s.bufferUsed - 1;
                uint32_t idx = bShift + b;
                if ( idx < s.bufferTable.size() ) s.bufferTable[idx] = {};
            }
            while ( s.uavTextureUsed )
            {
                uint32_t b = vhCtzll( s.uavTextureUsed );
                s.uavTextureUsed &= s.uavTextureUsed - 1;
                uint32_t idx = uShift + b;
                if ( idx < s.uavTable.size() ) s.uavTable[idx].first = {};
            }
            while ( s.uavBufferUsed )
            {
                uint32_t b = vhCtzll( s.uavBufferUsed );
                s.uavBufferUsed &= s.uavBufferUsed - 1;
                uint32_t idx = uShift + b;
                if ( idx < s.uavTable.size() ) s.uavTable[idx].second = {};
            }
            while ( s.accelStructUsed )
            {
                uint32_t b = vhCtzll( s.accelStructUsed );
                s.accelStructUsed &= s.accelStructUsed - 1;
                uint32_t idx = tShift + b;
                if ( idx < s.accelStructTable.size() ) s.accelStructTable[idx] = {};
            }

            using WS = ShaderStageBindingSlotState::WrittenSlot;
            for ( const auto& w : s.writtenSlots )
            {
                switch ( w.kind )
                {
                    case WS::Sampler:
                        if ( w.slot < s.samplerTable.size() ) s.samplerTable[w.slot] = nullptr;
                        break;
                    case WS::Texture:
                        if ( w.slot < s.textureTable.size() ) s.textureTable[w.slot] = {};
                        break;
                    case WS::BufferB:
                    case WS::BufferT:
                        if ( w.slot < s.bufferTable.size() ) s.bufferTable[w.slot] = {};
                        break;
                    case WS::UavTexture:
                        if ( w.slot < s.uavTable.size() ) s.uavTable[w.slot].first = {};
                        break;
                    case WS::UavBuffer:
                        if ( w.slot < s.uavTable.size() ) s.uavTable[w.slot].second = {};
                        break;
                    case WS::AccelStruct:
                        if ( w.slot < s.accelStructTable.size() ) s.accelStructTable[w.slot] = {};
                        break;
                }
            }
            s.writtenSlots.clear();

            s.userGlobalsSlot = UINT32_MAX;
            s.userGlobalsHash = 0;
            s.userGlobalsReflection = nullptr;
            s.globalUniformsSlot = UINT32_MAX;
            s.worldUniformsSlot = UINT32_MAX;
            stageBindingActive[i] = false;
        }
        userGlobalUniformsBufferCache.clear();
    }
};
