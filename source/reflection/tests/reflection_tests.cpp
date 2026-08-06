#include <vanguard/reflection/reflection.hpp>

#include <cstdio>
#include <cstring>

namespace
{
    bool Require(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAILED: %s\n", message);
        }
        return condition;
    }
} // namespace

int main()
{
    using namespace vanguard::reflection;

    bool passed = true;
    passed &= Require(Initialize(), "reflection initializes");
    passed &= Require(IsInitialized(), "reflection reports initialized");
    passed &= Require(Initialize(), "reflection initialization is idempotent");

    const TypeDescriptor name = FindType("CName");
    passed &= Require(static_cast<bool>(name), "CName is registered");
    passed &= Require(name.name != nullptr && std::strcmp(name.name, "CName") == 0, "CName metadata preserves its name");
    passed &= Require(name.size != 0, "CName metadata has a size");
    passed &= Require(name.alignment != 0, "CName metadata has an alignment");
    passed &= Require(name.kind == TypeKind::Name, "CName metadata preserves its RED RTTI kind");

    const TypeDescriptor serializable = FindType("ISerializable");
    passed &= Require(static_cast<bool>(serializable), "the RED serializable object root is registered");
    passed &= Require(serializable.kind == TypeKind::Class, "ISerializable is registered as a class");

    passed &= Require(!static_cast<bool>(FindType("Vanguard.Does.Not.Exist")), "unknown names do not manufacture types");
    passed &= Require(!static_cast<bool>(FindType(nullptr)), "null names are rejected");

    if (!passed)
    {
        return 1;
    }

    std::puts("Vanguard reflection compatibility tests passed.");
    return 0;
}
