#include <vanguard/material_tools/material_preview.hpp>

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;

    [[nodiscard]] bool CopyOutput(const assets::BuildOutput& source, assets::BuildOutput& destination) noexcept
    {
        assets::BuildOutput candidate;
        candidate.disposition = source.disposition;
        candidate.buildFingerprint = source.buildFingerprint;
        candidate.contentFingerprint = source.contentFingerprint;
        candidate.artifacts.Reserve(source.artifacts.Size());
        if (candidate.artifacts.Capacity() < source.artifacts.Size())
            return false;
        for (const assets::Artifact& artifact : source.artifacts)
        {
            assets::Artifact artifactCopy;
            artifactCopy.resource = artifact.resource;
            artifactCopy.segment = artifact.segment;
            artifactCopy.flags = artifact.flags;
            artifactCopy.alignmentLog2 = artifact.alignmentLog2;
            artifactCopy.bytes.Resize(artifact.bytes.Size());
            if (artifactCopy.bytes.Size() != artifact.bytes.Size())
                return false;
            for (u32 index = 0; index < artifact.bytes.Size(); ++index)
                artifactCopy.bytes[index] = artifact.bytes[index];
            candidate.artifacts.PushBack(static_cast<assets::Artifact&&>(artifactCopy));
        }
        destination = static_cast<assets::BuildOutput&&>(candidate);
        return true;
    }

    [[nodiscard]] const assets::Artifact* Primary(const assets::BuildOutput& output) noexcept
    {
        for (const assets::Artifact& artifact : output.artifacts)
            if (assets::HasFlag(artifact.flags, assets::ArtifactFlags::Primary))
                return &artifact;
        return nullptr;
    }

    [[nodiscard]] u64 OutputBytes(const assets::BuildOutput& output) noexcept
    {
        u64 bytes = 0;
        for (const assets::Artifact& artifact : output.artifacts)
        {
            if (artifact.bytes.Size() > ~u64{0} - bytes)
                return ~u64{0};
            bytes += artifact.bytes.Size();
        }
        return bytes;
    }
} // namespace

