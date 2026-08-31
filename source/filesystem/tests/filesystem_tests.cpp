#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <array>
#include <cstdio>

namespace
{
    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[filesystemTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    constexpr auto MakePayload()
    {
        std::array<vanguard::u8, 8192> data{};
        for (vanguard::usize index = 0; index < data.size(); ++index)
        {
            data[index] = static_cast<vanguard::u8>((index * 31u + index / 7u) & 0xffu);
        }
        return data;
    }

    constexpr auto g_payload = MakePayload();

    bool ReadAndCompare(vanguard::filesystem::IFile& reader)
    {
        std::array<vanguard::u8, g_payload.size()> result{};
        reader.Serialize(result.data(), result.size());
        return result == g_payload;
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;

    const filesystem::AbsolutePath workingDirectory = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath testDirectory = workingDirectory.AddDirPath("vanguard_filesystem_conformance");
    const filesystem::AbsolutePath nestedDirectory = testDirectory.AddDirPath("nested");
    const filesystem::AbsolutePath rawPath = testDirectory.AddFilePath("raw.bin");
    const filesystem::AbsolutePath copiedPath = testDirectory.AddFilePath("copied.bin");
    const filesystem::AbsolutePath movedPath = nestedDirectory.AddFilePath("moved.bin");
    const filesystem::AbsolutePath managedPath = testDirectory.AddFilePath("managed.bin");
    const filesystem::AbsolutePath safePath = testDirectory.AddFilePath("safe.bin");
    const filesystem::AbsolutePath replacementTarget = testDirectory.AddFilePath("replacement.bin");
    const filesystem::AbsolutePath replacementStaged = testDirectory.AddFilePath("replacement.bin.tmp");

    Check(!filesystem::Initialize({workingDirectory, workingDirectory, testDirectory}), "initialization rejects missing dependencies");
    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "filesystemTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");

    const filesystem::Config config{workingDirectory, workingDirectory, testDirectory};
    Check(filesystem::Initialize(config), "filesystem initialization");
    Check(filesystem::IsInitialized(), "initialized state");
    Check(filesystem::Initialize(config), "idempotent initialization");
    Check(&filesystem::GetManager() == &filesystem::GetManager(), "stable global manager");

    filesystem::Manager& manager = filesystem::GetManager();
    static_cast<void>(manager.DeleteFile(rawPath));
    static_cast<void>(manager.DeleteFile(copiedPath));
    static_cast<void>(manager.DeleteFile(movedPath));
    static_cast<void>(manager.DeleteFile(managedPath));
    static_cast<void>(manager.DeleteFile(safePath));
    static_cast<void>(manager.DeleteFile(replacementTarget));
    static_cast<void>(manager.DeleteFile(replacementStaged));
    static_cast<void>(manager.DeletePath(nestedDirectory));
    static_cast<void>(manager.DeletePath(testDirectory));

    Check(manager.CreatePath(testDirectory), "create test directory");
    Check(manager.CreatePath(nestedDirectory), "create nested directory");
    Check(manager.GetEngineRoot() == workingDirectory && manager.GetGameRoot() == workingDirectory && manager.GetCacheDirectory() == testDirectory,
          "configured roots");

    containers::DynamicArray<vanguard::u8> memoryData(memory::pools::Filesystem::GetInstance());
    {
        filesystem::MemoryFileWriter writer(memoryData);
        writer.Serialize(const_cast<vanguard::u8*>(g_payload.data()), g_payload.size());
        writer.Flush();
        Check(writer.GetSize() == g_payload.size() && writer.GetOffset() == g_payload.size(), "memory writer size and offset");
    }
    {
        filesystem::MemoryFileReader reader(memoryData, 0);
        Check(ReadAndCompare(reader), "memory stream round trip");
    }

    {
        auto writer = filesystem::RawFileWriter::Create(rawPath, false);
        Check(static_cast<bool>(writer), "raw writer create");
        if (writer)
        {
            writer->Serialize(const_cast<vanguard::u8*>(g_payload.data()), g_payload.size());
            writer->Flush();
        }
    }
    {
        auto reader = filesystem::RawFileReader::Create(rawPath);
        Check(static_cast<bool>(reader), "raw reader create");
        if (reader)
        {
            Check(reader->GetSize() == g_payload.size(), "raw reader size");
            Check(ReadAndCompare(*reader), "raw disk round trip");
        }
    }

    {
        auto writer = manager.CreateFileWriter(managedPath, filesystem::FOF_Buffered);
        Check(static_cast<bool>(writer), "manager buffered writer");
        if (writer)
        {
            writer->Serialize(const_cast<vanguard::u8*>(g_payload.data()), g_payload.size());
            writer->Flush();
        }
    }
    {
        auto reader = manager.CreateFileReader(managedPath, filesystem::FOF_Buffered);
        Check(static_cast<bool>(reader), "manager buffered reader");
        if (reader)
        {
            Check(ReadAndCompare(*reader), "manager buffered round trip");
        }
    }
    {
        auto reader = manager.CreateFileReader(managedPath, filesystem::FOF_MapToMemory);
        Check(static_cast<bool>(reader), "manager mapped reader");
        if (reader)
        {
            Check(ReadAndCompare(*reader), "manager mapped round trip");
        }
    }
    {
        auto writer = manager.CreateFileWriter(safePath, filesystem::FOF_SafeWrite);
        Check(static_cast<bool>(writer), "safe writer create");
        if (writer)
        {
            writer->Serialize(const_cast<vanguard::u8*>(g_payload.data()), g_payload.size());
            writer->Flush();
        }
    }
    Check(manager.FileExist(safePath), "safe-write publication");

    Check(manager.CopyFile(rawPath, copiedPath, false), "copy file");
    Check(manager.FileExist(copiedPath), "copied file exists");
    Check(manager.MoveFile(copiedPath, movedPath), "move file");
    Check(!manager.FileExist(copiedPath) && manager.FileExist(movedPath), "move file state");

    const std::array<vanguard::u8, 3> oldReplacement = {1, 2, 3};
    const std::array<vanguard::u8, 4> newReplacement = {9, 8, 7, 6};
    {
        auto oldWriter = manager.CreateFileWriter(replacementTarget, filesystem::FOF_Buffered);
        auto stagedWriter = manager.CreateFileWriter(replacementStaged, filesystem::FOF_Buffered);
        Check(oldWriter && stagedWriter, "replacement fixture writers");
        if (oldWriter && stagedWriter)
        {
            oldWriter->Serialize(const_cast<vanguard::u8*>(oldReplacement.data()), oldReplacement.size());
            stagedWriter->Serialize(const_cast<vanguard::u8*>(newReplacement.data()), newReplacement.size());
            oldWriter->Flush();
            stagedWriter->Flush();
        }
    }
    Check(filesystem::ReplaceFile(replacementStaged, replacementTarget) && !manager.FileExist(replacementStaged) &&
              manager.GetFileSize(replacementTarget) == newReplacement.size(),
          "same-directory replacement publishes staged file");
    {
        auto stagedWriter = manager.CreateFileWriter(replacementStaged, filesystem::FOF_Buffered);
        Check(static_cast<bool>(stagedWriter), "cross-directory rejection fixture");
        if (stagedWriter)
        {
            stagedWriter->Serialize(const_cast<vanguard::u8*>(oldReplacement.data()), oldReplacement.size());
            stagedWriter->Flush();
        }
    }
    Check(!filesystem::ReplaceFile(replacementStaged, movedPath) && manager.FileExist(replacementStaged) &&
              manager.GetFileSize(movedPath) == g_payload.size(),
          "replacement rejects non-siblings without touching either file");
    static_cast<void>(manager.DeleteFile(replacementStaged));
    static_cast<void>(manager.DeleteFile(replacementTarget));
    Check(manager.SetFileReadOnly(movedPath, true), "set read-only");
    Check(manager.IsFileReadOnly(movedPath), "query read-only");
    Check(manager.SetFileReadOnly(movedPath, false), "clear read-only");

    containers::DynamicArray<filesystem::AbsolutePath> foundFiles(memory::pools::Filesystem::GetInstance());
    manager.FindFiles(testDirectory, containers::String("*.bin"), foundFiles, true);
    Check(foundFiles.Size() == 4, "recursive wildcard traversal");
    Check(filesystem::paths::GetFileName(rawPath) == containers::StringView("raw.bin"), "path filename extraction");
    Check(filesystem::paths::GetExtension(rawPath) == containers::StringView("bin"), "path extension extraction");
    Check(filesystem::paths::IsSubpath(testDirectory, movedPath), "subpath classification");

    {
        filesystem::NullFileWriter nullWriter;
        nullWriter.Serialize(const_cast<vanguard::u8*>(g_payload.data()), g_payload.size());
        Check(nullWriter.GetOffset() == g_payload.size(), "null writer accounting");
    }

    constexpr vanguard::u32 stressPasses = 128;
    for (vanguard::u32 pass = 0; pass < stressPasses; ++pass)
    {
        auto reader = manager.CreateFileReader(managedPath, (pass & 1u) != 0u ? filesystem::FOF_Buffered : filesystem::FOF_MapToMemory);
        Check(static_cast<bool>(reader), "stress reader create");
        if (reader)
        {
            Check(ReadAndCompare(*reader), "stress reader payload");
        }
    }

    Check(manager.DeleteFile(rawPath), "delete raw file");
    Check(manager.DeleteFile(movedPath), "delete moved file");
    Check(manager.DeleteFile(managedPath), "delete managed file");
    Check(manager.DeleteFile(safePath), "delete safe file");
    Check(manager.DeletePath(nestedDirectory), "delete nested directory");
    Check(manager.DeletePath(testDirectory), "delete test directory");

    filesystem::Shutdown();
    Check(!filesystem::IsInitialized(), "shutdown state");
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::printf("[filesystemTests] complete RED filesystem adaptation passed "
                    "(%zu bytes, %u stress passes)\n",
                    g_payload.size(), stressPasses);
    }
    return g_failures == 0 ? 0 : 1;
}
