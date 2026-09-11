#include <vanguard/rendering/material_residency_runtime.hpp>
#include <vanguard/rendering/material_scene_binding.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/streaming/streaming.hpp>

#include <d3dcompiler.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

namespace vanguard::rendering::tests
{
    namespace
    {
        namespace material = vanguard::materials;
        namespace pipeline = vanguard::pipelines;
        namespace shader = vanguard::shaders;
        using ByteArray = containers::DynamicArray<u8>;

        struct CompiledShader
        {
            ID3DBlob* bytecode = nullptr;

            ~CompiledShader()
            {
                if (bytecode != nullptr)
                    bytecode->Release();
            }
        };

        struct RuntimeFixture
        {
            RuntimeFixture() noexcept : shaderBytes(memory::pools::Rendering::GetInstance()), pipelineBytes(memory::pools::Rendering::GetInstance()), materialBytes(memory::pools::Rendering::GetInstance()) {}

            resources::ResourceReference shaderReference{resources::ResourcePath::FromString("runtime/material_residency.vshader"), shader::ShaderResourceType};
            resources::ResourceReference pipelineReference{resources::ResourcePath::FromString("runtime/material_residency.vppl"), pipeline::PipelineResourceType};
            resources::ResourceReference materialReference{resources::ResourcePath::FromString("runtime/material_residency.vmat"), material::MaterialResourceType};
            resources::ResourceReference secondMaterialReference{resources::ResourcePath::FromString("runtime/material_residency_second.vmat"), material::MaterialResourceType};
            ByteArray shaderBytes;
            ByteArray pipelineBytes;
            ByteArray materialBytes;
        };

        [[nodiscard]] bool Compile(const char* const source, const char* const target, CompiledShader& output) noexcept
        {
            ID3DBlob* diagnostics = nullptr;
            const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &output.bytecode, &diagnostics);
            if (diagnostics != nullptr)
                diagnostics->Release();
            return SUCCEEDED(result) && output.bytecode != nullptr;
        }

        [[nodiscard]] bool BuildFixture(RuntimeFixture& output) noexcept
        {
            CompiledShader vertex;
            CompiledShader pixel;
            if (!Compile("float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1.0); }", "vs_5_0", vertex) ||
                !Compile("float4 main() : SV_Target0 { return float4(1.0, 0.0, 1.0, 1.0); }", "ps_5_0", pixel))
                return false;
            const shader::StageBuildRecord stages[]{{shader::ShaderStage::Vertex, shader::NativeFormat::Dxil, 0x1001, vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(), "main"},
                                                    {shader::ShaderStage::Fragment, shader::NativeFormat::Dxil, 0x1002, pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(), "main"}};
            const shader::VertexInput input{0x2001, 0, 0, shader::NumericClass::FloatingPoint, 3, 32};
            const shader::FragmentOutput fragmentOutput{0x3001, 0, 0, shader::NumericClass::FloatingPoint, 0x0f};
            shader::MaterialContractBuildDescription contract;
            contract.domain.name = 0x4d41545245534944ull;
            contract.domain.schemaVersion = 1;
            contract.domain.legalStages = shader::StageBit(shader::ShaderStage::Fragment);
            contract.domain.inputType = crypto::Sha256("RuntimeMaterialInput", 20);
            contract.domain.outputType = crypto::Sha256("RuntimeMaterialOutput", 21);
            contract.accessorAbiVersion = 1;
            shader::BuildDescription shaderDescription;
            shaderDescription.kind = shader::ProgramKind::Graphics;
            shaderDescription.program = 0x3c010001;
            shaderDescription.permutation = crypto::Sha256("material residency permutation", 30);
            shaderDescription.compilerFingerprint = crypto::Sha256("d3dcompiler runtime proof", 25);
            shaderDescription.pipelineInterface.stages = shader::StageBit(shader::ShaderStage::Vertex) | shader::StageBit(shader::ShaderStage::Fragment);
            shaderDescription.pipelineInterface.primitiveClass = shader::PrimitiveClass::Triangle;
            shaderDescription.pipelineInterface.renderTargetCount = 1;
            shaderDescription.stages = stages;
            shaderDescription.vertexInputs = {&input, 1};
            shaderDescription.fragmentOutputs = {&fragmentOutput, 1};
            shaderDescription.materialContract = &contract;
            filesystem::MemoryFileWriter shaderWriter(output.shaderBytes);
            if (shader::WriteShader(shaderWriter, shaderDescription) != shader::Result::Success)
                return false;
            filesystem::MemoryFileReader shaderReader(output.shaderBytes, 0);
            shader::ShaderFile shaderFile;
            if (shaderFile.Open(shaderReader) != shader::Result::Success || shaderFile.GetMaterialContract() == nullptr)
                return false;

            pipeline::ShaderReference pipelineShader{output.shaderReference.GetPath().Id(),
                                                     shaderFile.GetPermutation(),
                                                     shaderFile.BindingLayoutFingerprint(),
                                                     shaderFile.GetPipelineInterfaceFingerprint(),
                                                     shaderFile.GetMaterialContract()->domainFingerprint,
                                                     shaderFile.GetMaterialContract()->layoutFingerprint};
            const pipeline::VertexStream vertexStream{0, 12, pipeline::InputRate::PerVertex, 1};
            const pipeline::VertexAttribute vertexAttribute{0x2001, 0, 0, 0, 0, shader::NumericClass::FloatingPoint, 3, 32, pipeline::Format::R32G32B32Float, "POSITION"};
            pipeline::BuildDescription pipelineDescription;
            pipelineDescription.kind = pipeline::PipelineKind::Graphics;
            pipelineDescription.name = 0x3c010002;
            pipelineDescription.shaders = {&pipelineShader, 1};
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

            const material::TechniqueBuildRecord technique{0x3c010003, output.pipelineReference, &pipelineFile};
            material::BuildDescription materialDescription;
            materialDescription.name = 0x3c010004;
            materialDescription.shader = output.shaderReference;
            materialDescription.shaderReflection = &shaderFile;
            materialDescription.techniques = {&technique, 1};
            filesystem::MemoryFileWriter materialWriter(output.materialBytes);
            return material::WriteMaterial(materialWriter, materialDescription) == material::Result::Success;
        }

