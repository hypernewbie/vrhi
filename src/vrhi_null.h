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

#include <nvrhi/nvrhi.h>
#include <nvrhi/common/aftermath.h>
#include <atomic>
#include <cstdint>

template< typename TInterface >
class vhNullResource : public TInterface
{
    std::atomic< unsigned long > m_refCount = 1;

public:
    unsigned long AddRef() override { return ++m_refCount; }
    unsigned long Release() override
    {
        auto r = --m_refCount;
        if ( r == 0 ) delete this;
        return r;
    }
    unsigned long GetRefCount() override { return m_refCount.load(); }
    nvrhi::Object getNativeObject( nvrhi::ObjectType ) override { return nullptr; }
};

class vhNullHeap : public vhNullResource< nvrhi::IHeap >
{
    nvrhi::HeapDesc m_desc;
public:
    vhNullHeap( const nvrhi::HeapDesc& desc ) : m_desc( desc ) {}
    const nvrhi::HeapDesc& getDesc() override { return m_desc; }
};

class vhNullTexture : public vhNullResource< nvrhi::ITexture >
{
    nvrhi::TextureDesc m_desc;
public:
    vhNullTexture( const nvrhi::TextureDesc& desc ) : m_desc( desc ) {}
    const nvrhi::TextureDesc& getDesc() const override { return m_desc; }
    nvrhi::Object getNativeView( nvrhi::ObjectType, nvrhi::Format, nvrhi::TextureSubresourceSet, nvrhi::TextureDimension, bool ) override { return nullptr; }
};

class vhNullStagingTexture : public vhNullResource< nvrhi::IStagingTexture >
{
    nvrhi::TextureDesc m_desc;
public:
    vhNullStagingTexture( const nvrhi::TextureDesc& desc ) : m_desc( desc ) {}
    const nvrhi::TextureDesc& getDesc() const override { return m_desc; }
};

class vhNullBuffer : public vhNullResource< nvrhi::IBuffer >
{
    nvrhi::BufferDesc m_desc;
public:
    vhNullBuffer( const nvrhi::BufferDesc& desc ) : m_desc( desc ) {}
    const nvrhi::BufferDesc& getDesc() const override { return m_desc; }
    nvrhi::GpuVirtualAddress getGpuVirtualAddress() const override { return 0; }
};

class vhNullShader : public vhNullResource< nvrhi::IShader >
{
    nvrhi::ShaderDesc m_desc;
public:
    vhNullShader( const nvrhi::ShaderDesc& desc ) : m_desc( desc ) {}
    const nvrhi::ShaderDesc& getDesc() const override { return m_desc; }
    void getBytecode( const void** ppBytecode, size_t* pSize ) const override
    {
        if ( ppBytecode ) *ppBytecode = nullptr;
        if ( pSize ) *pSize = 0;
    }
};

class vhNullShaderLibrary : public vhNullResource< nvrhi::IShaderLibrary >
{
public:
    void getBytecode( const void** ppBytecode, size_t* pSize ) const override
    {
        if ( ppBytecode ) *ppBytecode = nullptr;
        if ( pSize ) *pSize = 0;
    }
    nvrhi::ShaderHandle getShader( const char*, nvrhi::ShaderType ) override { return nullptr; }
};

class vhNullSampler : public vhNullResource< nvrhi::ISampler >
{
    nvrhi::SamplerDesc m_desc;
public:
    vhNullSampler( const nvrhi::SamplerDesc& desc ) : m_desc( desc ) {}
    const nvrhi::SamplerDesc& getDesc() const override { return m_desc; }
};

class vhNullInputLayout : public vhNullResource< nvrhi::IInputLayout >
{
public:
    uint32_t getNumAttributes() const override { return 0; }
    const nvrhi::VertexAttributeDesc* getAttributeDesc( uint32_t ) const override { return nullptr; }
};

