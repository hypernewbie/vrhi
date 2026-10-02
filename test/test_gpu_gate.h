/*
    -- Vrhi --
    Test-owned timeline gate and bounded cleanup for pending GPU readers.
*/

#pragma once

#include "test.h"
#include <vrhi_internal.h>
#include <nvrhi/vulkan.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>

namespace vrhi_test
{

template< typename Predicate >
bool WaitUntil( Predicate predicate, std::chrono::seconds timeout = std::chrono::seconds( 5 ) )
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( !predicate() )
    {
        if ( std::chrono::steady_clock::now() >= deadline ) return false;
        std::this_thread::yield();
    }
    return true;
}

class VhGpuGate
{
public:
    explicit VhGpuGate( int* result ) : result( result ), device( vhGetVkDevice() )
    {
        VkPhysicalDeviceVulkan12Features features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceFeatures2 query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        query.pNext = &features;
        vkGetPhysicalDeviceFeatures2( vhGetVkPhysicalDevice(), &query );
        if ( !features.timelineSemaphore ) return;
        VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo create = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        create.pNext = &type;
        createResult = vkCreateSemaphore( device, &create, nullptr, &semaphore );
        if ( createResult != VK_SUCCESS ) Fail( "vkCreateSemaphore failed" );
    }

    ~VhGpuGate()
    {
        Release();
        JoinProducer();
        if ( watchdog.joinable() ) watchdog.join();
        if ( timedOut.load() ) Fail( "GPU gate watchdog expired" );
        if ( createResult != VK_SUCCESS ) Fail( "vkCreateSemaphore failed" );
        if ( signalResult != VK_SUCCESS ) Fail( "vkSignalSemaphore failed" );
        if ( armed ) Drain();
        if ( semaphore ) vkDestroySemaphore( device, semaphore, nullptr );
    }

    VhGpuGate( const VhGpuGate& ) = delete;
    VhGpuGate& operator=( const VhGpuGate& ) = delete;

    bool Supported() const { return semaphore != VK_NULL_HANDLE; }
    bool TimedOut() const { return timedOut.load(); }

    void WaitBefore( nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        g_vhVulkanDevice->queueWaitForSemaphore( queue, semaphore, 1 );
        armed = true;
        this->queue = queue;
        watchdog = std::thread( [this]
        {
            std::unique_lock< std::mutex > lock( mutex );
            if ( released.wait_for( lock, std::chrono::seconds( 5 ), [this] { return signalled; } ) ) return;
            timedOut.store( true );
            lock.unlock();
            Release();
        } );
    }

    void CaptureReader()
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        reader = g_vhDevice->createEventQuery();
        g_vhDevice->setEventQuery( reader, queue );
    }

    bool Pending() const { return reader && !g_vhDevice->pollEventQuery( reader ); }

    void Release()
    {
        std::lock_guard< std::mutex > lock( mutex );
        if ( signalled ) return;
        if ( semaphore )
        {
            VkSemaphoreSignalInfo signal = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
            signal.semaphore = semaphore;
            signal.value = 1;
            signalResult = vkSignalSemaphore( device, &signal );
        }
        signalled = true;
        released.notify_all();
    }

    template< typename Producer >
    void StartProducer( Producer body )
    {
        std::packaged_task< void() > task( std::move( body ) );
        completion = task.get_future();
        producer = std::thread( std::move( task ) );
    }

    bool ProducerFinished( std::chrono::milliseconds timeout = std::chrono::milliseconds( 0 ) )
    {
        return completion.valid() && completion.wait_for( timeout ) == std::future_status::ready;
    }

    void JoinProducer()
    {
        if ( !producer.joinable() ) return;
        if ( !ProducerFinished( std::chrono::seconds( 30 ) ) ) Abort( "producer failed to drain after gate release" );
        producer.join();
        try
        {
            completion.get();
        }
        catch ( ... )
        {
            Fail( "producer threw an exception" );
        }
    }

private:
    void Fail( const char* message )
    {
        std::fprintf( stderr, "VhGpuGate: %s\n", message );
        *result = UTEST_TEST_FAILURE;
    }

    void Abort( const char* message )
    {
        Fail( message );
        std::abort();
    }

    void Drain()
    {
        vhFlush( true );
        nvrhi::EventQueryHandle done[( uint32_t ) nvrhi::CommandQueue::Count];
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            // Consume an armed wait even if an assertion prevented its intended submit.
            for ( uint32_t i = 0; i < ( uint32_t ) nvrhi::CommandQueue::Count; ++i )
            {
                auto type = ( nvrhi::CommandQueue ) i;
                auto commands = g_vhDevice->createCommandList( nvrhi::CommandListParameters().setQueueType( type ) );
                commands->open();
                commands->close();
                g_vhDevice->executeCommandList( commands, type );
                done[i] = g_vhDevice->createEventQuery();
                g_vhDevice->setEventQuery( done[i], type );
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
        for ( auto& query : done )
        {
            while ( !g_vhDevice->pollEventQuery( query ) )
            {
                if ( std::chrono::steady_clock::now() >= deadline ) Abort( "GPU failed to drain after gate release" );
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        }
    }

    int* result;
    VkDevice device;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult createResult = VK_SUCCESS;
    VkResult signalResult = VK_SUCCESS;
    nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics;
    nvrhi::EventQueryHandle reader;
    bool armed = false;
    bool signalled = false;
    std::atomic< bool > timedOut = false;
    std::mutex mutex;
    std::condition_variable released;
    std::thread watchdog;
    std::thread producer;
    std::future< void > completion;
};

}