        [[nodiscard]] bool Save(const filesystem::AbsolutePath& path, const ByteArray& bytes) noexcept
        {
            auto writer = filesystem::RawFileWriter::Create(path, false);
            if (!writer)
                return false;
            writer->Serialize(const_cast<void*>(bytes.Data()), bytes.Size());
            writer->Flush();
            return writer->GetSize() == bytes.Size();
        }

        [[nodiscard]] bool SubmitRetirementFences(rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
        {
            const rhi::CommandListType types[]{rhi::CommandListType::Default, rhi::CommandListType::Compute, rhi::CommandListType::CopyAsync};
            for (u32 index = 0; index < 3; ++index)
            {
                const rhi::CommandListRef commandList = rhi::CreateCommandList(types[index], 0x3c010100 + index, &failure);
                if (!commandList || !rhi::BindCommandList(commandList, &failure))
                    return false;
                rhi::UnbindCommandList();
                const rhi::CommandListRef submissions[]{commandList};
                rhi::GpuFence completion;
                if (!rhi::CloseAndSubmitCommandLists("material residency retirement", submissions, rhi::CommandListSyncType::None, completion, &failure))
                    return false;
                fences.Include(completion);
            }
            return true;
        }

        [[nodiscard]] bool AdvanceSceneBinding(MaterialResidencyRuntime& residency, MaterialSceneBindingBridge& bindings, GpuSceneRuntime& runtime, RenderCommandSystem& commands, RenderSceneManager& scenes,
                                               u64& tick) noexcept
        {
            MaterialResidencyRuntimeFailure residencyFailure;
            MaterialSceneBindingFailure bindingFailure;
            RenderCommandFrameTickResult frame;
            RenderCommandFailure commandFailure;
            GpuSceneRuntimeFailure runtimeFailure;
            if (!residency.Update(&residencyFailure) || !residency.Update(&residencyFailure) || !bindings.Update(&bindingFailure) ||
                !commands.FrameTick(scenes.GetFramePipelineScenes(), tick++, frame, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) ||
                commands.ConsumeExecutionFailure(commandFailure) || !runtime.ResolveContributions(&runtimeFailure) || runtime.ConsumePublicationFailure(runtimeFailure) || !residency.Update(&residencyFailure) ||
                !bindings.Update(&bindingFailure))
                return false;
            return true;
        }

        [[nodiscard]] bool RunSceneBindingProof(const resources::ResourceHandle& firstMaterial, const resources::ResourceHandle& secondMaterial, MaterialMaterializer& materializer, GpuSceneRuntime& runtime,
                                                RenderCommandSystem& commands, RenderSceneManager& scenes, const resources::ResourceHandle& mesh) noexcept
        {
            PipelineCache pipelineCache;
            MaterialResidencyRuntime residency;
            MaterialResidencyRuntimeFailure residencyFailure;
            MaterialResidencyRuntimeConfig residencyConfig;
            residencyConfig.maximumResidencies = 2;
            residencyConfig.maximumDemands = 3;
            residencyConfig.maximumTechniqueRequests = 1;
            residencyConfig.maximumNativePrograms = 1;
            if (!pipelineCache.Initialize() || !residency.Initialize(materializer, pipelineCache, {}, residencyConfig, &residencyFailure))
                return false;

            RenderSceneDesc sceneDesc;
            sceneDesc.name = "material scene-binding proof";
            sceneDesc.maximumProxies = 2;
            sceneDesc.maximumPendingProxyMutations = 4;
            sceneDesc.maximumViews = 1;
            RenderSceneHandle scene;
            RenderSceneFailure sceneFailure;
            if (!scenes.CreateScene(sceneDesc, scene, &sceneFailure))
                return false;
            DecalProxyDesc decalDesc;
            decalDesc.proxy.scene = scene;
            decalDesc.proxy.debugName = "material scene-binding decal";
            decalDesc.material = resources::ResourceReference(firstMaterial.GetPath(), firstMaterial.GetType());
            decalDesc.materialHandle = firstMaterial;
            RenderProxyHandle decal;
            if (!scenes.CreateDecalProxy(decalDesc, decal, &sceneFailure))
                return false;
            MaterialSceneBindingBridge bindings;
            struct StageReporter
            {
                u32 stage = 2;
                ~StageReporter()
                {
                    if (stage != 100)
                        std::fprintf(stderr, "[material3C2] scene-binding proof failed at stage %u\n", stage);
                }
            } reporter;
            MaterialSceneBindingFailure bindingFailure;
            MaterialSceneBindingConfig bindingConfig;
            bindingConfig.maximumBindings = 2;
            bindingConfig.maximumChecksPerUpdate = 2;
            if (!bindings.Initialize(residency, runtime.GetScenePublisher(), bindingConfig, &bindingFailure) || !bindings.SetDecalMaterial(decal, firstMaterial, &bindingFailure))
                return false;
            reporter.stage = 3;

            u64 tick = 6;
            MaterialSceneBindingInfo info;
            for (u32 attempt = 0; attempt < 4; ++attempt)
                if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick))
                    return false;
            if (!bindings.GetInfo(decal, info, &bindingFailure) || !info.activeResidency.IsValid() || info.candidateResidency.IsValid() || !info.activeMaterial.IsValid())
                return false;
            reporter.stage = 4;
            const MaterialResidencyHandle firstResidency = info.activeResidency;
            const GpuMaterialHandle firstGpuMaterial = info.activeMaterial;