class vhNullFramebuffer : public vhNullResource< nvrhi::IFramebuffer >
{
    nvrhi::FramebufferDesc m_desc;
    nvrhi::FramebufferInfoEx m_info;
public:
    vhNullFramebuffer( const nvrhi::FramebufferDesc& desc ) : m_desc( desc )
    {
        for ( const auto& at : m_desc.colorAttachments )
        {
            m_info.colorFormats.push_back( at.format );
        }
        m_info.depthFormat = m_desc.depthAttachment.format;
        m_info.sampleCount = 1;
        m_info.sampleQuality = 0;
    }
    const nvrhi::FramebufferDesc& getDesc() const override { return m_desc; }
    const nvrhi::FramebufferInfoEx& getFramebufferInfo() const override { return m_info; }
};

class vhNullBindingLayout : public vhNullResource< nvrhi::IBindingLayout >
{
    nvrhi::BindingLayoutDesc m_desc;
public:
    vhNullBindingLayout( const nvrhi::BindingLayoutDesc& desc ) : m_desc( desc ) {}
    const nvrhi::BindingLayoutDesc* getDesc() const override { return &m_desc; }
    const nvrhi::BindlessLayoutDesc* getBindlessDesc() const override { return nullptr; }
};

class vhNullBindingSet : public vhNullResource< nvrhi::IBindingSet >
{
    nvrhi::BindingLayoutHandle m_layout;
public:
    vhNullBindingSet( nvrhi::IBindingLayout* layout ) : m_layout( layout ) {}
    const nvrhi::BindingSetDesc* getDesc() const override { return nullptr; }
    nvrhi::IBindingLayout* getLayout() const override { return m_layout; }
};

class vhNullGraphicsPipeline : public vhNullResource< nvrhi::IGraphicsPipeline >
{
    nvrhi::GraphicsPipelineDesc m_desc;
    nvrhi::FramebufferInfo m_info;
public:
    vhNullGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& info ) : m_desc( desc ), m_info( info ) {}
    const nvrhi::GraphicsPipelineDesc& getDesc() const override { return m_desc; }
    const nvrhi::FramebufferInfo& getFramebufferInfo() const override { return m_info; }
};

class vhNullComputePipeline : public vhNullResource< nvrhi::IComputePipeline >
{
    nvrhi::ComputePipelineDesc m_desc;
public:
    vhNullComputePipeline( const nvrhi::ComputePipelineDesc& desc ) : m_desc( desc ) {}
    const nvrhi::ComputePipelineDesc& getDesc() const override { return m_desc; }
};

class vhNullMeshletPipeline : public vhNullResource< nvrhi::IMeshletPipeline >
{
    nvrhi::MeshletPipelineDesc m_desc;
    nvrhi::FramebufferInfo m_info;
public:
    vhNullMeshletPipeline( const nvrhi::MeshletPipelineDesc& desc, const nvrhi::FramebufferInfo& info ) : m_desc( desc ), m_info( info ) {}
    const nvrhi::MeshletPipelineDesc& getDesc() const override { return m_desc; }
    const nvrhi::FramebufferInfo& getFramebufferInfo() const override { return m_info; }
};

class vhNullEventQuery : public vhNullResource< nvrhi::IEventQuery > {};
class vhNullTimerQuery : public vhNullResource< nvrhi::ITimerQuery > {};

class vhNullOpacityMicromap : public vhNullResource< nvrhi::rt::IOpacityMicromap >
{
    nvrhi::rt::OpacityMicromapDesc m_desc;
public:
    vhNullOpacityMicromap( const nvrhi::rt::OpacityMicromapDesc& desc ) : m_desc( desc ) {}
    const nvrhi::rt::OpacityMicromapDesc& getDesc() const override { return m_desc; }
    bool isCompacted() const override { return false; }
    uint64_t getDeviceAddress() const override { return 0; }
};

class vhNullAccelStruct : public vhNullResource< nvrhi::rt::IAccelStruct >
{
    nvrhi::rt::AccelStructDesc m_desc;
public:
    vhNullAccelStruct( const nvrhi::rt::AccelStructDesc& desc ) : m_desc( desc ) {}
    const nvrhi::rt::AccelStructDesc& getDesc() const override { return m_desc; }
    bool isCompacted() const override { return false; }
    uint64_t getDeviceAddress() const override { return 0; }
};

