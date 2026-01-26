/*
    -- Vrhi Hello World --

    Minimal headless example showing VRHI initialisation and a simple clear operation.
*/

#include <vrhi.h>
#include <iostream>

int main( int argc, char** argv )
{
    // Configure for headless (compute/offscreen) mode
    g_vhInit.appName = "VrhiHelloWorld";
    g_vhInit.headless = true;
    
    // Initialise the RHI
    vhInit();

    // Create a small render target
    vhTexture rt = vhAllocTexture();
    vhCreateTexture2D( rt, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );

    // Setup state to clear the texture to a specific colour
    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.1f, 0.2f, 0.4f, 1.0f ) );

    // Submit the clear command
    vhStateId sid = 1;
    vhSetState( sid, state );
    vhClear( sid, VRHI_CLEAR_COLOR );

    // Wait for the GPU to finish
    vhFinish();

    std::cout << "VRHI Hello World: Screen cleared successfully (Headless)" << std::endl;

    // Cleanup resources
    vhDestroyTexture( rt );
    vhFlush();
    vhShutdown();

    return 0;
}
