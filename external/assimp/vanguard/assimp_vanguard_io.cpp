#include "assimp_vanguard_io.hpp"

#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>

#include <filesystem>
#include <string>

namespace
{
    class TrackingIOSystem final : public Assimp::IOSystem
    {
    public:
        TrackingIOSystem(const char* sourceDirectory, const vanguard_assimp::OpenedFileCallback callback, void* userData)
            : m_sourceDirectory(sourceDirectory != nullptr ? sourceDirectory : ""), m_callback(callback), m_userData(userData)
        {
        }

        bool Exists(const char* file) const override { return m_delegate.Exists(file); }
        char getOsSeparator() const override { return m_delegate.getOsSeparator(); }

        Assimp::IOStream* Open(const char* file, const char* mode = "rb") override
        {
            std::filesystem::path resolved(file);
            Assimp::IOStream* stream = nullptr;
            if (resolved.is_relative())
            {
                const std::filesystem::path fromSource = m_sourceDirectory / resolved;
                stream = m_delegate.Open(fromSource.string().c_str(), mode);
                if (stream != nullptr)
                {
                    resolved = fromSource;
                }
            }
            if (stream == nullptr)
            {
                stream = m_delegate.Open(file, mode);
            }
            if (stream != nullptr && m_callback != nullptr)
            {
                std::error_code error;
                const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, error);
                const std::string path = (error ? resolved : canonical).generic_string();
                m_callback(path.c_str(), m_userData);
            }
            return stream;
        }

        void Close(Assimp::IOStream* file) override { m_delegate.Close(file); }
        bool ComparePaths(const char* first, const char* second) const override { return m_delegate.ComparePaths(first, second); }

    private:
        Assimp::DefaultIOSystem m_delegate;
        std::filesystem::path m_sourceDirectory;
        vanguard_assimp::OpenedFileCallback m_callback = nullptr;
        void* m_userData = nullptr;
    };
} // namespace

namespace vanguard_assimp
{
    Assimp::IOSystem* CreateTrackingIOSystem(const char* sourceDirectory, const OpenedFileCallback callback, void* userData)
    {
        return new TrackingIOSystem(sourceDirectory, callback, userData);
    }
} // namespace vanguard_assimp
