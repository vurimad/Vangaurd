#include <vanguard/crypto/crypto.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>

namespace
{
    using namespace vanguard;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[cryptoTests] FAILED: %s", message);
            ++g_failures;
        }
    }

    [[nodiscard]] u8 HexNibble(const char value) noexcept
    {
        return value >= '0' && value <= '9' ? static_cast<u8>(value - '0') : static_cast<u8>(value - 'a' + 10);
    }

    [[nodiscard]] crypto::Digest256 DigestFromHex(const char* const hex) noexcept
    {
        crypto::Digest256 digest;
        for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
        {
            digest.bytes[index] = static_cast<u8>((HexNibble(hex[index * 2u]) << 4u) | HexNibble(hex[index * 2u + 1u]));
        }
        return digest;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "cryptoTests"), "diagnostics initialization");

    const crypto::Digest256 empty = crypto::Sha256(nullptr, 0);
    Check(empty == DigestFromHex("e3b0c44298fc1c149afbf4c8996fb924"
                                 "27ae41e4649b934ca495991b7852b855"),
          "SHA-256 empty NIST vector");

    const char abc[] = {'a', 'b', 'c'};
    Check(crypto::Sha256(abc, 3) == DigestFromHex("ba7816bf8f01cfea414140de5dae2223"
                                                  "b00361a396177a9cb410ff61f20015ad"),
          "SHA-256 abc NIST vector");

    crypto::Sha256Builder incremental;
    crypto::Digest256 incrementalDigest;
    Check(incremental.Update(abc, 1) && incremental.Update(abc + 1, 2) && incremental.Finalize(incrementalDigest) &&
              incrementalDigest == crypto::Sha256(abc, 3) && incremental.IsFinalized() && !incremental.Update(abc, 1) &&
              !incremental.Finalize(incrementalDigest),
          "incremental and terminal-state contract");

    char thousandAs[1000];
    for (u32 index = 0; index < 1000; ++index)
    {
        thousandAs[index] = 'a';
    }

    crypto::Sha256Builder longBuilder;
    for (u32 block = 0; block < 1000; ++block)
    {
        Check(longBuilder.Update(thousandAs, sizeof(thousandAs)), "SHA-256 long-vector update");
    }
    crypto::Digest256 millionAs;
    Check(longBuilder.Finalize(millionAs) && millionAs == DigestFromHex("cdc76e5c9914fb9281a1c7e284d73e67"
                                                                        "f1809a48a497200e046d39ccc7112cd0"),
          "SHA-256 million-a NIST vector");

    if (g_failures == 0)
    {
        VG_LOG_INFO(diagnostics::Category::FunctionalTests, "[cryptoTests] Vanguard SHA-256 checks passed");
    }
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