class vhNullShaderTable : public vhNullResource< nvrhi::rt::IShaderTable >
{
public:
    void setRayGenerationShader( const char*, nvrhi::IBindingSet* ) override {}
    int addMissShader( const char*, nvrhi::IBindingSet* ) override { return 0; }
    int addHitGroup( const char*, nvrhi::IBindingSet* ) override { return 0; }
    int addCallableShader( const char*, nvrhi::IBindingSet* ) override { return 0; }
    void clearMissShaders() override {}
    void clearHitShaders() override {}
    void clearCallableShaders() override {}
    nvrhi::rt::IPipeline* getPipeline() override { return nullptr; }
};

class vhNullRTPipeline : public vhNullResource< nvrhi::rt::IPipeline >
{
    nvrhi::rt::PipelineDesc m_desc;
public:
    vhNullRTPipeline( const nvrhi::rt::PipelineDesc& desc ) : m_desc( desc ) {}
    const nvrhi::rt::PipelineDesc& getDesc() const override { return m_desc; }
    nvrhi::rt::ShaderTableHandle createShaderTable() override { return new vhNullShaderTable(); }
};

class vhNullCommandList : public vhNullResource< nvrhi::ICommandList >
{
    nvrhi::CommandListParameters m_params;
    nvrhi::IDevice* m_device;
public:
    vhNullCommandList( const nvrhi::CommandListParameters& params, nvrhi::IDevice* device ) : m_params( params ), m_device( device ) {}
    const nvrhi::CommandListParameters& getDesc() override { return m_params; }
    nvrhi::IDevice* getDevice() override { return m_device; }