            if (!bindings.Remove(decal, &bindingFailure) || !bindings.SetDecalMaterial(decal, firstMaterial, &bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure) || info.clearing ||
                info.awaitingScenePublication || info.activeResidency != firstResidency || info.candidateResidency.IsValid() || info.activeMaterial != firstGpuMaterial)
                return false;
            if (!bindings.Remove(decal, &bindingFailure) || !bindings.SetDecalMaterial(decal, secondMaterial, &bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure) || info.clearing ||
                info.awaitingScenePublication || info.activeResidency != firstResidency || !info.candidateResidency.IsValid() || info.activeMaterial != firstGpuMaterial ||
                !bindings.CancelCandidate(decal, &bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure) || info.clearing || info.awaitingScenePublication ||
                info.activeResidency != firstResidency || info.candidateResidency.IsValid() || info.activeMaterial != firstGpuMaterial)
                return false;

            const bool setReplacement = bindings.SetDecalMaterial(decal, secondMaterial, &bindingFailure);
            const MaterialSceneBindingFailure setFailure = bindingFailure;
            const bool queriedReplacement = bindings.GetInfo(decal, info, &bindingFailure);
            const bool activePreserved = info.activeResidency == firstResidency;
            const bool candidateCreated = info.candidateResidency.IsValid();
            const bool canceledReplacement = bindings.CancelCandidate(decal, &bindingFailure);
            const bool queriedCancellation = bindings.GetInfo(decal, info, &bindingFailure);
            if (!setReplacement || !queriedReplacement || !activePreserved || !candidateCreated || !canceledReplacement || !queriedCancellation || info.activeResidency != firstResidency ||
                info.candidateResidency.IsValid() || info.activeMaterial != firstGpuMaterial)
            {
                std::fprintf(stderr, "[material3C2] cancellation: set=%u query=%u active=%u candidate=%u cancel=%u query2=%u code=%u residency=%u message=%s\n", setReplacement, queriedReplacement,
                             activePreserved, candidateCreated, canceledReplacement, queriedCancellation, static_cast<u32>(setFailure.code), static_cast<u32>(setFailure.residencyFailure.code),
                             setFailure.message != nullptr ? setFailure.message : "");
                return false;
            }
            reporter.stage = 5;

            if (bindings.SetDecalMaterial(decal, mesh, &bindingFailure) || bindingFailure.code != MaterialSceneBindingFailureCode::ResidencyFailure || !bindings.GetInfo(decal, info, &bindingFailure) ||
                info.activeResidency != firstResidency || info.candidateResidency.IsValid())
                return false;

