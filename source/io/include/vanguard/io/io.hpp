#pragma once

#include <vanguard/containers/containers.hpp>

// Complete compatibility I/O image. This header is the sole Vanguard public adaptation
// boundary; normal engine code uses vanguard::io and never ::io directly.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#pragma warning(disable : 4996)
#endif

#include "../../../../imported/common/redIO/include/redIOPublic.h"
#include "../../../../imported/common/redIO/include/redIOAsyncFileHandleCache.h"
#include "../../../../imported/common/redIO/include/redIOAsyncIO.h"
#include "../../../../imported/common/redIO/include/redIOProfilerInterface.h"
#include "../../../../imported/common/redIO/include/redIOStats.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vanguard::io
{
    using InitSetup = ::io::InitSetup;
    using BufferSlice = ::io::BufferSlice;
    using FileHandle = ::io::TFileHandle;
    using TFileHandle = ::io::TFileHandle;

    inline constexpr FileHandle InvalidFileHandle = ::io::INVALID_FILE_HANDLE;
    inline constexpr FileHandle INVALID_FILE_HANDLE = ::io::INVALID_FILE_HANDLE;
    inline constexpr u32 MaxPathLength = ::io::REDIO_MAX_PATH_LENGTH;
    inline constexpr u32 MaxAsyncOperations = ::io::REDIO_MAX_ASYNC_OPS;
    inline constexpr u32 MaxFileHandles = ::io::REDIO_MAX_FILE_HANDLES;
    inline constexpr u32 MaxDirectoryHandles = ::io::REDIO_MAX_DIR_HANDLES;

    using OpenFlag = ::io::EOpenFlag;
    using EOpenFlag = ::io::EOpenFlag;
    using ::io::eOpenFlag_Append;
    using ::io::eOpenFlag_Async;
    using ::io::eOpenFlag_Create;
    using ::io::eOpenFlag_Read;
    using ::io::eOpenFlag_ReadWrite;
    using ::io::eOpenFlag_ReadWriteNew;
    using ::io::eOpenFlag_Truncate;
    using ::io::eOpenFlag_Unbuffered;
    using ::io::eOpenFlag_Write;
    using ::io::eOpenFlag_WriteNew;

    using AsyncFlag = ::io::EAsyncFlag;
    using EAsyncFlag = ::io::EAsyncFlag;
    using ::io::eAsyncFlag_DeferredOpen;
    using ::io::eAsyncFlag_None;
    using ::io::eAsyncFlag_TryCloseFileWhenNotUsed;
    using ::io::eAsyncFlag_Unbuffered;

    using AsyncPriority = ::io::EAsyncPriority;
    using EAsyncPriority = ::io::EAsyncPriority;
    using ::io::eAsyncPriority_AboveNormal;
    using ::io::eAsyncPriority_AUDIO;
    using ::io::eAsyncPriority_AUDIO_CRITICAL;
    using ::io::eAsyncPriority_Background;
    using ::io::eAsyncPriority_COLLISION_CRITICAL;
    using ::io::eAsyncPriority_COUNT;
    using ::io::eAsyncPriority_DEFAULT;
    using ::io::eAsyncPriority_FULLSCREENVIDEO;
    using ::io::eAsyncPriority_GAME;
    using ::io::eAsyncPriority_GAMEPLAY_CRITICAL;
    using ::io::eAsyncPriority_High;
    using ::io::eAsyncPriority_INVALID;
    using ::io::eAsyncPriority_Normal;
    using ::io::eAsyncPriority_RESERVED_CRITICAL;
    using ::io::eAsyncPriority_Streaming;
    using ::io::eAsyncPriority_UI;

    using SeekOrigin = ::io::ESeekOrigin;
    using ESeekOrigin = ::io::ESeekOrigin;
    using ::io::eSeekOrigin_Current;
    using ::io::eSeekOrigin_End;
    using ::io::eSeekOrigin_Set;

    using AsyncResult = ::red::EAsyncResult;
    using ::red::eAsyncResult_Canceled;
    using ::red::eAsyncResult_Error;
    using ::red::eAsyncResult_Pending;
    using ::red::eAsyncResult_Success;
    using ::red::eAsyncResult_Undeferred;
    using ::red::eAsyncResult_Unknown;

    using UniqueBuffer = ::red::UniqueBuffer;
    using ShareableIOMemory = ::io::ShareableIOMemory;
    using AsyncOperationCallback = ::io::FAsyncOpCallback;
    using FAsyncOpCallback = ::io::FAsyncOpCallback;
    using IOContext = ::io::IOContext;
    using RequestSource = ::io::RequestSource;
    using AsyncReadToken = ::io::AsyncReadToken;
    using NativeFileHandle = ::io::NativeFileHandle;
    using AsyncFile = ::io::AsyncFile;
    using AsyncFileHandleCache = ::io::AsyncFileHandleCache;
    using AsyncIO = ::io::AsyncIO;
    using RuntimeIOMemoryMetrics = ::io::RuntimeIOMemoryMetrics;
    using AsyncOpRequestStats = ::io::AsyncOpRequestStats;
    using AsyncOpStats = ::io::AsyncOpStats;
    using SortByBytesTotalRead = ::io::SortByBytesTotalRead;
    using AsyncIOStats = ::io::AsyncIOStats;
    using AsyncIOStatsAvailableInFinal = ::io::AsyncIOStatsAvailableInFinal;

#ifdef RED_PROFILE_FILE_SYSTEM
    using Profiler = ::IIOProfiler;
#endif

    [[nodiscard]] bool Initialize(const InitSetup& setup = InitSetup{}) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] AsyncIO& GetSystem() noexcept;
    [[nodiscard]] const char* GetRequestSourceDebugText(RequestSource source) noexcept;
} // namespace vanguard::io