    void open() override {}
    void close() override {}
    void clearState() override {}
    void clearTextureFloat( nvrhi::ITexture*, nvrhi::TextureSubresourceSet, const nvrhi::Color& ) override {}
    void clearDepthStencilTexture( nvrhi::ITexture*, nvrhi::TextureSubresourceSet, bool, float, bool, uint8_t ) override {}
    void clearTextureUInt( nvrhi::ITexture*, nvrhi::TextureSubresourceSet, uint32_t ) override {}
    void clearBufferUInt( nvrhi::IBuffer*, uint32_t ) override {}
    void copyTexture( nvrhi::ITexture*, const nvrhi::TextureSlice&, nvrhi::ITexture*, const nvrhi::TextureSlice& ) override {}
    void copyTexture( nvrhi::IStagingTexture*, const nvrhi::TextureSlice&, nvrhi::ITexture*, const nvrhi::TextureSlice& ) override {}
    void copyTexture( nvrhi::ITexture*, const nvrhi::TextureSlice&, nvrhi::IStagingTexture*, const nvrhi::TextureSlice& ) override {}
    void writeTexture( nvrhi::ITexture*, uint32_t, uint32_t, const void*, size_t, size_t ) override {}
    void resolveTexture( nvrhi::ITexture*, const nvrhi::TextureSubresourceSet&, nvrhi::ITexture*, const nvrhi::TextureSubresourceSet& ) override {}
    void writeBuffer( nvrhi::IBuffer*, const void*, size_t, uint64_t ) override {}
    void copyBuffer( nvrhi::IBuffer*, uint64_t, nvrhi::IBuffer*, uint64_t, uint64_t ) override {}
    void setPushConstants( const void*, size_t ) override {}
    void setGraphicsState( const nvrhi::GraphicsState& ) override {}
    void draw( const nvrhi::DrawArguments& ) override {}
    void drawIndexed( const nvrhi::DrawArguments& ) override {}
    void drawIndirect( uint32_t, uint32_t ) override {}
    void drawIndexedIndirect( uint32_t, uint32_t ) override {}
    void setComputeState( const nvrhi::ComputeState& ) override {}
    void dispatch( uint32_t, uint32_t, uint32_t ) override {}
    void dispatchIndirect( uint32_t ) override {}
    void setMeshletState( const nvrhi::MeshletState& ) override {}
    void dispatchMesh( uint32_t, uint32_t, uint32_t ) override {}
    void setRayTracingState( const nvrhi::rt::State& ) override {}
    void dispatchRays( const nvrhi::rt::DispatchRaysArguments& ) override {}
    void buildOpacityMicromap( nvrhi::rt::IOpacityMicromap*, const nvrhi::rt::OpacityMicromapDesc& ) override {}
    void buildTopLevelAccelStruct( nvrhi::rt::IAccelStruct*, const nvrhi::rt::InstanceDesc*, size_t, nvrhi::rt::AccelStructBuildFlags ) override {}
    void buildBottomLevelAccelStruct( nvrhi::rt::IAccelStruct*, const nvrhi::rt::GeometryDesc*, size_t, nvrhi::rt::AccelStructBuildFlags ) override {}
    void buildTopLevelAccelStructFromBuffer( nvrhi::rt::IAccelStruct*, nvrhi::IBuffer*, uint64_t, size_t, nvrhi::rt::AccelStructBuildFlags ) override {}
    void compactBottomLevelAccelStructs() override {}
    void executeMultiIndirectClusterOperation( const nvrhi::rt::cluster::OperationDesc& ) override {}
    void beginTimerQuery( nvrhi::ITimerQuery* ) override {}
    void endTimerQuery( nvrhi::ITimerQuery* ) override {}
    void beginMarker( const char* ) override {}
    void endMarker() override {}
    void setTextureState( nvrhi::ITexture*, nvrhi::TextureSubresourceSet, nvrhi::ResourceStates ) override {}
    void setBufferState( nvrhi::IBuffer*, nvrhi::ResourceStates ) override {}
    void setAccelStructState( nvrhi::rt::IAccelStruct*, nvrhi::ResourceStates ) override {}
    void commitBarriers() override {}
    nvrhi::ResourceStates getTextureSubresourceState( nvrhi::ITexture*, uint32_t, uint32_t ) override { return nvrhi::ResourceStates::Common; }
    nvrhi::ResourceStates getBufferState( nvrhi::IBuffer* ) override { return nvrhi::ResourceStates::Common; }
    void beginTrackingTextureState( nvrhi::ITexture*, nvrhi::TextureSubresourceSet, nvrhi::ResourceStates ) override {}
    void beginTrackingBufferState( nvrhi::IBuffer*, nvrhi::ResourceStates ) override {}
    void clearSamplerFeedbackTexture( nvrhi::ISamplerFeedbackTexture* ) override {}
    void decodeSamplerFeedbackTexture( nvrhi::IBuffer*, nvrhi::ISamplerFeedbackTexture*, nvrhi::Format ) override {}
    void setSamplerFeedbackTextureState( nvrhi::ISamplerFeedbackTexture*, nvrhi::ResourceStates ) override {}
    void setEnableAutomaticBarriers( bool ) override {}
    void setResourceStatesForBindingSet( nvrhi::IBindingSet* ) override {}
    void setEnableUavBarriersForTexture( nvrhi::ITexture*, bool ) override {}
    void setEnableUavBarriersForBuffer( nvrhi::IBuffer*, bool ) override {}
    void setPermanentTextureState( nvrhi::ITexture*, nvrhi::ResourceStates ) override {}
    void setPermanentBufferState( nvrhi::IBuffer*, nvrhi::ResourceStates ) override {}
    void convertCoopVecMatrices( const nvrhi::coopvec::ConvertMatrixLayoutDesc*, size_t ) override {}
};

