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

#ifndef VRHI_IMPLEMENTATION
#include "vrhi_impl.h"
#endif // VRHI_IMPLEMENTATION
#include "vrhi_utils.h"

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <cctype>
#include <cstring>
#include <vector>
#include <string>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

struct vhVertexLayoutDef
{
    std::string semantic;
    std::string type;
    int semanticIndex;
    int componentCount;
    int offset;
};

// Helper to get element size of a base type
int vhGetBaseTypeSize( const std::string& type )
{
    if ( type == "float" || type == "int" || type == "uint" ) return 4;
    if ( type == "half" || type == "short" || type == "ushort" ) return 2;
    if ( type == "byte" || type == "ubyte" ) return 1;
    return 0;
}

template <bool EmitOutput>
bool vhParseVertexLayout( const vhVertexLayout& layout, std::vector<vhVertexLayoutDef>* outDefs )
{
    if constexpr ( EmitOutput )
    {
        if ( outDefs ) outDefs->clear();
    }

    const char* ptr = layout.c_str();
    const char* baseTypes[] = { "float", "half", "int", "uint", "short", "ushort", "byte", "ubyte", nullptr };
    int currentOffset = 0;
    int attributeCount = 0;

    while ( *ptr )
    {
        // Skip whitespace
        while ( *ptr && isspace( *ptr ) ) ptr++;
        if ( !*ptr ) break; 

        // 1. Extract Type Token
        const char* tStart = ptr;
        while ( *ptr && !isspace( *ptr ) ) ptr++;
        std::string typeStr(tStart, ptr);

        // Validate Type
        bool typeValid = false;
        std::string baseType;
        int componentCount = 1;

        for ( const char** bt = baseTypes; *bt; ++bt )
        {
            std::string base = *bt;
            if ( typeStr.size() >= base.size() && typeStr.compare(0, base.size(), base) == 0 )
            {
                std::string suffix = typeStr.substr( base.size() );
                if ( suffix.empty() )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = 1;
                    break;
                }
                else if ( suffix.size() == 1 && suffix[0] >= '2' && suffix[0] <= '4' )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = suffix[0] - '0';
                    break;
                }
            }
        }
        if ( !typeValid ) return false;

        // Skip whitespace between Type and Semantic
        while ( *ptr && isspace( *ptr ) ) ptr++;
        if ( !*ptr ) return false; // Unexpected end, missing semantic

        // 2. Extract Semantic Token
        const char* sStart = ptr;
        while ( *ptr && !isspace( *ptr ) ) ptr++;
        std::string semanticFull(sStart, ptr);

        // Validate Semantic: Must be uppercase letters/digits, starting with letter
        if ( semanticFull.empty() || !isupper(semanticFull[0]) ) return false;
        
        std::string semanticName;
        int semanticIndex = 0;

        // Extract potential index from end of semantic
        size_t lastAlpha = std::string::npos;
        for ( size_t i = 0; i < semanticFull.size(); ++i )
        {
            if ( isalpha( semanticFull[i] ) ) lastAlpha = i;
            if ( !isalnum( semanticFull[i] ) ) return false; // Invalid char
        }
        
        // If the string has digits at the end
        if ( lastAlpha != std::string::npos && lastAlpha + 1 < semanticFull.size() )
        {
            semanticName = semanticFull.substr( 0, lastAlpha + 1 );
            std::string indexStr = semanticFull.substr( lastAlpha + 1 );
            // Validate digits
            for ( char c : indexStr ) if ( !isdigit(c) ) return false;
            semanticIndex = std::stoi( indexStr );
        }
        else
        {
            semanticName = semanticFull;
            semanticIndex = 0;
        }

        // Validate Semantic Name (Must be all upper)
        for ( char c : semanticName )
        {
            if ( isalpha(c) && !isupper(c) ) return false; 
        }

        int typeSize = vhGetBaseTypeSize( baseType );
        int sizeBytes = typeSize * componentCount;

        if constexpr ( EmitOutput )
        {
            if ( outDefs )
            {
                vhVertexLayoutDef def;
                def.semantic = semanticName;
                def.type = baseType;
                def.semanticIndex = semanticIndex;
                def.componentCount = componentCount;
                def.offset = currentOffset;
                outDefs->push_back( def );
            }
        }
        
        currentOffset += sizeBytes;
        attributeCount++;
    }

    return attributeCount > 0;
}

bool vhValidateVertexLayout( const vhVertexLayout& layout )
{
    return vhParseVertexLayout<false>( layout, nullptr );
}