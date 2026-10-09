#pragma once

#include <vanguard/editor/editor_service_ids.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::editor
{
    using EditorResourceId = u64;

    inline constexpr EditorResourceId InvalidEditorResourceId = 0;
    inline constexpr EditorResourceId EditorUiRegularFontResourceId = 0x656472666f6e7401ull;
    inline constexpr EditorResourceId EditorUiSemiboldFontResourceId = 0x656472666f6e7402ull;
    inline constexpr EditorResourceId EditorVanguardEngineWordmarkResourceId = 0x6564726c6f676f01ull;

    enum class EditorResourceKind : u8
    {
        Font,
        SvgIcon,
        SvgImage,
        RasterImage,
        Cursor,
        Data
    };

    struct EditorResourceDescriptor final
    {
        EditorResourceId id = InvalidEditorResourceId;
        EditorResourceKind kind = EditorResourceKind::Data;
        const char* relativePath = nullptr;
        bool required = false;
    };

    struct EditorResourceLocation final
    {
        EditorResourceId id = InvalidEditorResourceId;
        EditorResourceKind kind = EditorResourceKind::Data;
        filesystem::AbsolutePath path;
        bool required = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return id != InvalidEditorResourceId && !path.Empty() && path.IsFilePath();
        }
    };

    struct EditorResourceServiceConfig final
    {
        // Immutable application-owned root, normally <process-image-directory>/Editor/.
        filesystem::AbsolutePath root;
    };

    class EditorResourceService : public application::Service
    {
    public:
        ~EditorResourceService() override = default;

        [[nodiscard]] virtual const filesystem::AbsolutePath& GetRoot() const noexcept = 0;
        [[nodiscard]] virtual bool Resolve(EditorResourceId resource, EditorResourceLocation& output) const noexcept = 0;

        // Extension resources remain subordinate to the compiled extension catalog.
        // This resolves a known extension's immutable files; it never scans for plugins.
        [[nodiscard]] virtual bool ResolveExtensionResource(containers::StringView resourceNamespace,
                                                            EditorResourceKind kind,
                                                            containers::StringView relativePath,
                                                            filesystem::AbsolutePath& output) const noexcept = 0;

    protected:
        EditorResourceService() noexcept = default;
    };

    [[nodiscard]] bool RegisterEditorResourceService(application::EngineHost& host, const EditorResourceServiceConfig& config,
                                                     application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorResourceService* FindEditorResourceService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorResourceService* FindEditorResourceService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