class vhNullDevice : public vhNullResource< nvrhi::IDevice >
{
    std::atomic< uint64_t > m_instanceCounter = 0;

public:
    nvrhi::HeapHandle createHeap( const nvrhi::HeapDesc& desc ) override { return new vhNullHeap( desc ); }
    nvrhi::TextureHandle createTexture( const nvrhi::TextureDesc& desc ) override { return new vhNullTexture( desc ); }
    nvrhi::MemoryRequirements getTextureMemoryRequirements( nvrhi::ITexture* ) override { return { 0, 0 }; }
    bool bindTextureMemory( nvrhi::ITexture*, nvrhi::IHeap*, uint64_t ) override { return true; }
    nvrhi::TextureHandle createHandleForNativeTexture( nvrhi::ObjectType, nvrhi::Object, const nvrhi::TextureDesc& desc ) override { return new vhNullTexture( desc ); }
    nvrhi::StagingTextureHandle createStagingTexture( const nvrhi::TextureDesc& desc, nvrhi::CpuAccessMode ) override { return new vhNullStagingTexture( desc ); }
    void* mapStagingTexture( nvrhi::IStagingTexture*, const nvrhi::TextureSlice&, nvrhi::CpuAccessMode, size_t* pRowPitch ) override
    {
        if ( pRowPitch ) *pRowPitch = 0;
        return nullptr;
    }
    void unmapStagingTexture( nvrhi::IStagingTexture* ) override {}
    nvrhi::BufferHandle createBuffer( const nvrhi::BufferDesc& desc ) override { return new vhNullBuffer( desc ); }
    nvrhi::MemoryRequirements getBufferMemoryRequirements( nvrhi::IBuffer* ) override { return { 0, 0 }; }
    bool bindBufferMemory( nvrhi::IBuffer*, nvrhi::IHeap*, uint64_t ) override { return true; }
    nvrhi::BufferHandle createHandleForNativeBuffer( nvrhi::ObjectType, nvrhi::Object, const nvrhi::BufferDesc& desc ) override { return new vhNullBuffer( desc ); }
    void* mapBuffer( nvrhi::IBuffer*, nvrhi::CpuAccessMode ) override { return nullptr; }
    void unmapBuffer( nvrhi::IBuffer* ) override {}
    nvrhi::ShaderHandle createShader( const nvrhi::ShaderDesc& desc, const void*, size_t ) override { return new vhNullShader( desc ); }
    nvrhi::ShaderHandle createShaderSpecialization( nvrhi::IShader* baseShader, const nvrhi::ShaderSpecialization*, uint32_t ) override { return new vhNullShader( baseShader->getDesc() ); }
    nvrhi::ShaderLibraryHandle createShaderLibrary( const void*, size_t ) override { return new vhNullShaderLibrary(); }
    nvrhi::SamplerHandle createSampler( const nvrhi::SamplerDesc& desc ) override { return new vhNullSampler( desc ); }
    nvrhi::InputLayoutHandle createInputLayout( const nvrhi::VertexAttributeDesc*, uint32_t, nvrhi::IShader* ) override { return new vhNullInputLayout(); }
    nvrhi::EventQueryHandle createEventQuery() override { return new vhNullEventQuery(); }
    void setEventQuery( nvrhi::IEventQuery*, nvrhi::CommandQueue ) override {}
    bool pollEventQuery( nvrhi::IEventQuery* ) override { return true; }
    void waitEventQuery( nvrhi::IEventQuery* ) override {}
    void resetEventQuery( nvrhi::IEventQuery* ) override {}
    nvrhi::TimerQueryHandle createTimerQuery() override { return new vhNullTimerQuery(); }
    bool pollTimerQuery( nvrhi::ITimerQuery* ) override { return true; }
    float getTimerQueryTime( nvrhi::ITimerQuery* ) override { return 0.0f; }
    void resetTimerQuery( nvrhi::ITimerQuery* ) override {}
    nvrhi::BindingLayoutHandle createBindingLayout( const nvrhi::BindingLayoutDesc& desc ) override { return new vhNullBindingLayout( desc ); }
    nvrhi::BindingLayoutHandle createBindlessLayout( const nvrhi::BindlessLayoutDesc& ) override { return new vhNullBindingLayout( nvrhi::BindingLayoutDesc() ); }
    nvrhi::BindingSetHandle createBindingSet( const nvrhi::BindingSetDesc&, nvrhi::IBindingLayout* layout ) override { return new vhNullBindingSet( layout ); }
    nvrhi::DescriptorTableHandle createDescriptorTable( nvrhi::IBindingLayout* ) override { return nullptr; }
    void resizeDescriptorTable( nvrhi::IDescriptorTable*, uint32_t, bool ) override {}
    bool writeDescriptorTable( nvrhi::IDescriptorTable*, const nvrhi::BindingSetItem& ) override { return true; }
    nvrhi::FramebufferHandle createFramebuffer( const nvrhi::FramebufferDesc& desc ) override { return new vhNullFramebuffer( desc ); }
    nvrhi::GraphicsPipelineHandle createGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& info ) override { return new vhNullGraphicsPipeline( desc, info ); }
    nvrhi::GraphicsPipelineHandle createGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, nvrhi::IFramebuffer* fb ) override { return new vhNullGraphicsPipeline( desc, fb->getFramebufferInfo() ); }
    nvrhi::ComputePipelineHandle createComputePipeline( const nvrhi::ComputePipelineDesc& desc ) override { return new vhNullComputePipeline( desc ); }
    nvrhi::MeshletPipelineHandle createMeshletPipeline( const nvrhi::MeshletPipelineDesc& desc, const nvrhi::FramebufferInfo& info ) override { return new vhNullMeshletPipeline( desc, info ); }
    nvrhi::MeshletPipelineHandle createMeshletPipeline( const nvrhi::MeshletPipelineDesc& desc, nvrhi::IFramebuffer* fb ) override { return new vhNullMeshletPipeline( desc, fb->getFramebufferInfo() ); }
    nvrhi::rt::OpacityMicromapHandle createOpacityMicromap( const nvrhi::rt::OpacityMicromapDesc& desc ) override { return new vhNullOpacityMicromap( desc ); }
    nvrhi::rt::AccelStructHandle createAccelStruct( const nvrhi::rt::AccelStructDesc& desc ) override { return new vhNullAccelStruct( desc ); }
    nvrhi::MemoryRequirements getAccelStructMemoryRequirements( nvrhi::rt::IAccelStruct* ) override { return { 0, 0 }; }
    bool bindAccelStructMemory( nvrhi::rt::IAccelStruct*, nvrhi::IHeap*, uint64_t ) override { return true; }
    nvrhi::rt::PipelineHandle createRayTracingPipeline( const nvrhi::rt::PipelineDesc& desc ) override { return new vhNullRTPipeline( desc ); }
    nvrhi::CommandListHandle createCommandList( const nvrhi::CommandListParameters& params ) override { return new vhNullCommandList( params, this ); }
    uint64_t executeCommandLists( nvrhi::ICommandList* const* pCommandLists, size_t numCommandLists, nvrhi::CommandQueue executionQueue = nvrhi::CommandQueue::Graphics ) override { return ++m_instanceCounter; }
    void queueWaitForCommandList( nvrhi::CommandQueue, nvrhi::CommandQueue, uint64_t ) override {}
    bool waitForIdle() override { return true; }
    void runGarbageCollection() override {}
    bool queryFeatureSupport( nvrhi::Feature, void*, size_t ) override { return false; }
    nvrhi::FormatSupport queryFormatSupport( nvrhi::Format ) override { return nvrhi::FormatSupport::None; }
    nvrhi::Object getNativeQueue( nvrhi::ObjectType, nvrhi::CommandQueue ) override { return nullptr; }
    nvrhi::IMessageCallback* getMessageCallback() override { return nullptr; }
    nvrhi::GraphicsAPI getGraphicsAPI() override { return nvrhi::GraphicsAPI::VULKAN; }
    bool isAftermathEnabled() override { return false; }
    nvrhi::AftermathCrashDumpHelper& getAftermathCrashDumpHelper() override { static nvrhi::AftermathCrashDumpHelper d; return d; }
    void getTextureTiling( nvrhi::ITexture*, uint32_t*, nvrhi::PackedMipDesc*, nvrhi::TileShape*, uint32_t*, nvrhi::SubresourceTiling* ) override {}
    void updateTextureTileMappings( nvrhi::ITexture*, const nvrhi::TextureTilesMapping*, uint32_t, nvrhi::CommandQueue ) override {}
    nvrhi::SamplerFeedbackTextureHandle createSamplerFeedbackTexture( nvrhi::ITexture*, const nvrhi::SamplerFeedbackTextureDesc& ) override { return nullptr; }
    nvrhi::SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture( nvrhi::ObjectType, nvrhi::Object, nvrhi::ITexture* ) override { return nullptr; }
    nvrhi::coopvec::DeviceFeatures queryCoopVecFeatures() override { return {}; }
    size_t getCoopVecMatrixSize( nvrhi::coopvec::DataType, nvrhi::coopvec::MatrixLayout, int, int ) override { return 0; }
    nvrhi::rt::cluster::OperationSizeInfo getClusterOperationSizeInfo( const nvrhi::rt::cluster::OperationParams& ) override { return {}; }
};
