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

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES // Define this if you have these in PCH already.
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

#include <nvrhi/vulkan.h>
#include "vrhi_defines.h"

#define VRHI_INVALID_HANDLE 0xFFFFFFFF

struct vhInitData
{
    std::string appName = "VRHI_APP";
    bool debug = false;
    int deviceIndex = -1; // -1 for auto-selection (Discrete > Integrated > CPU)
    glm::ivec2 resolution = glm::ivec2( 1280, 720 );
};

typedef uint32_t vhTexture;
extern vhInitData g_vhInit;
extern nvrhi::DeviceHandle g_vhDevice;

// -------------------------------------- Interface --------------------------------------

void vhInit();

void vhShutdown();

std::string vhGetDeviceInfo();

vhTexture vhAllocTexture();

void vhDestroyTexture( vhTexture texture );

void vhCreateTexture(
    vhTexture texture,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
);

// -------------------------------------- Implementation --------------------------------------

#ifdef VRHI_IMPLEMENTATION
#include "vrhi_impl.h"
#endif // VRHI_IMPLEMENTATION

