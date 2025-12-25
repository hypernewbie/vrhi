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


// -------------------------------------------------------- Utils --------------------------------------------------------

// Allocator for list of object IDs.
// Works be using a free list.
class vhAllocatorObjectFreeList
{
    vhAllocatorObjectFreeList( const vhAllocatorObjectFreeList& ) = delete;
    vhAllocatorObjectFreeList& operator=( const vhAllocatorObjectFreeList& ) = delete;

    std::vector< uint32_t > m_freeList;
    uint32_t m_allocCount = 0;
    uint32_t m_size = 0;
    uint32_t m_end = 0;

public:
    vhAllocatorObjectFreeList() {}
    vhAllocatorObjectFreeList( uint32_t sz ) { m_size = sz; }

    int32_t alloc( int32_t size = 1 /* unused */, int32_t algn = 0 /* unused */ )
    {
        if ( size == 0 ) return -1;
        if ( size != 1 ) return -1;
        if ( algn != 0 && algn != -1 ) return -1;

        uint32_t ret = m_end;

        if ( m_freeList.size( ) > 0 ) {
            ret = m_freeList[m_freeList.size( ) - 1];
            m_freeList.pop_back( );
            m_allocCount++;
            return ret;
        }
        if ( ( uint32_t ) ret + size > m_size ) {
            return -1;
        }
        m_end = ret + size;
        m_allocCount++;
        return ret;
    }

    void release( int32_t addr )
    {
        m_freeList.push_back( addr );
        m_allocCount--;
        return;
    }

    void purge()
    {
        m_freeList.clear( );
        m_end = 0;
        m_allocCount = 0;
    }
};

// Allocator for hot objects that have low number of unique sizes.
// Works by using a free list of objects that have been previously allocated, and reusing them when possible.
// Thread-unsafe; protect with external lock if needed.
class vhRecycleAllocator
{
    vhRecycleAllocator( const vhRecycleAllocator& ) = delete;
    vhRecycleAllocator& operator=( const vhRecycleAllocator& ) = delete;

    std::unordered_map< size_t, std::vector< void* > > m_freeListBySize;

public:
    vhRecycleAllocator() {};
    virtual ~vhRecycleAllocator() { purge(); };

    // Allocates and constructs an object T using arguments Args...
    template< typename T, typename... Args >
    T* alloc( Args&&... args )
    {
        size_t size = sizeof(T);
        void* ptr = nullptr;

        // 1. Try to reuse memory
        auto it = m_freeListBySize.find( size );
        if ( it != m_freeListBySize.end() && !it->second.empty() )
        {
            ptr = it->second.back();
            it->second.pop_back();
        }
        else
        {
            ptr = ::operator new(size);
        }

        return new ( ptr ) T( std::forward<Args>(args)... );
    }

    // Destroys and releases an object T
    template< typename T >
    void release( T* obj )
    {
        if ( !obj ) return;
        obj->~T();
        m_freeListBySize[sizeof(T)].push_back( (void*)obj );
    }

    void purge()
    {
        for ( auto& pair : m_freeListBySize )
        {
            for ( void* ptr : pair.second )
            {
                ::operator delete(ptr);
            }
        }
        m_freeListBySize.clear();
    }
};