#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rendering/material_program_layout_internal.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/streaming/streaming.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>

namespace
{
    using namespace vanguard;
    namespace material = vanguard::materials;
    namespace pipeline = vanguard::pipelines;
    namespace shader = vanguard::shaders;

    using ByteArray = containers::DynamicArray<u8>;

    constexpr resources::ResourceTypeId TextureType = vanguard::serialization::MakeFourCC('V', 'T', 'E', 'X');

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[materialResourceTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    template <typename T> [[nodiscard]] T* AllocateResourceObject() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Resources, sizeof(T), alignof(T));
        return block ? new (block.address) T() : nullptr;
    }

    template <typename T> void DeleteResourceObject(T* const object) noexcept
    {
        if (object == nullptr)
            return;
        object->~T();
        memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Resources};
        memory::Free(block);
    }

    class TextureResourceObject final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return TextureType;
        }
    };

    resources::ResourceObject* DecodeTexture(const resources::ResourceReference reference, const void* const data, const usize size,
                                             const resources::LoadContext& context, resources::Failure& failure, void*) noexcept
    {
        failure = resources::Failure::None;
        if (reference.ExpectedType() != TextureType || context.Reference() != reference || data == nullptr || size == 0 || context.GetDependencyCount() != 0)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }
        TextureResourceObject* const object = AllocateResourceObject<TextureResourceObject>();
        if (object == nullptr)
            failure = resources::Failure::OutOfMemory;
        return object;
    }

    void DestroyTexture(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResourceObject(static_cast<TextureResourceObject*>(resource));
    }

    [[nodiscard]] bool SaveBytes(const filesystem::AbsolutePath& path, const void* const data, const usize size) noexcept
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        if (size != 0)
            writer->Serialize(const_cast<void*>(data), size);
        writer->Flush();
        return writer->GetSize() == size;
    }

    [[nodiscard]] bool SaveBytes(const filesystem::AbsolutePath& path, const ByteArray& bytes) noexcept
    {
        return SaveBytes(path, bytes.Data(), bytes.Size());
    }

    void WaitForDrain(resources::ResourcePipeline& resourcePipeline, streaming::ResourceStreamer& streamer) noexcept
    {
        for (u32 attempt = 0; attempt < 10000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = resourcePipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeJobs == 0 && pipelineStats.activeOperations == 0 && pipelineStats.activePreparations == 0 &&
                streamingStats.activeLoads == 0 && streamingStats.activeReads == 0 && streamingStats.stagingBytesInUse == 0)
                return;
            concurrency::SleepOnCurrentThread(1);
        }
        Check(false, "resource and streaming work drains");
    }

    struct ArtifactFixture final
    {
        ArtifactFixture() noexcept
            : shaderBytes(memory::pools::Rendering::GetInstance()), pipelineBytes(memory::pools::Rendering::GetInstance()),
              materialBytes(memory::pools::Rendering::GetInstance())
        {
        }

        resources::ResourceReference shaderReference{resources::ResourcePath::FromString("runtime/material_test.vshader"), shader::ShaderResourceType};
        resources::ResourceReference pipelineReference{resources::ResourcePath::FromString("runtime/material_test.vppl"), pipeline::PipelineResourceType};
        resources::ResourceReference materialReference{resources::ResourcePath::FromString("runtime/material_test.vmat"), material::MaterialResourceType};
        resources::ResourceReference requiredTexture{resources::ResourcePath::FromString("runtime/required.vtex"), TextureType};
        resources::ResourceReference optionalTexture{resources::ResourcePath::FromString("runtime/missing_optional.vtex"), TextureType};
        resources::ResourceReference softTexture{resources::ResourcePath::FromString("runtime/missing_soft.vtex"), TextureType};
        ByteArray shaderBytes;
        ByteArray pipelineBytes;
        ByteArray materialBytes;
        crypto::Digest256 shaderPermutation;
        crypto::Digest256 shaderBindingLayout;
        crypto::Digest256 pipelineTemplate;
        crypto::Digest256 materialContent;
    };

    [[nodiscard]] bool BuildFixture(ArtifactFixture& output, const u32 variant = 0) noexcept
    {
        const std::array<u8, 8> vertexBytes{{1, 2, 3, 4, 5, 6, 7, 8}};
        const std::array<u8, 8> fragmentBytes{{8, 7, 6, 5, 4, 3, 2, 1}};
        const std::array<shader::StageBuildRecord, 2> stages{
            {{shader::ShaderStage::Vertex, shader::NativeFormat::Dxil, 0x1001, vertexBytes.data(), vertexBytes.size(), "mainVS"},
             {shader::ShaderStage::Fragment, shader::NativeFormat::Dxil, 0x1002, fragmentBytes.data(), fragmentBytes.size(), "mainPS"}}};
        const std::array<shader::DescriptorBinding, 2> bindings{
            {{0x1000, 2, 0, 1, shader::BindingKind::ConstantBuffer, shader::BindingAccess::Read, shader::StageBit(shader::ShaderStage::Fragment)},
             {0x2000, 2, 1, 3, shader::BindingKind::SampledTexture, shader::BindingAccess::Read, shader::StageBit(shader::ShaderStage::Fragment)}}};
        const std::array<shader::ConstantBuffer, 1> buffers{{{0x1000, 2, 0, 16, 0, 1}}};
        const std::array<shader::ConstantMember, 1> members{{{0x1100, 0, 16, 0, 0, shader::ScalarType::F32, 1, 4, false}}};
        const crypto::Digest256 textureType = crypto::Sha256("Texture2D<float4>", 17);
        const shader::MaterialResourceShape textureShape{shader::MaterialResourceAccess::Read, shader::MaterialTextureDimension::D2,
                                                         shader::MaterialBufferKind::None, shader::MaterialSamplerKind::None,
                                                         shader::ScalarType::F32, 4, shader::MaterialResourceShapeFlags::None, 0, 0};
        const std::array<shader::MaterialResourceRole, 3> resourceRoles{
            {{0x2000, 0, 0, shader::MaterialResourceKind::Texture, shader::MaterialResourceFlags::Required, 0, textureType, textureShape},
             {0x2000, 1, 1, shader::MaterialResourceKind::Texture, shader::MaterialResourceFlags::None, 0, textureType, textureShape},
             {0x2000, 2, 2, shader::MaterialResourceKind::Texture, shader::MaterialResourceFlags::None, 0, textureType, textureShape}}};
        const std::array<shader::VertexInput, 1> inputs{{{0x4000, 0, 0, shader::NumericClass::FloatingPoint, 3, 32}}};
        const std::array<shader::FragmentOutput, 1> outputs{{{0x5000, 0, 0, shader::NumericClass::FloatingPoint, 0x0f}}};

        shader::MaterialContractBuildDescription contract;
        contract.domain.name = 0x7000;
        contract.domain.schemaVersion = 1;
        contract.domain.legalStages = shader::StageBit(shader::ShaderStage::Fragment);
        contract.domain.inputType = crypto::Sha256("SurfaceInput", 12);
        contract.domain.outputType = crypto::Sha256("SurfaceOutput", 13);
        contract.accessorAbiVersion = 1u + variant;
        contract.parameterByteSize = 16;
        contract.parameters = {members.data(), static_cast<u32>(members.size())};
        contract.resources = {resourceRoles.data(), static_cast<u32>(resourceRoles.size())};

        shader::BuildDescription shaderDescription;
        shaderDescription.kind = shader::ProgramKind::Graphics;
        shaderDescription.program = 0xabcdu + variant;
        shaderDescription.permutation = crypto::Sha256(&variant, sizeof(variant));
        shaderDescription.compilerFingerprint = crypto::Sha256("runtime test compiler", 21);
        shaderDescription.pipelineInterface.stages = shader::StageBit(shader::ShaderStage::Vertex) | shader::StageBit(shader::ShaderStage::Fragment);
        shaderDescription.pipelineInterface.primitiveClass = shader::PrimitiveClass::Triangle;
        shaderDescription.pipelineInterface.renderTargetCount = 1;
        shaderDescription.stages = {stages.data(), static_cast<u32>(stages.size())};
        shaderDescription.bindings = {bindings.data(), static_cast<u32>(bindings.size())};
        shaderDescription.constantBuffers = {buffers.data(), static_cast<u32>(buffers.size())};
        shaderDescription.constantMembers = {members.data(), static_cast<u32>(members.size())};
        shaderDescription.vertexInputs = {inputs.data(), static_cast<u32>(inputs.size())};
        shaderDescription.fragmentOutputs = {outputs.data(), static_cast<u32>(outputs.size())};
        shaderDescription.materialContract = &contract;
        filesystem::MemoryFileWriter shaderWriter(output.shaderBytes);
        if (shader::WriteShader(shaderWriter, shaderDescription) != shader::Result::Success)
            return false;

        filesystem::MemoryFileReader shaderReader(output.shaderBytes, 0);
        shader::ShaderFile shaderFile;
        if (shaderFile.Open(shaderReader) != shader::Result::Success || shaderFile.GetMaterialContract() == nullptr)
            return false;
        output.shaderPermutation = shaderFile.GetPermutation();
        output.shaderBindingLayout = shaderFile.BindingLayoutFingerprint();

        pipeline::ShaderReference pipelineShader{output.shaderReference.GetPath().Id(), shaderFile.GetPermutation(), shaderFile.BindingLayoutFingerprint(),
                                                  shaderFile.GetPipelineInterfaceFingerprint()};
        pipelineShader.materialDomain = shaderFile.GetMaterialContract()->domainFingerprint;
        pipelineShader.materialLayout = shaderFile.GetMaterialContract()->layoutFingerprint;
        pipeline::BuildDescription pipelineDescription;
        pipelineDescription.kind = pipeline::PipelineKind::Graphics;
        pipelineDescription.name = 0x8800;
        pipelineDescription.shaders = {&pipelineShader, 1};
        const pipeline::VertexStream vertexStream{0, 12, pipeline::InputRate::PerVertex, 1};
        const pipeline::VertexAttribute vertexAttribute{0x4000, 0, 0, 0, 0, shader::NumericClass::FloatingPoint, 3, 32,
                                                        pipeline::Format::R32G32B32Float, "POSITION"};
        pipelineDescription.vertexStreams = {&vertexStream, 1};
        pipelineDescription.vertexAttributes = {&vertexAttribute, 1};
        pipelineDescription.graphics.blend.attachmentCount = 1;
        pipelineDescription.graphics.attachmentPolicy = pipeline::AttachmentPolicy::Deferred;
        filesystem::MemoryFileWriter pipelineWriter(output.pipelineBytes);
        if (pipeline::WritePipeline(pipelineWriter, pipelineDescription) != pipeline::Result::Success)
            return false;

        filesystem::MemoryFileReader pipelineReader(output.pipelineBytes, 0);
        pipeline::PipelineFile pipelineFile;
        if (pipelineFile.Open(pipelineReader) != pipeline::Result::Success)
            return false;
        output.pipelineTemplate = pipelineFile.GetTemplateFingerprint();

        const material::TechniqueBuildRecord technique{0x9000, output.pipelineReference, &pipelineFile};
        const std::array<f32, 4> baseColor{{0.25f, 0.5f, 0.75f, 1.0f}};
        const material::ConstantValueBuildRecord constant{0x1100, baseColor.data(), static_cast<u32>(sizeof(baseColor))};
        const std::array<material::ResourceValueBuildRecord, 3> resourceValues{
            {{0x2000, 0, output.requiredTexture, resources::DependencyKind::Required},
             {0x2000, 1, output.optionalTexture, resources::DependencyKind::Optional},
             {0x2000, 2, output.softTexture, resources::DependencyKind::Soft}}};
        const material::ResourceTypeCompatibility compatibility{material::ResourceParameterKind::Texture, TextureType};
        material::BuildDescription materialDescription;
        materialDescription.name = 0x9900;
        materialDescription.shader = output.shaderReference;
        materialDescription.shaderReflection = &shaderFile;
        materialDescription.techniques = {&technique, 1};
        materialDescription.constants = {&constant, 1};
        materialDescription.resources = {resourceValues.data(), static_cast<u32>(resourceValues.size())};
        materialDescription.resourceTypeCompatibility = {&compatibility, 1};
        filesystem::MemoryFileWriter materialWriter(output.materialBytes);
        if (material::WriteMaterial(materialWriter, materialDescription) != material::Result::Success)
            return false;

        filesystem::MemoryFileReader materialReader(output.materialBytes, 0);
        material::MaterialFile materialFile;
        if (materialFile.Open(materialReader) != material::Result::Success)
            return false;
        output.materialContent = materialFile.GetContentFingerprint();
        return materialFile.GetDependencies().Size() == 5;
    }

    [[nodiscard]] bool BuildPackage(const ArtifactFixture& fixture, const material::MaterialFile& materialFile, ByteArray& output) noexcept
    {
        const packages::Dependency shaderDependency{fixture.shaderReference.GetPath().Id(), shader::ShaderResourceType,
                                                     resources::DependencyKind::Required};
        std::array<packages::Dependency, 8> materialDependencies{};
        u32 materialDependencyCount = 0;
        for (const material::ResourceDependency& dependency : materialFile.GetDependencies())
            materialDependencies[materialDependencyCount++] = {dependency.resource.GetPath().Id(), dependency.resource.ExpectedType(), dependency.kind};

        constexpr std::array<u8, 4> textureBytes{{0x56, 0x54, 0x45, 0x58}};
        const packages::BuildSegment shaderSegment{fixture.shaderBytes.TypedData(), fixture.shaderBytes.Size(), packages::Codec::Lz4, 4,
                                                    packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment pipelineSegment{fixture.pipelineBytes.TypedData(), fixture.pipelineBytes.Size(), packages::Codec::Lz4, 4,
                                                      packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment materialSegment{fixture.materialBytes.TypedData(), fixture.materialBytes.Size(), packages::Codec::Lz4, 4,
                                                      packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment textureSegment{textureBytes.data(), textureBytes.size(), packages::Codec::None, 4,
                                                     packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};

        packages::BuildResource shaderResource{"runtime/material_test.vshader", shader::ShaderResourceType, packages::ResourceFlags::Streamable,
                                                {&shaderSegment, 1}, {}};
        packages::BuildResource pipelineResource{"runtime/material_test.vppl", pipeline::PipelineResourceType, packages::ResourceFlags::Streamable,
                                                  {&pipelineSegment, 1}, {&shaderDependency, 1}};
        packages::BuildResource materialResource{"runtime/material_test.vmat", material::MaterialResourceType, packages::ResourceFlags::Streamable,
                                                  {&materialSegment, 1}, {materialDependencies.data(), materialDependencyCount}};
        packages::BuildResource textureResource{"runtime/required.vtex", TextureType, packages::ResourceFlags::Streamable,
                                                 {&textureSegment, 1}, {}};
        std::array<packages::BuildResource*, 4> resources{{&shaderResource, &pipelineResource, &materialResource, &textureResource}};
        std::sort(resources.begin(), resources.end(), [](const packages::BuildResource* const left, const packages::BuildResource* const right) noexcept
                  { return packages::HashResourcePath(left->path) < packages::HashResourcePath(right->path); });

        filesystem::MemoryFileWriter writer(output);
        packages::PackageWriter packageWriter;
        if (packageWriter.Begin(writer) != packages::Result::Success)
            return false;
        for (const packages::BuildResource* const resource : resources)
            if (packageWriter.Add(*resource) != packages::Result::Success)
                return false;
        return packageWriter.Finalize() == packages::Result::Success;
    }

    void CheckShader(const resources::ResourceHandle& handle, const ArtifactFixture& fixture, const char* const label)
    {
        const auto* const object = static_cast<const shader::ShaderResourceObject*>(handle.Get());
        Check(handle.GetType() == shader::ShaderResourceType && object != nullptr && object->IsOpen() &&
                  object->GetFile().GetPermutation() == fixture.shaderPermutation &&
                  object->GetFile().BindingLayoutFingerprint() == fixture.shaderBindingLayout,
              label);
    }

    void CheckPipeline(const resources::ResourceHandle& handle, const ArtifactFixture& fixture, const char* const label)
    {
        const auto* const object = static_cast<const pipeline::PipelineResourceObject*>(handle.Get());
        Check(handle.GetType() == pipeline::PipelineResourceType && object != nullptr && object->IsOpen() &&
                  object->GetFile().GetTemplateFingerprint() == fixture.pipelineTemplate && object->GetShaderDependencies().Size() == 1 &&
                  object->GetShaderDependencies()[0].GetPath() == fixture.shaderReference.GetPath(),
              label);
        if (object != nullptr && object->GetShaderDependencies().Size() == 1)
            CheckShader(object->GetShaderDependencies()[0], fixture, "pipeline retains its exact shader object");
    }

    void CheckClosure(const resources::ResourceHandle& handle, const ArtifactFixture& fixture, const char* const label)
    {
        const auto* const materialObject = static_cast<const material::MaterialResourceObject*>(handle.Get());
        Check(materialObject != nullptr && materialObject->IsOpen() && materialObject->GetFile().GetContentFingerprint() == fixture.materialContent, label);
        if (materialObject == nullptr)
            return;

        bool foundShader = false;
        bool foundPipeline = false;
        bool foundRequired = false;
        bool foundOptional = false;
        bool foundSoft = false;
        for (const material::LoadedMaterialDependency& dependency : materialObject->GetLoadedDependencies())
        {
            foundShader |= dependency.resource == fixture.shaderReference && dependency.handle.IsValid() && dependency.failure == resources::Failure::None;
            foundRequired |= dependency.resource == fixture.requiredTexture && dependency.handle.IsValid() && dependency.failure == resources::Failure::None;
            foundOptional |= dependency.resource == fixture.optionalTexture && !dependency.handle.IsValid() && dependency.failure == resources::Failure::NotFound &&
                             dependency.kind == resources::DependencyKind::Optional;
            foundSoft |= dependency.resource == fixture.softTexture;
            if (dependency.resource != fixture.pipelineReference)
                continue;
            const auto* const pipelineObject = static_cast<const pipeline::PipelineResourceObject*>(dependency.handle.Get());
            foundPipeline = pipelineObject != nullptr && pipelineObject->IsOpen() && pipelineObject->GetShaderDependencies().Size() == 1 &&
                            pipelineObject->GetShaderDependencies()[0].IsValid() &&
                            pipelineObject->GetShaderDependencies()[0].GetPath() == fixture.shaderReference.GetPath();
            if (foundPipeline)
            {
                const auto* const shaderObject = static_cast<const shader::ShaderResourceObject*>(pipelineObject->GetShaderDependencies()[0].Get());
                foundPipeline = shaderObject != nullptr && shaderObject->IsOpen();
            }
        }
        Check(materialObject->GetLoadedDependencies().Size() == 4 && foundShader && foundPipeline && foundRequired && foundOptional && !foundSoft,
              "material closure retains exact non-Soft dependency generations and Optional failure");
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "materialResourceTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_material_resource_conformance");
    Check(filesystem::Initialize({root, root, directory}), "filesystem initialization");
    filesystem::Manager& files = filesystem::GetManager();
    static_cast<void>(files.DeletePath(directory));
    Check(files.CreatePath(directory), "test directory creation");

    ArtifactFixture fixture;
    Check(BuildFixture(fixture), "build VSHADER/VPPL/VMAT resource fixture");
    ArtifactFixture alternateFixture;
    Check(BuildFixture(alternateFixture, 1), "build independently valid alternate material closure");
    filesystem::MemoryFileReader materialReader(fixture.materialBytes, 0);
    material::MaterialFile materialFile;
    Check(materialFile.Open(materialReader) == material::Result::Success, "reopen resource-test VMAT dependency authority");

    const filesystem::AbsolutePath shaderPath = directory.AddFilePath("material_test.vshader");
    const filesystem::AbsolutePath pipelinePath = directory.AddFilePath("material_test.vppl");
    const filesystem::AbsolutePath materialPath = directory.AddFilePath("material_test.vmat");
    const filesystem::AbsolutePath texturePath = directory.AddFilePath("required.vtex");
    const filesystem::AbsolutePath packagePath = directory.AddFilePath("materials.vpak");
    const filesystem::AbsolutePath alternateShaderPath = directory.AddFilePath("alternate.vshader");
    const filesystem::AbsolutePath alternatePipelinePath = directory.AddFilePath("alternate.vppl");
    const filesystem::AbsolutePath corruptMaterialPath = directory.AddFilePath("corrupt.vmat");
    constexpr std::array<u8, 4> textureBytes{{0x56, 0x54, 0x45, 0x58}};
    ByteArray corruptMaterialBytes(fixture.materialBytes);
    if (!corruptMaterialBytes.Empty())
        corruptMaterialBytes.Back() ^= 0x5au;
    Check(SaveBytes(shaderPath, fixture.shaderBytes) && SaveBytes(pipelinePath, fixture.pipelineBytes) &&
              SaveBytes(alternateShaderPath, alternateFixture.shaderBytes) && SaveBytes(alternatePipelinePath, alternateFixture.pipelineBytes) &&
              SaveBytes(materialPath, fixture.materialBytes) && SaveBytes(corruptMaterialPath, corruptMaterialBytes) &&
              SaveBytes(texturePath, textureBytes.data(), textureBytes.size()),
          "write loose artifact resources");

    resources::ResourceRegistry registry;
    resources::ResourcePipeline resourcePipeline;
    streaming::ResourceStreamer streamer;
    Check(registry.Initialize(), "resource registry initialization");
    Check(resourcePipeline.Initialize(registry), "resource pipeline initialization");
    Check(streamer.Initialize(resourcePipeline), "resource streamer initialization");
    shader::ShaderResourceDecoderConfig shaderConfig;
    pipeline::PipelineResourceDecoderConfig pipelineConfig;
    material::MaterialResourceDecoderConfig materialConfig;
    Check(streamer.RegisterDecoder({shader::ShaderResourceType, "Vanguard shader artifact", &shader::DecodeShaderResource,
                                    &shader::DestroyShaderResource, &shaderConfig}) &&
              streamer.RegisterDecoder({pipeline::PipelineResourceType, "Vanguard pipeline artifact", &pipeline::DecodePipelineResource,
                                        &pipeline::DestroyPipelineResource, &pipelineConfig}) &&
              streamer.RegisterDecoder({material::MaterialResourceType, "Vanguard material artifact", &material::DecodeMaterialResource,
                                        &material::DestroyMaterialResource, &materialConfig}) &&
              streamer.RegisterDecoder({TextureType, "test texture", &DecodeTexture, &DestroyTexture, nullptr}),
          "register artifact decoders");

    const streaming::DependencyDescriptor pipelineDependency{fixture.shaderReference, resources::DependencyKind::Required};
    containers::DynamicArray<streaming::DependencyDescriptor> materialDependencies{memory::pools::Resources::GetInstance()};
    for (const material::ResourceDependency& dependency : materialFile.GetDependencies())
        materialDependencies.PushBack({dependency.resource, dependency.kind});
    Check(streamer.RegisterLoose({fixture.shaderReference, shaderPath, {}, vanguard::serialization::Crc64(fixture.shaderBytes.Data(), fixture.shaderBytes.Size()), 0}) &&
              streamer.RegisterLoose({fixture.pipelineReference, pipelinePath, {&pipelineDependency, 1},
                                      vanguard::serialization::Crc64(fixture.pipelineBytes.Data(), fixture.pipelineBytes.Size()), 0}) &&
              streamer.RegisterLoose({fixture.materialReference, materialPath, {materialDependencies.TypedData(), materialDependencies.Size()},
                                      vanguard::serialization::Crc64(fixture.materialBytes.Data(), fixture.materialBytes.Size()), 0}) &&
              streamer.RegisterLoose({fixture.requiredTexture, texturePath, {}, vanguard::serialization::Crc64(textureBytes.data(), textureBytes.size()), 0}),
          "register loose artifact closure");

    resources::PipelineRequest looseShaderRequest = streamer.Request(fixture.shaderReference, resources::LoadPriority::High);
    Check(looseShaderRequest.TryWait(10000) && looseShaderRequest.HasLoaded(), "load loose VSHADER as a root resource");
    resources::ResourceHandle looseShaderHandle = looseShaderRequest.Acquire();
    CheckShader(looseShaderHandle, fixture, "loose VSHADER preserves its canonical parsed identity");
    looseShaderRequest.Reset();

    resources::PipelineRequest loosePipelineRequest = streamer.Request(fixture.pipelineReference, resources::LoadPriority::High);
    Check(loosePipelineRequest.TryWait(10000) && loosePipelineRequest.HasLoaded(), "load loose VPPL as a root resource");
    resources::ResourceHandle loosePipelineHandle = loosePipelineRequest.Acquire();
    CheckPipeline(loosePipelineHandle, fixture, "loose VPPL preserves its canonical parsed identity and dependency");
    loosePipelineRequest.Reset();

    resources::PipelineRequest cancelledLooseInterest = streamer.Request(fixture.materialReference, resources::LoadPriority::Background);
    resources::PipelineRequest looseRequest = streamer.Request(fixture.materialReference, resources::LoadPriority::Critical);
    Check(cancelledLooseInterest.IsSameOperation(looseRequest) && cancelledLooseInterest.Priority() == resources::LoadPriority::Critical &&
              looseRequest.Priority() == resources::LoadPriority::Critical,
          "coalesced VMAT callers share one closure operation and promote its priority");
    Check(cancelledLooseInterest.Cancel() && cancelledLooseInterest.GetStatus() == resources::State::Cancelled,
          "cancelling one coalesced VMAT caller releases only that caller's interest");
    cancelledLooseInterest.Reset();
    Check(looseRequest.TryWait(10000) && looseRequest.HasLoaded(), "load immutable loose material closure");
    resources::ResourceHandle looseHandle = looseRequest.Acquire();
    const u32 looseMaterialGeneration = looseHandle.GetGeneration();
    resources::WeakResourceHandle looseMaterialWeak = looseHandle.ToWeak();
    CheckClosure(looseHandle, fixture, "loose VMAT publishes an immutable CPU resource object");
    looseRequest.Reset();
    CheckClosure(looseHandle, fixture, "root handle retains dependency generations after request release");

    const resources::ResourceReference mismatchReference(resources::ResourcePath::FromString("runtime/mismatch.vmat"), material::MaterialResourceType);
    containers::DynamicArray<streaming::DependencyDescriptor> mismatchedDependencies(materialDependencies);
    for (u32 index = 0; index < mismatchedDependencies.Size(); ++index)
    {
        if (mismatchedDependencies[index].kind != resources::DependencyKind::Soft)
        {
            static_cast<void>(mismatchedDependencies.RemoveAt(index));
            break;
        }
    }
    Check(streamer.RegisterLoose({mismatchReference, materialPath, {mismatchedDependencies.TypedData(), mismatchedDependencies.Size()},
                                  vanguard::serialization::Crc64(fixture.materialBytes.Data(), fixture.materialBytes.Size()), 0}),
          "register deliberately incomplete material dependency metadata");
    resources::PipelineRequest mismatchRequest = streamer.Request(mismatchReference);
    Check(mismatchRequest.TryWait(10000) && mismatchRequest.HasFailed() && mismatchRequest.GetError() == resources::Failure::IntegrityFailure,
          "payload-derived dependencies reject incomplete loose metadata");
    mismatchRequest.Reset();

    const resources::ResourceReference corruptPayloadReference(resources::ResourcePath::FromString("runtime/corrupt.vmat"),
                                                               material::MaterialResourceType);
    Check(streamer.RegisterLoose({corruptPayloadReference, corruptMaterialPath,
                                  {materialDependencies.TypedData(), materialDependencies.Size()},
                                  vanguard::serialization::Crc64(corruptMaterialBytes.Data(), corruptMaterialBytes.Size()), 0}),
          "register VMAT whose source CRC is valid but whose document payload is corrupt");
    resources::PipelineRequest corruptPayloadRequest = streamer.Request(corruptPayloadReference);
    Check(corruptPayloadRequest.TryWait(10000) && corruptPayloadRequest.HasFailed() &&
              corruptPayloadRequest.GetError() == resources::Failure::IntegrityFailure,
          "VMAT decoder rejects corrupt payload bytes after source integrity succeeds");
    corruptPayloadRequest.Reset();
    looseHandle.Reset();
    loosePipelineHandle.Reset();
    looseShaderHandle.Reset();
    WaitForDrain(resourcePipeline, streamer);

    Check(streamer.UnregisterLoose(corruptPayloadReference.GetPath()) && streamer.UnregisterLoose(mismatchReference.GetPath()) &&
              streamer.UnregisterLoose(fixture.materialReference.GetPath()) &&
              streamer.UnregisterLoose(fixture.pipelineReference.GetPath()) && streamer.UnregisterLoose(fixture.shaderReference.GetPath()) &&
              streamer.UnregisterLoose(fixture.requiredTexture.GetPath()),
          "unregister loose artifact closure");

    const resources::ResourceReference badPipelineReference(resources::ResourcePath::FromString("runtime/mismatched_shader.vppl"),
                                                             pipeline::PipelineResourceType);
    const resources::ResourceReference badMaterialReference(resources::ResourcePath::FromString("runtime/mismatched_closure.vmat"),
                                                             material::MaterialResourceType);
    Check(streamer.RegisterLoose({fixture.shaderReference, alternateShaderPath, {},
                                  vanguard::serialization::Crc64(alternateFixture.shaderBytes.Data(), alternateFixture.shaderBytes.Size()), 0}) &&
              streamer.RegisterLoose({badPipelineReference, pipelinePath, {&pipelineDependency, 1},
                                      vanguard::serialization::Crc64(fixture.pipelineBytes.Data(), fixture.pipelineBytes.Size()), 0}) &&
              streamer.RegisterLoose({fixture.pipelineReference, alternatePipelinePath, {&pipelineDependency, 1},
                                      vanguard::serialization::Crc64(alternateFixture.pipelineBytes.Data(), alternateFixture.pipelineBytes.Size()), 0}) &&
              streamer.RegisterLoose({badMaterialReference, materialPath, {materialDependencies.TypedData(), materialDependencies.Size()},
                                      vanguard::serialization::Crc64(fixture.materialBytes.Data(), fixture.materialBytes.Size()), 0}) &&
              streamer.RegisterLoose({fixture.requiredTexture, texturePath, {}, vanguard::serialization::Crc64(textureBytes.data(), textureBytes.size()), 0}),
          "register deliberately cross-mismatched but individually valid artifact closures");
    resources::PipelineRequest badPipelineRequest = streamer.Request(badPipelineReference);
    Check(badPipelineRequest.TryWait(10000) && badPipelineRequest.HasFailed() &&
              badPipelineRequest.GetError() == resources::Failure::IntegrityFailure,
          "VPPL decoder rejects a retained VSHADER generation with mismatched fingerprints");
    badPipelineRequest.Reset();
    resources::PipelineRequest badMaterialRequest = streamer.Request(badMaterialReference);
    Check(badMaterialRequest.TryWait(10000) && badMaterialRequest.HasFailed() &&
              badMaterialRequest.GetError() == resources::Failure::IntegrityFailure,
          "VMAT decoder rejects a self-consistent shader/pipeline closure from a different material layout");
    badMaterialRequest.Reset();
    WaitForDrain(resourcePipeline, streamer);
    Check(streamer.UnregisterLoose(badMaterialReference.GetPath()) && streamer.UnregisterLoose(fixture.pipelineReference.GetPath()) &&
              streamer.UnregisterLoose(badPipelineReference.GetPath()) && streamer.UnregisterLoose(fixture.shaderReference.GetPath()) &&
              streamer.UnregisterLoose(fixture.requiredTexture.GetPath()),
          "unregister cross-mismatch proof closure");

    ByteArray packageBytes(memory::pools::Assets::GetInstance());
    Check(BuildPackage(fixture, materialFile, packageBytes) && SaveBytes(packagePath, packageBytes), "build indexed artifact VPAK");
    auto packageFile = filesystem::RawFileReader::Create(packagePath);
    packages::PackageReader packageReader;
    Check(packageFile && packageReader.Open(*packageFile) == packages::Result::Success, "open indexed artifact VPAK");
    packageFile.Reset();
    Check(streamer.MountPackage(packageReader, packagePath, 0), "mount indexed artifact VPAK");

    resources::PipelineRequest packageShaderRequest = streamer.Request(fixture.shaderReference, resources::LoadPriority::High);
    Check(packageShaderRequest.TryWait(10000) && packageShaderRequest.HasLoaded(), "load indexed-VPAK VSHADER as a root resource");
    resources::ResourceHandle packageShaderHandle = packageShaderRequest.Acquire();
    CheckShader(packageShaderHandle, fixture, "indexed VPAK preserves byte-equivalent VSHADER identity");
    packageShaderRequest.Reset();

    const auto* const layoutShader = static_cast<const shader::ShaderResourceObject*>(packageShaderHandle.Get());
    Check(!rhi::IsInitialized(), "material-layout admission begins without an initialized RHI");
    rendering::MaterialProgramLayoutRegistry layoutRegistry;
    rendering::MaterialProgramLayoutFailure layoutFailure;
    Check(layoutRegistry.Initialize({rhi::BackendKind::D3D12, 1}, &layoutFailure), "initialize bounded D3D12 material program-layout registry");
    rendering::MaterialProgramLayoutId layout;
    Check(layoutShader != nullptr && layoutRegistry.Register(*layoutShader, layout, &layoutFailure) && layout.IsValid() && layout.index == 0,
          "validated shader contract receives the first compact material-layout id");
    rendering::MaterialProgramLayoutId repeatedLayout;
    Check(layoutShader != nullptr && layoutRegistry.Register(*layoutShader, repeatedLayout, &layoutFailure) && repeatedLayout == layout,
          "repeated full material-layout identity returns the stable compact id");
    rendering::MaterialProgramLayoutView layoutView;
    Check(layoutRegistry.Get(layout, layoutView) && layoutView.id == layout,
          "material-layout registry lookup returns a canonical record");
    u32 collisionCount = 0;
    if (layoutShader != nullptr && layoutShader->GetFile().GetMaterialContract() != nullptr)
    {
        const shader::MaterialContract& contract = *layoutShader->GetFile().GetMaterialContract();
        Check(layoutView.layoutFingerprint == contract.layoutFingerprint && layoutView.domainFingerprint == contract.domainFingerprint &&
                  layoutView.accessorAbiVersion == contract.accessorAbiVersion && layoutView.parameterByteSize == contract.parameterByteSize &&
                  layoutView.parameters.Size() == layoutShader->GetFile().GetMaterialParameters().Size() &&
                  layoutView.resources.Size() == layoutShader->GetFile().GetMaterialResources().Size(),
              "compact id resolves to the complete reflected material-layout authority");

        rendering::detail::MaterialProgramLayoutCanonical collision{contract.layoutFingerprint, contract.domainFingerprint, contract.domain,
                                                                     contract.accessorAbiVersion, contract.parameterByteSize,
                                                                     layoutShader->GetFile().GetMaterialParameters(),
                                                                     layoutShader->GetFile().GetMaterialResources()};
        rendering::MaterialProgramLayoutId rejectedLayout;
        const auto rejectCollision = [&](const rendering::detail::MaterialProgramLayoutCanonical& candidate, const char* const message)
        {
            Check(!rendering::detail::MaterialProgramLayoutRegistryAccess::RegisterCanonical(layoutRegistry, candidate, rejectedLayout, &layoutFailure) &&
                      layoutFailure.code == rendering::MaterialProgramLayoutFailureCode::FingerprintCollision && !rejectedLayout.IsValid(),
                  message);
            ++collisionCount;
        };

        ++collision.accessorAbiVersion;
        rejectCollision(collision, "equal full fingerprint with a different accessor ABI is rejected as a collision");
        collision.accessorAbiVersion = contract.accessorAbiVersion;
        ++collision.parameterByteSize;
        rejectCollision(collision, "equal full fingerprint with a different parameter byte size is rejected as a collision");
        collision.parameterByteSize = contract.parameterByteSize;
        ++collision.domain.schemaVersion;
        rejectCollision(collision, "equal full fingerprint with a different material-domain payload is rejected as a collision");
        collision.domain = contract.domain;
        ++collision.domainFingerprint.bytes[0];
        rejectCollision(collision, "equal full fingerprint with a different domain fingerprint is rejected as a collision");
        collision.domainFingerprint = contract.domainFingerprint;

        std::array<shader::ConstantMember, 1> changedParameters{{layoutShader->GetFile().GetMaterialParameters()[0]}};
        ++changedParameters[0].byteOffset;
        collision.parameters = {changedParameters.data(), static_cast<u32>(changedParameters.size())};
        rejectCollision(collision, "equal full fingerprint with a different parameter record is rejected as a collision");
        collision.parameters = layoutShader->GetFile().GetMaterialParameters();
        std::array<shader::MaterialResourceRole, 3> changedResources{};
        for (u32 index = 0; index < static_cast<u32>(changedResources.size()); ++index)
            changedResources[index] = layoutShader->GetFile().GetMaterialResources()[index];
        ++changedResources[0].slot;
        collision.resources = {changedResources.data(), static_cast<u32>(changedResources.size())};
        rejectCollision(collision, "equal full fingerprint with a different resource-role record is rejected as a collision");
        changedResources[0] = layoutShader->GetFile().GetMaterialResources()[0];
        changedResources[0].shape.textureDimension = shader::MaterialTextureDimension::Cube;
        rejectCollision(collision, "equal full fingerprint with a different reconstructable resource shape is rejected as a collision");
        collision.resources = layoutShader->GetFile().GetMaterialResources();

        rendering::detail::MaterialProgramLayoutCanonical overflow = collision;
        ++overflow.layoutFingerprint.bytes[0];
        if (overflow.layoutFingerprint.IsEmpty())
            overflow.layoutFingerprint.bytes[0] = 1;
        Check(!rendering::detail::MaterialProgramLayoutRegistryAccess::RegisterCanonical(layoutRegistry, overflow, rejectedLayout, &layoutFailure) &&
                  layoutFailure.code == rendering::MaterialProgramLayoutFailureCode::CapacityExceeded && !rejectedLayout.IsValid(),
              "bounded material-layout registry rejects a distinct layout after capacity is exhausted");
    }
    const rendering::MaterialProgramLayoutRegistryStats layoutStats = layoutRegistry.GetStats();
    Check(layoutStats.registeredLayouts == 1 && layoutStats.repeatedRegistrations == 1 && layoutStats.fingerprintCollisions == collisionCount &&
              layoutStats.rejectedRegistrations == collisionCount + 1u,
          "material-layout registry reports stable reuse, collision, and capacity outcomes");
    Check(layoutRegistry.Shutdown(&layoutFailure), "shutdown material program-layout registry");

    rendering::MaterialProgramLayoutRegistry wrongBackendRegistry;
    Check(wrongBackendRegistry.Initialize({rhi::BackendKind::Vulkan, 1}, &layoutFailure), "initialize Vulkan material program-layout registry");
    Check(layoutShader != nullptr && !wrongBackendRegistry.Register(*layoutShader, layout, &layoutFailure) &&
              layoutFailure.code == rendering::MaterialProgramLayoutFailureCode::UnsupportedBackendFormat,
          "renderer admission rejects DXIL material layout on a Vulkan backend without creating native state");
    Check(wrongBackendRegistry.Shutdown(&layoutFailure), "shutdown wrong-backend material program-layout registry");
    Check(!rhi::IsInitialized(), "material-layout registration and rejection perform zero RHI initialization");

    resources::PipelineRequest packagePipelineRequest = streamer.Request(fixture.pipelineReference, resources::LoadPriority::High);
    Check(packagePipelineRequest.TryWait(10000) && packagePipelineRequest.HasLoaded(), "load indexed-VPAK VPPL as a root resource");
    resources::ResourceHandle packagePipelineHandle = packagePipelineRequest.Acquire();
    CheckPipeline(packagePipelineHandle, fixture, "indexed VPAK preserves byte-equivalent VPPL identity and dependency");
    packagePipelineRequest.Reset();

    resources::PipelineRequest packageRequest = streamer.Request(fixture.materialReference, resources::LoadPriority::High);
    Check(packageRequest.TryWait(10000) && packageRequest.HasLoaded(), "load immutable indexed-VPAK material closure");
    resources::ResourceHandle packageHandle = packageRequest.Acquire();
    CheckClosure(packageHandle, fixture, "indexed VPAK preserves the byte-equivalent CPU material closure");
    Check(packageHandle.GetGeneration() != looseMaterialGeneration && looseMaterialWeak.IsStale() && !looseMaterialWeak.Lock(),
          "loose-to-VPAK replacement publishes a new immutable generation and cannot revive the old closure");
    looseMaterialWeak.Reset();
    packageHandle.Reset();
    packageRequest.Reset();
    packagePipelineHandle.Reset();
    packageShaderHandle.Reset();
    WaitForDrain(resourcePipeline, streamer);

    Check(streamer.UnmountPackage(packageReader), "unmount artifact VPAK");
    Check(streamer.UnregisterDecoder(material::MaterialResourceType) && streamer.UnregisterDecoder(pipeline::PipelineResourceType) &&
              streamer.UnregisterDecoder(shader::ShaderResourceType) && streamer.UnregisterDecoder(TextureType),
          "unregister artifact decoders in reverse dependency order");
    Check(streamer.Shutdown(), "resource streamer shutdown");
    Check(resourcePipeline.Shutdown(), "resource pipeline shutdown");
    Check(registry.Shutdown(), "resource registry shutdown");
    Check(jobs::Shutdown(), "jobs shutdown");

    packageReader.Close();
    static_cast<void>(files.DeleteFile(shaderPath));
    static_cast<void>(files.DeleteFile(pipelinePath));
    static_cast<void>(files.DeleteFile(materialPath));
    static_cast<void>(files.DeleteFile(texturePath));
    static_cast<void>(files.DeleteFile(packagePath));
    static_cast<void>(files.DeleteFile(alternateShaderPath));
    static_cast<void>(files.DeleteFile(alternatePipelinePath));
    static_cast<void>(files.DeleteFile(corruptMaterialPath));
    static_cast<void>(files.DeletePath(directory));
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
        std::puts("[materialResourceTests] Vanguard material artifact resource checks passed");
    return g_failures == 0 ? 0 : 1;
}