namespace vanguard::material_tools
{
    struct MaterialPreviewService::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Tools);

        explicit Impl(const MaterialPreviewLimits& value) noexcept
            : limits(value), pipelineRequests(memory::pools::Tools::GetInstance()), lastMaterial()
        {
        }

        static bool Resolve(const assets::BuildDependency& dependency, assets::BuildRequest& request, void* userData) noexcept
        {
            Impl& implementation = *static_cast<Impl*>(userData);
            const MaterialPreviewBuildSet* const build = implementation.submitting;
            if (build != nullptr)
            {
                if (build->program.output == dependency.identity)
                {
                    request = build->program;
                    return true;
                }
                for (const assets::BuildRequest& pipeline : build->pipelines)
                {
                    if (pipeline.output == dependency.identity)
                    {
                        request = pipeline;
                        return true;
                    }
                }
            }
            return implementation.fallback != nullptr && implementation.fallback(dependency, request, implementation.fallbackUserData);
        }

        void ResetCurrent() noexcept
        {
            materialRequest.Reset();
            for (assets::GraphRequest& request : pipelineRequests)
                request.Reset();
            pipelineRequests.Clear();
            programRequest.Reset();
        }

        void CancelAndWaitCurrent() noexcept
        {
            static_cast<void>(programRequest.Cancel());
            for (assets::GraphRequest& request : pipelineRequests)
                static_cast<void>(request.Cancel());
            static_cast<void>(materialRequest.Cancel());
            programRequest.Wait();
            for (const assets::GraphRequest& request : pipelineRequests)
                request.Wait();
            materialRequest.Wait();
            ResetCurrent();
        }

        [[nodiscard]] bool AllFinished() const noexcept
        {
            if (!programRequest.HasFinished() || !materialRequest.HasFinished())
                return false;
            for (const assets::GraphRequest& request : pipelineRequests)
                if (!request.HasFinished())
                    return false;
            return true;
        }

        [[nodiscard]] bool AnyFailed() const noexcept
        {
            if (!programRequest.HasSucceeded() || !materialRequest.HasSucceeded())
                return true;
            for (const assets::GraphRequest& request : pipelineRequests)
                if (!request.HasSucceeded())
                    return true;
            return false;
        }

        [[nodiscard]] bool Accept() noexcept
        {
            assets::BuildOutput program;
            assets::BuildOutput material;
            if (!programRequest.CopyOutput(program) || !materialRequest.CopyOutput(material))
                return false;
            const assets::Artifact* const programArtifact = Primary(program);
            const assets::Artifact* const materialArtifact = Primary(material);
            if (programArtifact == nullptr || materialArtifact == nullptr)
                return false;
            filesystem::MemoryFileReaderExternalBuffer shaderReader(programArtifact->bytes.Data(), programArtifact->bytes.Size(), nullptr);
            shaders::ShaderFile shader;
            if (shader.Open(shaderReader) != shaders::Result::Success || !shader.HasMaterialContract())
                return false;

            containers::DynamicArray<assets::BuildOutput> pipelines{memory::pools::Tools::GetInstance()};
            pipelines.Reserve(pipelineRequests.Size());
            if (pipelines.Capacity() < pipelineRequests.Size())
                return false;
            const u64 programBytes = OutputBytes(program);
            const u64 materialBytes = OutputBytes(material);
            if (programBytes == ~u64{0} || materialBytes > ~u64{0} - programBytes)
                return false;
            u64 retainedBytes = programBytes + materialBytes;
            for (assets::GraphRequest& request : pipelineRequests)
            {
                assets::BuildOutput output;
                if (!request.CopyOutput(output))
                    return false;
                const assets::Artifact* const artifact = Primary(output);
                if (artifact == nullptr)
                    return false;
                filesystem::MemoryFileReaderExternalBuffer pipelineReader(artifact->bytes.Data(), artifact->bytes.Size(), nullptr);
                pipelines::PipelineFile pipeline;
                if (pipeline.Open(pipelineReader) != pipelines::Result::Success ||
                    pipelines::ValidateShaderCompatibility(pipeline, shader) != pipelines::Result::Success)
                    return false;
                const u64 outputBytes = OutputBytes(output);
                if (outputBytes > ~u64{0} - retainedBytes)
                    return false;
                retainedBytes += outputBytes;
                pipelines.PushBack(static_cast<assets::BuildOutput&&>(output));
            }
            if (retainedBytes > limits.maximumRetainedArtifactBytes)
                return false;
            filesystem::MemoryFileReaderExternalBuffer materialReader(materialArtifact->bytes.Data(), materialArtifact->bytes.Size(), nullptr);
            materials::MaterialFile materialFile;
            if (materialFile.Open(materialReader) != materials::Result::Success || materialFile.GetShader() != programArtifact->resource ||
                materialFile.GetTechniques().Count() != pipelines.Size())
                return false;
            const containers::ArraySpan<const materials::TechniqueRecord> materialTechniques = materialFile.GetTechniques();
            for (u32 index = 0; index < materialTechniques.Count(); ++index)
            {
                const assets::Artifact* const pipelineArtifact = Primary(pipelines[index]);
                if (pipelineArtifact == nullptr || materialTechniques[index].pipeline != pipelineArtifact->resource)
                    return false;
            }
            return CopyOutput(material, lastMaterial);
        }

        assets::BuildGraph graph;
        assets::ResolveGeneratedDependencyFunction fallback = nullptr;
        void* fallbackUserData = nullptr;
        const MaterialPreviewBuildSet* submitting = nullptr;
        MaterialPreviewLimits limits;
        assets::GraphRequest programRequest;
        containers::DynamicArray<assets::GraphRequest> pipelineRequests;
        assets::GraphRequest materialRequest;
        assets::BuildOutput lastMaterial;
        assets::BuildReport lastReport;
        u64 currentRevision = 0;
        u64 lastValidRevision = 0;
        MaterialPreviewState state = MaterialPreviewState::Idle;
    };

    MaterialPreviewService::~MaterialPreviewService() { static_cast<void>(Shutdown()); }

    bool MaterialPreviewService::Initialize(assets::BuildSystem& buildSystem, const assets::ResolveGeneratedDependencyFunction fallbackResolver,
                                            void* const fallbackUserData, const assets::BuildGraphConfig& graphConfig,
                                            const MaterialPreviewLimits& limits) noexcept
    {
        if (m_impl != nullptr || limits.maximumPipelines == 0 || limits.maximumRetainedArtifactBytes == 0)
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools)(limits);
        if (implementation == nullptr)
            return false;
        implementation->fallback = fallbackResolver;
        implementation->fallbackUserData = fallbackUserData;
        if (!implementation->graph.Initialize(buildSystem, Impl::Resolve, implementation, graphConfig, nullptr))
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        m_impl = implementation;
        return true;
    }

    bool MaterialPreviewService::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        m_impl->CancelAndWaitCurrent();
        if (!m_impl->graph.Shutdown())
            return false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool MaterialPreviewService::IsInitialized() const noexcept { return m_impl != nullptr; }

    u64 MaterialPreviewService::Submit(const MaterialPreviewBuildSet& build) noexcept
    {
        if (m_impl == nullptr || !build.program.IsValid() || !build.material.IsValid() || build.pipelines.Empty() ||
            build.pipelines.Count() > m_impl->limits.maximumPipelines)
            return 0;
        m_impl->ResetCurrent();
        m_impl->lastReport.Reset();
        ++m_impl->currentRevision;
        if (m_impl->currentRevision == 0)
            ++m_impl->currentRevision;
        m_impl->submitting = &build;
        m_impl->programRequest = m_impl->graph.Request(build.program, assets::BuildPriority::High);
        m_impl->pipelineRequests.Reserve(build.pipelines.Count());
        for (const assets::BuildRequest& pipeline : build.pipelines)
        {
            assets::GraphRequest request = m_impl->graph.Request(pipeline, assets::BuildPriority::High);
            m_impl->pipelineRequests.PushBack(static_cast<assets::GraphRequest&&>(request));
        }
        m_impl->materialRequest = m_impl->graph.Request(build.material, assets::BuildPriority::High);
        m_impl->submitting = nullptr;
        if (!m_impl->programRequest || !m_impl->materialRequest || m_impl->pipelineRequests.Size() != build.pipelines.Count())
        {
            m_impl->ResetCurrent();
            m_impl->state = MaterialPreviewState::Failed;
            return m_impl->currentRevision;
        }
        for (const assets::GraphRequest& request : m_impl->pipelineRequests)
        {
            if (!request)
            {
                m_impl->ResetCurrent();
                m_impl->state = MaterialPreviewState::Failed;
                return m_impl->currentRevision;
            }
        }
        m_impl->state = MaterialPreviewState::Building;
        return m_impl->currentRevision;
    }

    MaterialPreviewState MaterialPreviewService::Poll() noexcept
    {
        if (m_impl == nullptr || m_impl->state != MaterialPreviewState::Building || !m_impl->AllFinished())
            return m_impl != nullptr ? m_impl->state : MaterialPreviewState::Idle;
        if (m_impl->AnyFailed())
        {
            static_cast<void>(m_impl->materialRequest.CopyReport(m_impl->lastReport));
            const MaterialPreviewState completedState = m_impl->materialRequest.GetStatus() == assets::BuildState::Cancelled
                                                            ? MaterialPreviewState::Cancelled
                                                            : MaterialPreviewState::Failed;
            m_impl->ResetCurrent();
            m_impl->state = completedState;
            return m_impl->state;
        }
        const bool accepted = m_impl->Accept();
        m_impl->ResetCurrent();
        if (!accepted)
        {
            m_impl->state = MaterialPreviewState::Failed;
            return m_impl->state;
        }
        m_impl->lastReport.Reset();
        m_impl->lastValidRevision = m_impl->currentRevision;
        m_impl->state = MaterialPreviewState::Valid;
        return m_impl->state;
    }

    void MaterialPreviewService::Wait() noexcept
    {
        if (m_impl == nullptr || m_impl->state != MaterialPreviewState::Building)
            return;
        m_impl->programRequest.Wait();
        for (const assets::GraphRequest& request : m_impl->pipelineRequests)
            request.Wait();
        m_impl->materialRequest.Wait();
        static_cast<void>(Poll());
    }

    bool MaterialPreviewService::Cancel() noexcept
    {
        if (m_impl == nullptr || m_impl->state != MaterialPreviewState::Building)
            return false;
        m_impl->CancelAndWaitCurrent();
        m_impl->state = MaterialPreviewState::Cancelled;
        return true;
    }

    u64 MaterialPreviewService::CurrentRevision() const noexcept { return m_impl != nullptr ? m_impl->currentRevision : 0; }
    u64 MaterialPreviewService::LastValidRevision() const noexcept { return m_impl != nullptr ? m_impl->lastValidRevision : 0; }
    MaterialPreviewState MaterialPreviewService::State() const noexcept { return m_impl != nullptr ? m_impl->state : MaterialPreviewState::Idle; }

    bool MaterialPreviewService::CopyLastValidMaterial(assets::BuildOutput& output) const noexcept
    {
        return m_impl != nullptr && m_impl->lastValidRevision != 0 && CopyOutput(m_impl->lastMaterial, output);
    }

    bool MaterialPreviewService::CopyLastReport(assets::BuildReport& report) const noexcept
    {
        report.Reset();
        return m_impl != nullptr && report.CopyFrom(m_impl->lastReport);
    }

    bool MaterialPreviewService::Apply(const ApplyMaterialRevisionFunction apply, void* const userData) noexcept
    {
        return m_impl != nullptr && apply != nullptr && m_impl->lastValidRevision != 0 && apply(m_impl->lastValidRevision, userData);
    }
} // namespace vanguard::material_tools
