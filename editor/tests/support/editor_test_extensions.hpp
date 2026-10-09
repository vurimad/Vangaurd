#pragma once

#include <vanguard/editor/editor_extension.hpp>

namespace vanguard::editor::testing
{
    // Test-only extension identities. These exist solely to exercise the real
    // editor extension/command/panel machinery from native and focused tests.
    inline constexpr EditorExtensionId StaticTestBaseExtensionId{0x6564746573740001ull};
    inline constexpr EditorExtensionId StaticTestDependencyExtensionId{0x6564746573740002ull};
    inline constexpr EditorExtensionId StaticTestProjectExtensionId{0x6564746573740003ull};
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorTestExtensions() noexcept;
} // namespace vanguard::editor::testing
