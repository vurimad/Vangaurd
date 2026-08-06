#include <type_traits>

#include <vanguard/system/assert.hpp>
#include <vanguard/system/build_config.hpp>
#include <vanguard/system/compiler.hpp>
#include <vanguard/system/platform.hpp>
#include <vanguard/system/types.hpp>

namespace
{
    static_assert(sizeof(void*) == 8);
    static_assert(VG_ARCH_X64 || VG_ARCH_ARM64);
    static_assert(VG_ENDIAN_LITTLE || VG_ENDIAN_BIG);
    static_assert(VG_BUILD_DEBUG || VG_BUILD_DEVELOPMENT || VG_BUILD_PROFILE || VG_BUILD_SHIPPING);
    static_assert(std::is_same_v<vanguard::u32, std::uint32_t>);
} // namespace

int main()
{
    int verifyCount = 0;
    VG_VERIFY(++verifyCount == 1);

    if (verifyCount != 1)
    {
        return 1;
    }

    int assertionEvaluationCount = 0;
    VG_ASSERT(++assertionEvaluationCount == 1);

#if VG_ENABLE_ASSERTS
    if (assertionEvaluationCount != 1)
    {
        return 2;
    }
#else
    if (assertionEvaluationCount != 0)
    {
        return 3;
    }
#endif

    if (!VG_ENSURE(verifyCount == 1))
    {
        return 4;
    }

    vanguard::system::WriteDebugMessage("[systemTests] All system contract tests passed.\n");
    return 0;
}
