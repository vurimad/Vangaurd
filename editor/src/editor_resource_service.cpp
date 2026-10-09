#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_toolbar_icons.hpp>

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/memory/memory.hpp>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;

    constexpr editor::EditorResourceDescriptor CoreResources[]{
        {editor::EditorUiRegularFontResourceId, editor::EditorResourceKind::Font,
         "Fonts/SourceSans3/SourceSans3-Regular.ttf", true},
        {editor::EditorUiSemiboldFontResourceId, editor::EditorResourceKind::Font,
         "Fonts/SourceSans3/SourceSans3-Semibold.ttf", true},
        {editor::EditorVanguardEngineWordmarkResourceId, editor::EditorResourceKind::SvgImage,
         "Images/Branding/VanguardEngine.svg", false}};

    [[nodiscard]] const editor::EditorResourceDescriptor* FindCoreResource(const editor::EditorResourceId id) noexcept
    {
        for (const editor::EditorResourceDescriptor& resource : CoreResources)
            if (resource.id == id)
                return &resource;
        for (const editor::detail::EditorToolbarIconDescriptor& icon : editor::detail::GetEditorToolbarIconDescriptors())
            if (icon.resource.id == id)
                return &icon.resource;
        return nullptr;
    }

    [[nodiscard]] bool ValidateRequiredResource(const editor::EditorResourceDescriptor& descriptor,
                                                const vanguard::filesystem::AbsolutePath& root,
                                                vanguard::filesystem::Manager& files) noexcept
    {
        if (!descriptor.required)
            return true;
        const vanguard::filesystem::AbsolutePath path = root.AddFilePath(descriptor.relativePath);
        return !path.Empty() && path.IsFilePath() && files.FileExist(path);
    }

    [[nodiscard]] bool ValidNamespace(const vanguard::containers::StringView value) noexcept
    {
        if (value.Empty() || value.Length() >= 128u)
            return false;
        for (vanguard::u32 index = 0; index < value.Length(); ++index)
        {
            const char c = value[index];
            const bool alphaNumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!alphaNumeric && c != '-' && c != '_')
                return false;
        }
        return true;
    }

    [[nodiscard]] bool ValidRelativeResourcePath(const vanguard::containers::StringView value) noexcept
    {
        if (value.Empty() || value.Length() >= 384u || value[0] == '/' || value[0] == '\\')
            return false;
        vanguard::u32 segmentStart = 0;
        for (vanguard::u32 index = 0; index <= value.Length(); ++index)
        {
            const bool end = index == value.Length();
            const char c = end ? '/' : value[index];
            if (!end && (c == '\\' || c == ':'))
                return false;
            if (c != '/')
                continue;
            const vanguard::u32 segmentLength = index - segmentStart;
            if (segmentLength == 0u ||
                (segmentLength == 1u && value[segmentStart] == '.') ||
                (segmentLength == 2u && value[segmentStart] == '.' && value[segmentStart + 1u] == '.'))
                return false;
            segmentStart = index + 1u;
        }
        return true;
    }

    class ManagedEditorResourceService final : public editor::EditorResourceService
    {
    public:
        explicit ManagedEditorResourceService(const editor::EditorResourceServiceConfig* const config) noexcept
            : m_root(config != nullptr ? config->root : vanguard::filesystem::AbsolutePath{})
        {
        }

        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetRoot() const noexcept override { return m_root; }

        [[nodiscard]] bool Resolve(const editor::EditorResourceId resource, editor::EditorResourceLocation& output) const noexcept override
        {
            output = {};
            const editor::EditorResourceDescriptor* const descriptor = FindCoreResource(resource);
            if (descriptor == nullptr || descriptor->relativePath == nullptr || m_root.Empty())
                return false;
            output.id = descriptor->id;
            output.kind = descriptor->kind;
            output.path = m_root.AddFilePath(descriptor->relativePath);
            output.required = descriptor->required;
            return output.IsValid();
        }

        [[nodiscard]] bool ResolveExtensionResource(const vanguard::containers::StringView resourceNamespace,
                                                    const editor::EditorResourceKind,
                                                    const vanguard::containers::StringView relativePath,
                                                    vanguard::filesystem::AbsolutePath& output) const noexcept override
        {
            output.Clear();
            if (m_root.Empty() || !ValidNamespace(resourceNamespace) || !ValidRelativeResourcePath(relativePath))
                return false;
            output = m_root.AddDirPath("Extensions").AddDirPath(resourceNamespace).AddFilePath(relativePath);
            return !output.Empty() && output.IsFilePath();
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (m_root.Empty() || !m_root.IsDirectoryPath() || !vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Editor resource root is invalid or filesystem is unavailable");
            vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
            for (const editor::EditorResourceDescriptor& descriptor : CoreResources)
                if (!ValidateRequiredResource(descriptor, m_root, files))
                    return app::LifecycleStatus::Failure("A required editor resource is missing from the editor image");
            for (const editor::detail::EditorToolbarIconDescriptor& icon : editor::detail::GetEditorToolbarIconDescriptors())
                if (!ValidateRequiredResource(icon.resource, m_root, files))
                    return app::LifecycleStatus::Failure("A required editor toolbar icon is missing from the editor image");
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::filesystem::AbsolutePath m_root;
    };

    app::Service* CreateEditorResourceService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(vanguard::memory::PoolId::Editor,
                                                                         sizeof(ManagedEditorResourceService),
                                                                         alignof(ManagedEditorResourceService));
        return block ? ::new (block.address) ManagedEditorResourceService(
                           static_cast<const editor::EditorResourceServiceConfig*>(userData)) : nullptr;
    }

    void DestroyEditorResourceService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorResourceService*>(service)->~ManagedEditorResourceService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorResourceService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorResourceService(application::EngineHost& host, const EditorResourceServiceConfig& config,
                                       application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FilesystemServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorResourceCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorResourceServiceId;
        descriptor.name = "editorResources";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 1};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorResourceService;
        descriptor.destroy = DestroyEditorResourceService;
        descriptor.userData = const_cast<EditorResourceServiceConfig*>(&config);
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorResourceService* FindEditorResourceService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorResourceService*>(host.FindCapability(EditorResourceCapabilityId));
    }

    EditorResourceService* FindEditorResourceService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorResourceService*>(context.FindCapability(EditorResourceCapabilityId));
    }
} // namespace vanguard::editor