            RenderSceneGpuFailure gpuSceneFailure;
            RenderSceneGpuBindingReceipt componentUpdateReceipt;
            if (!runtime.GetScenePublisher().BindDecalMaterial(decal, firstGpuMaterial, componentUpdateReceipt, &gpuSceneFailure) || !AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick) ||
                runtime.GetScenePublisher().PollBinding(componentUpdateReceipt) != RenderSceneGpuBindingStatus::Accepted)
                return false;
            DecalProxyUpdate logicalReplacement;
            logicalReplacement.fields = DecalProxyUpdateFields::Material;
            logicalReplacement.material = resources::ResourceReference(secondMaterial.GetPath(), secondMaterial.GetType());
            logicalReplacement.materialHandle = secondMaterial;
            if (!scenes.UpdateDecalProxy(decal, logicalReplacement, &sceneFailure) || runtime.GetScenePublisher().PollBinding(componentUpdateReceipt) != RenderSceneGpuBindingStatus::Accepted)
                return false;

            if (!bindings.SetDecalMaterial(decal, secondMaterial, &bindingFailure))
                return false;
            bool publicationPending = false;
            for (u32 attempt = 0; attempt < 4 && !publicationPending; ++attempt)
            {
                if (!residency.Update(&residencyFailure) || !residency.Update(&residencyFailure) || !bindings.Update(&bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure))
                    return false;
                publicationPending = info.awaitingScenePublication;
                if (!publicationPending && !AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick))
                    return false;
            }
            if (!publicationPending || info.activeResidency != firstResidency)
                return false;
            reporter.stage = 6;

            RenderSceneSnapshot snapshot;
            RenderSceneGpuPublication canceledPublication;
            if (!scenes.GetSnapshot(scene, snapshot) || !runtime.GetScenePublisher().Prepare(scene, snapshot.completedMutationEpoch, canceledPublication, &gpuSceneFailure) ||
                !runtime.GetScenePublisher().Cancel(canceledPublication, &gpuSceneFailure) || !bindings.Update(&bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure) ||
                !info.awaitingScenePublication || info.activeResidency != firstResidency)
                return false;
            reporter.stage = 7;

            for (u32 attempt = 0; attempt < 3 && info.activeResidency == firstResidency; ++attempt)
            {
                if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick) || !bindings.GetInfo(decal, info, &bindingFailure))
                    return false;
            }
            if (info.activeResidency == firstResidency || info.candidateResidency.IsValid() || !info.activeMaterial.IsValid() || bindings.GetStats().replacements != 1)
                return false;
            const MaterialResidencyHandle secondResidency = info.activeResidency;
            const GpuMaterialHandle secondGpuMaterial = info.activeMaterial;
            reporter.stage = 8;

            RenderSceneGpuBindingReceipt staleReceipt;
            RenderSceneGpuBindingReceipt currentReceipt;
            if (!runtime.GetScenePublisher().BindDecalMaterial(decal, info.activeMaterial, staleReceipt, &gpuSceneFailure) ||
                !runtime.GetScenePublisher().BindDecalMaterial(decal, info.activeMaterial, currentReceipt, &gpuSceneFailure) ||
                runtime.GetScenePublisher().PollBinding(staleReceipt) != RenderSceneGpuBindingStatus::Stale || runtime.GetScenePublisher().PollBinding(currentReceipt) != RenderSceneGpuBindingStatus::Pending)
                return false;
            reporter.stage = 9;
            if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick) || runtime.GetScenePublisher().PollBinding(currentReceipt) != RenderSceneGpuBindingStatus::Accepted)
                return false;
            reporter.stage = 10;
            if (!runtime.GetScenePublisher().BindDecalMaterial(decal, info.activeMaterial, staleReceipt, &gpuSceneFailure) ||
                runtime.GetScenePublisher().PollBinding(currentReceipt) != RenderSceneGpuBindingStatus::Stale || runtime.GetScenePublisher().PollBinding(staleReceipt) != RenderSceneGpuBindingStatus::Pending)
                return false;

            rhi::ResidencyFenceSet firstRetirementFences;
            rhi::Failure rhiFailure;
            const bool submittedFirstRetirement = SubmitRetirementFences(firstRetirementFences, rhiFailure);
            const bool sealedFirstRetirement = submittedFirstRetirement && residency.SealRetirements(firstRetirementFences, &residencyFailure);
            const bool requestedFirstReplacement = sealedFirstRetirement && bindings.SetDecalMaterial(decal, firstMaterial, &bindingFailure);
            if (!requestedFirstReplacement)
            {
                std::fprintf(stderr, "[material3C2] replacement setup: submit=%u seal=%u request=%u residency=%u binding=%u\n", submittedFirstRetirement, sealedFirstRetirement, requestedFirstReplacement,
                             static_cast<u32>(residencyFailure.code), static_cast<u32>(bindingFailure.code));
                return false;
            }
            bool replacementPending = false;
            for (u32 attempt = 0; attempt < 4 && !replacementPending; ++attempt)
            {
                if (!residency.Update(&residencyFailure) || !residency.Update(&residencyFailure) || !bindings.GetInfo(decal, info, &bindingFailure))
                    return false;
                MaterialResidencyInfo candidateInfo;
                if (!residency.GetInfo(info.candidateResidency, candidateInfo, &residencyFailure))
                    return false;
                if (candidateInfo.state != MaterialResidencyState::Resident)
                {
                    RenderCommandFrameTickResult replacementTick;
                    RenderCommandFailure replacementCommandFailure;
                    GpuSceneRuntimeFailure replacementRuntimeFailure;
                    if (!commands.FrameTick(scenes.GetFramePipelineScenes(), tick++, replacementTick, &replacementCommandFailure) || !commands.FlushPreviousFrameProcessing(&replacementCommandFailure) ||
                        commands.ConsumeExecutionFailure(replacementCommandFailure) || !runtime.ResolveContributions(&replacementRuntimeFailure) ||
                        runtime.ConsumePublicationFailure(replacementRuntimeFailure) || !residency.Update(&residencyFailure))
                        return false;
                }
                if (!bindings.Update(&bindingFailure) || !bindings.GetInfo(decal, info, &bindingFailure))
                    return false;
                replacementPending = info.awaitingScenePublication;
            }
            RenderSceneGpuBindingReceipt interferenceReceipt;
            if (!replacementPending || info.activeResidency != secondResidency || info.activeMaterial != secondGpuMaterial)
            {
                std::fprintf(stderr, "[material3C2] replacement staging: pending=%u active=%u material=%u state=%u\n", replacementPending, info.activeResidency == secondResidency,
                             info.activeMaterial == secondGpuMaterial, static_cast<u32>(info.candidateState));
                return false;
            }
            DecalProxyDesc secondDecalDesc = decalDesc;
            secondDecalDesc.proxy.debugName = "material scene-binding progress isolation";
            secondDecalDesc.material = resources::ResourceReference(secondMaterial.GetPath(), secondMaterial.GetType());
            secondDecalDesc.materialHandle = secondMaterial;
            RenderProxyHandle secondDecal;
            if (!scenes.CreateDecalProxy(secondDecalDesc, secondDecal, &sceneFailure) || !bindings.SetDecalMaterial(secondDecal, secondMaterial, &bindingFailure))
                return false;
            if (!runtime.GetScenePublisher().BindDecalMaterial(decal, info.activeMaterial, interferenceReceipt, &gpuSceneFailure))
                return false;
            const bool staleUpdate = bindings.Update(&bindingFailure);
            const MaterialSceneBindingFailure staleFailure = bindingFailure;
            const bool staleInfo = bindings.GetInfo(decal, info, &bindingFailure);
            MaterialSceneBindingInfo secondDecalInfo;
            const bool progressInfo = bindings.GetInfo(secondDecal, secondDecalInfo, &bindingFailure);
            if (staleUpdate || staleFailure.code != MaterialSceneBindingFailureCode::ScenePublicationFailure || !staleInfo || info.candidateResidency.IsValid() || info.activeResidency != secondResidency ||
                info.activeMaterial != secondGpuMaterial || !progressInfo || !secondDecalInfo.awaitingScenePublication)
            {
                std::fprintf(stderr, "[material3C2] stale correction: pending=%u update=%u code=%u info=%u candidate=%u active=%u material=%u progress=%u\n", replacementPending, staleUpdate,
                             static_cast<u32>(staleFailure.code), staleInfo, info.candidateResidency.IsValid(), info.activeResidency == secondResidency, info.activeMaterial == secondGpuMaterial,
                             progressInfo && secondDecalInfo.awaitingScenePublication);
                return false;
            }
            if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick) || !bindings.GetInfo(secondDecal, secondDecalInfo, &bindingFailure) ||
                secondDecalInfo.activeResidency != secondResidency || !scenes.DestroyProxy(secondDecal, &sceneFailure) || !bindings.Update(&bindingFailure) || bindings.GetStats().destroyedProxies != 1)
                return false;

            if (!bindings.Remove(decal, &bindingFailure))
                return false;
            for (u32 attempt = 0; attempt < 3 && bindings.GetStats().bindings != 0; ++attempt)
                if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick))
                    return false;
            rhi::ResidencyFenceSet secondRetirementFences;
            if (bindings.GetStats().bindings != 0 || !SubmitRetirementFences(secondRetirementFences, rhiFailure) || !residency.SealRetirements(secondRetirementFences, &residencyFailure) ||
                !bindings.SetDecalMaterial(decal, firstMaterial, &bindingFailure))
                return false;
            reporter.stage = 11;
            for (u32 attempt = 0; attempt < 4; ++attempt)
                if (!AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick))
                    return false;
            if (!scenes.DestroyProxy(decal, &sceneFailure) || !bindings.Update(&bindingFailure) || bindings.GetStats().bindings != 0 || bindings.GetStats().destroyedProxies != 2 ||
                !AdvanceSceneBinding(residency, bindings, runtime, commands, scenes, tick) || !bindings.Shutdown(&bindingFailure) || !scenes.DestroyScene(scene, &sceneFailure))
                return false;
            reporter.stage = 12;

            rhi::ResidencyFenceSet retirementFences;
            if (!SubmitRetirementFences(retirementFences, rhiFailure) || !residency.SealRetirements(retirementFences, &residencyFailure) || !residency.Shutdown(&residencyFailure))
                return false;
            pipelineCache.WaitIdle();
            const bool succeeded = pipelineCache.Shutdown();
            reporter.stage = succeeded ? 100u : 13u;
            return succeeded;
        }
    } // namespace

    bool RunMaterialResidencyRuntimeProof(MaterialMaterializer& materializer, GpuSceneRuntime& runtime, RenderCommandSystem& commands, RenderSceneManager& scenes, const resources::ResourceHandle& mesh) noexcept
    {
        RuntimeFixture fixture;
        if (!BuildFixture(fixture))
            return false;
        const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
        const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_material_residency_runtime");
        filesystem::Manager& files = filesystem::GetManager();
        static_cast<void>(files.DeletePath(directory));
        if (!files.CreatePath(directory))
            return false;
        const filesystem::AbsolutePath shaderPath = directory.AddFilePath("material_residency.vshader");
        const filesystem::AbsolutePath pipelinePath = directory.AddFilePath("material_residency.vppl");
        const filesystem::AbsolutePath materialPath = directory.AddFilePath("material_residency.vmat");
        if (!Save(shaderPath, fixture.shaderBytes) || !Save(pipelinePath, fixture.pipelineBytes) || !Save(materialPath, fixture.materialBytes))
            return false;

        resources::ResourceRegistry registry;
        resources::ResourcePipeline resourcePipeline;
        streaming::ResourceStreamer streamer;
        shader::ShaderResourceDecoderConfig shaderConfig;
        pipeline::PipelineResourceDecoderConfig pipelineConfig;
        material::MaterialResourceDecoderConfig materialConfig;
        if (!registry.Initialize() || !resourcePipeline.Initialize(registry) || !streamer.Initialize(resourcePipeline) ||
            !streamer.RegisterDecoder({shader::ShaderResourceType, "3C.1 shader", &shader::DecodeShaderResource, &shader::DestroyShaderResource, &shaderConfig}) ||
            !streamer.RegisterDecoder({pipeline::PipelineResourceType, "3C.1 pipeline", &pipeline::DecodePipelineResource, &pipeline::DestroyPipelineResource, &pipelineConfig}) ||
            !streamer.RegisterDecoder({material::MaterialResourceType, "3C.1 material", &material::DecodeMaterialResource, &material::DestroyMaterialResource, &materialConfig}))
            return false;
        const streaming::DependencyDescriptor pipelineDependency{fixture.shaderReference, resources::DependencyKind::Required};
        const streaming::DependencyDescriptor materialDependencies[]{{fixture.shaderReference, resources::DependencyKind::Required}, {fixture.pipelineReference, resources::DependencyKind::Required}};
        if (!streamer.RegisterLoose({fixture.shaderReference, shaderPath, {}, serialization::Crc64(fixture.shaderBytes.Data(), fixture.shaderBytes.Size()), 0}) ||
            !streamer.RegisterLoose({fixture.pipelineReference, pipelinePath, {&pipelineDependency, 1}, serialization::Crc64(fixture.pipelineBytes.Data(), fixture.pipelineBytes.Size()), 0}) ||
            !streamer.RegisterLoose({fixture.materialReference, materialPath, materialDependencies, serialization::Crc64(fixture.materialBytes.Data(), fixture.materialBytes.Size()), 0}) ||
            !streamer.RegisterLoose({fixture.secondMaterialReference, materialPath, materialDependencies, serialization::Crc64(fixture.materialBytes.Data(), fixture.materialBytes.Size()), 0}))
            return false;
        resources::PipelineRequest materialLoad = streamer.Request(fixture.materialReference);
        resources::PipelineRequest secondMaterialLoad = streamer.Request(fixture.secondMaterialReference);
        if (!materialLoad.TryWait(10'000) || !materialLoad.HasLoaded() || !secondMaterialLoad.TryWait(10'000) || !secondMaterialLoad.HasLoaded())
            return false;
        resources::ResourceHandle materialHandle = materialLoad.Acquire();
        resources::ResourceHandle secondMaterialHandle = secondMaterialLoad.Acquire();
        materialLoad.Reset();
        secondMaterialLoad.Reset();

        PipelineCache pipelineCache;
        MaterialResidencyRuntime residency;
        MaterialResidencyRuntimeFailure residencyFailure;
        MaterialResidencyRuntimeConfig residencyConfig;
        residencyConfig.maximumResidencies = 1;
        residencyConfig.maximumDemands = 2;
        residencyConfig.maximumTechniqueRequests = 2;
        residencyConfig.maximumNativePrograms = 1;
        if (!pipelineCache.Initialize() || !residency.Initialize(materializer, pipelineCache, {}, residencyConfig, &residencyFailure))
            return false;
        const MaterialMaterializerStats materializerStatsBefore = materializer.GetStats();

        MaterialDemandHandle cancelledDemand;
        if (!residency.RequestMaterial(materialHandle, cancelledDemand, &residencyFailure))
            return false;
        cancelledDemand.Reset();
        if (residency.GetStats().residencyRecords != 0 || residency.GetStats().liveDemands != 0)
            return false;

        MaterialDemandHandle firstDemand;
        MaterialDemandHandle secondDemand;
        MaterialDemandHandle exhaustedDemand;
        MaterialDemandHandle exhaustedResidency;
        if (!residency.RequestMaterial(materialHandle, firstDemand, &residencyFailure) || !residency.RequestMaterial(materialHandle, secondDemand, &residencyFailure) ||
            firstDemand.GetResidency() != secondDemand.GetResidency() || residency.RequestMaterial(materialHandle, exhaustedDemand, &residencyFailure) ||
            residencyFailure.code != MaterialResidencyRuntimeFailureCode::CapacityExceeded)
            return false;
        secondDemand.Reset();
        if (residency.RequestMaterial(secondMaterialHandle, exhaustedResidency, &residencyFailure) || residencyFailure.code != MaterialResidencyRuntimeFailureCode::CapacityExceeded)
            return false;
        if (!residency.Update(&residencyFailure) || !residency.Update(&residencyFailure))
            return false;
        RenderCommandFrameTickResult tick;
        RenderCommandFailure commandFailure;
        GpuSceneRuntimeFailure runtimeFailure;
        if (!commands.FrameTick(scenes.GetFramePipelineScenes(), 5, tick, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) || commands.ConsumeExecutionFailure(commandFailure) ||
            !runtime.ResolveContributions(&runtimeFailure) || runtime.ConsumePublicationFailure(runtimeFailure) || !residency.Update(&residencyFailure))
            return false;
        MaterialResidencyInfo residencyInfo;
        if (!residency.GetInfo(firstDemand.GetResidency(), residencyInfo, &residencyFailure) || residencyInfo.state != MaterialResidencyState::Resident || !residencyInfo.material.IsValid())
            return false;

        MaterialTechniqueRequest missingAttachments;
        const MaterialTechniqueDesc deferredTechnique{firstDemand.GetResidency(), 0x3c010003, nullptr, pipeline_cache::Priority::Normal};
        if (residency.RequestTechnique(deferredTechnique, missingAttachments, &residencyFailure) || residencyFailure.code != MaterialResidencyRuntimeFailureCode::PipelineFailure ||
            missingAttachments.IsValid() || !residency.GetInfo(firstDemand.GetResidency(), residencyInfo, &residencyFailure) || residencyInfo.state != MaterialResidencyState::Resident)
            return false;
        MaterialTechniqueRequest missingTechnique;
        if (residency.RequestTechnique({firstDemand.GetResidency(), 0xdeadbeef, nullptr, pipeline_cache::Priority::Normal}, missingTechnique, &residencyFailure) ||
            residencyFailure.code != MaterialResidencyRuntimeFailureCode::TechniqueNotFound)
            return false;

        pipeline::AttachmentSignature attachments;
        attachments.colorCount = 1;
        attachments.colors[0].format = pipeline::Format::R8G8B8A8UNorm;
        attachments.colors[0].numericClass = shader::NumericClass::FloatingPoint;
        MaterialTechniqueRequest firstTechnique;
        MaterialTechniqueRequest secondTechnique;
        const MaterialTechniqueDesc concreteTechnique{firstDemand.GetResidency(), 0x3c010003, &attachments, pipeline_cache::Priority::Critical};
        if (!residency.RequestTechnique(concreteTechnique, firstTechnique, &residencyFailure))
        {
            std::fprintf(stderr, "[material3C1] first technique failed: code=%u pipeline=%u shader=%u rhi=%u message=%s\n", static_cast<u32>(residencyFailure.code),
                         static_cast<u32>(residencyFailure.pipelineResult), static_cast<u32>(residencyFailure.shaderResult), static_cast<u32>(residencyFailure.rhiFailure.code),
                         residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        if (!residency.RequestTechnique(concreteTechnique, secondTechnique, &residencyFailure))
        {
            std::fprintf(stderr, "[material3C1] second technique failed: code=%u pipeline=%u shader=%u rhi=%u message=%s\n", static_cast<u32>(residencyFailure.code),
                         static_cast<u32>(residencyFailure.pipelineResult), static_cast<u32>(residencyFailure.shaderResult), static_cast<u32>(residencyFailure.rhiFailure.code),
                         residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        MaterialTechniqueInfo firstTechniqueInfo;
        MaterialTechniqueInfo secondTechniqueInfo;
        for (u32 attempt = 0; attempt < 100'000; ++attempt)
        {
            if (!residency.GetTechniqueInfo(firstTechnique, firstTechniqueInfo, &residencyFailure) || !residency.GetTechniqueInfo(secondTechnique, secondTechniqueInfo, &residencyFailure))
                return false;
            if (firstTechniqueInfo.state != MaterialTechniqueState::Pending && secondTechniqueInfo.state != MaterialTechniqueState::Pending)
                break;
            std::this_thread::yield();
        }
        const MaterialResidencyRuntimeStats proofStats = residency.GetStats();
        if (firstTechniqueInfo.state != MaterialTechniqueState::Ready || secondTechniqueInfo.state != MaterialTechniqueState::Ready || firstTechniqueInfo.pipeline != secondTechniqueInfo.pipeline ||
            firstTechniqueInfo.material != residencyInfo.material || proofStats.nativeProgramLoads != 1 || proofStats.nativeProgramReuses < 1 || proofStats.residencyLookupProbes != 1 ||
            proofStats.nativeProgramLookupProbes != proofStats.nativeProgramReuses || proofStats.residencyStateChecks == 0 || proofStats.residencyStateChecks > 3 ||
            pipelineCache.GetStats().coalescedRequests < 1)
            return false;
        secondTechnique.Reset();
        rhi::PipelineRef variants[3]{};
        for (u32 variant = 0; variant < 3; ++variant)
        {
            MaterialTechniqueDesc variantDesc = concreteTechnique;
            variantDesc.mirrored = (variant & 1u) == 0;
            variantDesc.twoSided = variant != 0;
            if (!residency.RequestTechnique(variantDesc, secondTechnique, &residencyFailure))
                return false;
            for (u32 attempt = 0; attempt < 100'000; ++attempt)
            {
                if (!residency.GetTechniqueInfo(secondTechnique, secondTechniqueInfo, &residencyFailure))
                    return false;
                if (secondTechniqueInfo.state != MaterialTechniqueState::Pending)
                    break;
                std::this_thread::yield();
            }
            if (secondTechniqueInfo.state != MaterialTechniqueState::Ready || secondTechniqueInfo.pipeline == firstTechniqueInfo.pipeline || secondTechniqueInfo.material != firstTechniqueInfo.material)
                return false;
            for (u32 previous = 0; previous < variant; ++previous)
                if (secondTechniqueInfo.pipeline == variants[previous])
                    return false;
            variants[variant] = secondTechniqueInfo.pipeline;
            secondTechnique.Reset();
        }
        firstTechnique.Reset();
        firstDemand.Reset();
        if (residency.GetStats().liveDemands != 0 || residency.GetStats().liveTechniqueRequests != 0 || residency.GetStats().residencyRecords != 1 || residency.Shutdown(&residencyFailure) ||
            residencyFailure.code != MaterialResidencyRuntimeFailureCode::LiveWorkRemains)
            return false;
        rhi::ResidencyFenceSet retirementFences;
        rhi::Failure rhiFailure;
        if (residency.SealRetirements({}, &residencyFailure) || residencyFailure.code != MaterialResidencyRuntimeFailureCode::MissingRetirementFence || !SubmitRetirementFences(retirementFences, rhiFailure) ||
            !residency.SealRetirements(retirementFences, &residencyFailure) || residency.GetStats().residencyRecords != 0 || residency.GetStats().retirementChecks != 1 ||
            materializer.GetStats().operationStateChecks == materializerStatsBefore.operationStateChecks || materializer.GetStats().operationStateChecks - materializerStatsBefore.operationStateChecks > 3 ||
            !residency.Shutdown(&residencyFailure))
            return false;
        pipelineCache.WaitIdle();
        if (!pipelineCache.Shutdown())
            return false;

        if (!RunSceneBindingProof(materialHandle, secondMaterialHandle, materializer, runtime, commands, scenes, mesh))
            return false;

        PipelineCache abandonmentPipelines;
        MaterialResidencyRuntime abandonmentResidency;
        MaterialDemandHandle abandonedDemand;
        if (!abandonmentPipelines.Initialize() || !abandonmentResidency.Initialize(materializer, abandonmentPipelines, {}, residencyConfig, &residencyFailure) ||
            !abandonmentResidency.RequestMaterial(materialHandle, abandonedDemand, &residencyFailure) || !abandonedDemand.IsValid() || !abandonmentResidency.AbandonDevice(&residencyFailure) ||
            abandonedDemand.IsValid())
            return false;
        abandonedDemand.Reset();
        if (!abandonmentResidency.Shutdown(&residencyFailure))
            return false;
        abandonmentPipelines.WaitIdle();
        if (!abandonmentPipelines.Shutdown())
            return false;

        materialHandle.Reset();
        secondMaterialHandle.Reset();
        if (!streamer.UnregisterLoose(fixture.secondMaterialReference.GetPath()) || !streamer.UnregisterLoose(fixture.materialReference.GetPath()) ||
            !streamer.UnregisterLoose(fixture.pipelineReference.GetPath()) || !streamer.UnregisterLoose(fixture.shaderReference.GetPath()) || !streamer.UnregisterDecoder(material::MaterialResourceType) ||
            !streamer.UnregisterDecoder(pipeline::PipelineResourceType) || !streamer.UnregisterDecoder(shader::ShaderResourceType) || !streamer.Shutdown() || !resourcePipeline.Shutdown() ||
            !registry.Shutdown())
            return false;
        static_cast<void>(files.DeleteFile(shaderPath));
        static_cast<void>(files.DeleteFile(pipelinePath));
        static_cast<void>(files.DeleteFile(materialPath));
        static_cast<void>(files.DeletePath(directory));
        std::puts("[material3C2] material residency, scene binding, and atomic replacement proof passed");
        return true;
    }
} // namespace vanguard::rendering::tests
