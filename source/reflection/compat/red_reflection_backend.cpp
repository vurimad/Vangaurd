#include <vanguard/reflection/reflection_backend.hpp>

#include <vanguard/memory/memory.hpp>

#include "../../imported/common/redReflection/include/redReflectionPublic.h"
#include "../../imported/common/redReflection/include/rttiSystem.h"
#include "../../imported/common/redReflection/include/rttiType.h"
#include "../../imported/common/redSystem/include/module.h"

extern void InitializeModule_redConfig();
extern void InitializeModule_commProtocol();
extern void InitializeModule_commChannel();
extern void InitializeModule_redReflection();

namespace
{
    bool g_initialized = false;

    vanguard::reflection::TypeDescriptor Describe(const rtti::IType* type) noexcept
    {
        if (type == nullptr)
        {
            return {};
        }

        return {type, type->GetName().AsChar(), type->GetSize(), type->GetAlignment(), static_cast<vanguard::reflection::TypeKind>(type->GetType())};
    }
} // namespace

namespace vanguard::reflection::backend
{
    bool Initialize() noexcept
    {
        if (g_initialized)
        {
            return true;
        }

        if (!vanguard::memory::Initialize())
        {
            return false;
        }

        ::InitializeModule_redConfig();
        ::InitializeModule_commProtocol();
        ::InitializeModule_commChannel();
        ::InitializeModule_redReflection();
        g_initialized = true;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_initialized;
    }

    TypeDescriptor FindType(const char* name) noexcept
    {
        if (!g_initialized || name == nullptr || name[0] == '\0')
        {
            return {};
        }

        return Describe(GetRttiSystem().FindType(NameBuilder::Build(name)));
    }

    TypeDescriptor FindTypeByHash(const u64 nameHash) noexcept
    {
        if (!g_initialized || nameHash == 0)
        {
            return {};
        }

        return Describe(GetRttiSystem().FindType(CName(nameHash)));
    }
} // namespace vanguard::reflection::backend
