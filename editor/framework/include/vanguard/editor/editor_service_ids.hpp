#pragma once

#include <vanguard/application/service.hpp>

namespace vanguard::editor
{
    inline constexpr application::ModuleId EditorModuleId = 0x656469746f720001ull;

    inline constexpr application::ServiceId ProjectWorkspaceServiceId = 0x656470726f6a7301ull;
    inline constexpr application::CapabilityId ProjectWorkspaceCapabilityId = 0x656470726f6a6301ull;

    inline constexpr application::ServiceId EditorUiServiceId = 0x6564756973657201ull;
    inline constexpr application::CapabilityId EditorUiCapabilityId = 0x6564756963617001ull;

    inline constexpr application::ServiceId EditorResourceServiceId = 0x6564726573737601ull;
    inline constexpr application::CapabilityId EditorResourceCapabilityId = 0x6564726573636101ull;

    inline constexpr application::ServiceId EditorActivityServiceId = 0x6564616374737601ull;
    inline constexpr application::CapabilityId EditorActivityCapabilityId = 0x6564616374636101ull;

    inline constexpr application::ServiceId EditorFileDialogServiceId = 0x656466646c677301ull;
    inline constexpr application::CapabilityId EditorFileDialogCapabilityId = 0x656466646c676301ull;

    inline constexpr application::ServiceId EditorCommandServiceId = 0x6564636d64737601ull;
    inline constexpr application::CapabilityId EditorCommandCapabilityId = 0x6564636d64636101ull;

    inline constexpr application::ServiceId EditorShellServiceId = 0x65647368656c6c01ull;
    inline constexpr application::CapabilityId EditorShellCapabilityId = 0x6564736863617001ull;

    inline constexpr application::ServiceId EditorUserStateServiceId = 0x6564757374737601ull;
    inline constexpr application::CapabilityId EditorUserStateCapabilityId = 0x6564757374636101ull;

    inline constexpr application::ServiceId EditorSceneViewServiceId = 0x6573636e76777301ull;
} // namespace vanguard::editor
