#pragma once

#include <vanguard/io/io.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)
#pragma warning(disable : 4324)
#pragma warning(disable : 4458)
#pragma warning(disable : 4996)
#endif

#include "../../../../imported/common/redCore/include/redCorePublic.h"
#include "../../../../imported/common/redFileSystem/include/redFileSystemPublic.h"
#include "../../../../imported/common/redFileSystem/include/bufferedReader.h"
#include "../../../../imported/common/redFileSystem/include/bufferedWriter.h"
#include "../../../../imported/common/redFileSystem/include/fileStringReader.h"
#include "../../../../imported/common/redFileSystem/include/fileStringWriter.h"
#include "../../../../imported/common/redFileSystem/include/fileSys.h"
#include "../../../../imported/common/redFileSystem/include/fileUtils.h"
#include "../../../../imported/common/redFileSystem/include/memoryFileReader.h"
#include "../../../../imported/common/redFileSystem/include/memoryFileWriter.h"
#include "../../../../imported/common/redFileSystem/include/nullFile.h"
#include "../../../../imported/common/redFileSystem/include/rawFileReader.h"
#include "../../../../imported/common/redFileSystem/include/rawFileWriter.h"
#include "../../../../imported/common/redFileSystem/include/system.h"
#if defined(RED_FILE_SYNC_SERVICE_ENABLED)
#include "../../../../imported/common/redFileSystem/include/fileSyncFileManager.h"
#include "../../../../imported/common/redFileSystem/include/fileSyncService.h"
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vanguard::filesystem
{
    using AbsolutePath = ::red::AbsolutePath;
    using File = ::IFile;
    using IFile = ::IFile;
    using Manager = ::CFileManager;
    using SystemIO = ::CSystemIO;
    using SystemFindFile = ::CSystemFindFile;
    using FileFlags = ::EFileFlags;
    using FileOpenFlags = ::EFileOpenFlags;

    using ::FF_Cloner;
    using ::FF_Cooker;
    using ::FF_ErrorOccured;
    using ::FF_FileBased;
    using ::FF_Mapper;
    using ::FF_MemoryBased;
    using ::FF_NullBased;
    using ::FF_Reader;
    using ::FF_ResourceCollector;
    using ::FF_ResourceResave;
    using ::FF_SaveStream;
    using ::FF_Writer;
    using ::FOF_AbsolutePath;
    using ::FOF_Append;
    using ::FOF_Buffered;
    using ::FOF_MapToMemory;
    using ::FOF_SafeWrite;

    using MemoryFileReader = ::CMemoryFileReader;
    using MemoryFileReaderWithBuffer = ::CMemoryFileReaderWithBuffer;
    using MemoryFileReaderExternalBuffer = ::CMemoryFileReaderExternalBuffer;
    using MemoryFileWriter = ::CMemoryFileWriter;
    using MemoryFileWriterWithDebugName = ::CMemoryFileWriterWithDebugName;
    using MemoryFileWriterExternalBuffer = ::CMemoryFileWriterExternalBuffer;
    using MemoryFileWriterExternalUniqueBuffer = ::CMemoryFileWriterExternalUniqueBuffer;
    using MemoryFileBufferedWriter = ::CMemoryFileBufferedWriter;
    using NullFileWriter = ::CNullFileWriter;
    using AnsiStringFileReader = ::red::CAnsiStringFileReader;
    using AnsiStringFileWriter = ::red::CAnsiStringFileWriter;
    using AnsiStringFileStream = ::red::CAnsiStringFileStream;

    using BufferedReader = ::fs::BufferedReader;
    using BufferedWriter = ::fs::BufferedWriter;
    using RawFileReader = ::fs::RawFileReader;
    using RawFileWriter = ::fs::RawFileWriter;

    using ::red::LoadFileToBuffer;
    using ::red::LoadFileToBufferChunked;
    using ::red::LoadFileToString;
    using ::red::SaveStringToFile;

#if defined(RED_FILE_SYNC_SERVICE_ENABLED)
    using FileSyncManager = ::FileSyncFileManager;
    using FileSyncMode = ::FileSyncServiceMode;
    using FileSyncConfig = ::FileSyncServiceConfig;
    using FileSyncService = ::FileSyncService;
#endif

    namespace paths = ::red::paths;
    namespace utils = ::fs;

    struct Config
    {
        AbsolutePath engineRoot;
        AbsolutePath gameRoot;
        AbsolutePath cacheRoot;
    };

    [[nodiscard]] bool Initialize(const Config& config) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] Manager& GetManager() noexcept;

    // Replaces target with a completed sibling staging file without deleting
    // target first. Requiring one parent directory keeps the operation on one
    // filesystem volume. On failure this function performs no cleanup, so the
    // caller can inspect or remove staged while the previous target remains.
    [[nodiscard]] bool ReplaceFile(const AbsolutePath& staged, const AbsolutePath& target) noexcept;
} // namespace vanguard::filesystem
