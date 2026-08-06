#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>

#include <cstdio>
#include <cstring>
#include <utility>

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[ioTests] FAILED: %s\n", message);
            ++failures;
        }
    }

    struct AsyncReadResult
    {
        vanguard::concurrency::ManualResetEvent completed;
        vanguard::io::AsyncResult result = vanguard::io::eAsyncResult_Unknown;
        vanguard::u32 bytesTransferred = 0;
        vanguard::u32 memoryOffset = 0;
        vanguard::io::ShareableIOMemory memory;
    };

    void OnAsyncRead(const vanguard::io::AsyncReadToken& token, const vanguard::io::AsyncResult result,
                     const vanguard::u32 bytesTransferred, vanguard::io::ShareableIOMemory memory, const vanguard::u32 memoryOffset,
                     vanguard::io::UniqueBuffer)
    {
        auto* const state = static_cast<AsyncReadResult*>(token.m_userData);
        state->result = result;
        state->bytesTransferred = bytesTransferred;
        state->memoryOffset = memoryOffset;
        state->memory = std::move(memory);
        state->completed.Signal();
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;

    Check(!io::Initialize(), "initialization rejects missing dependencies");
    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "ioTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    Check(io::IsInitialized(), "I/O initialized state");
    Check(io::Initialize(), "I/O idempotent initialization");
    Check(&io::System() == &io::System(), "stable global I/O system");

    char absolutePath[io::MaxPathLength] = {};
    const char* const localPath = "vanguard_io_conformance.tmp";
    Check(::GetFullPathNameA(localPath, static_cast<DWORD>(sizeof(absolutePath)), absolutePath, nullptr) != 0, "test file absolute path");

    constexpr char payload[] = "RED Vanguard complete RED I/O adaptation conformance payload";
    constexpr vanguard::u32 payloadSize = static_cast<vanguard::u32>(sizeof(payload));

    {
        io::NativeFileHandle file;
        Check(file.Open(absolutePath, io::eOpenFlag_WriteNew), "native file create");
        Check(file.IsValid(), "native file valid after open");

        vanguard::u32 bytesWritten = 0;
        Check(file.Write(payload, payloadSize, bytesWritten), "native blocking write");
        Check(bytesWritten == payloadSize, "native write byte count");
        Check(file.Tell() == payloadSize, "native tell after write");
        Check(file.GetFileSize() == payloadSize, "native file size");
        Check(file.Flush(), "native file flush");
        Check(file.Close(), "native file close");
        Check(!file.IsValid(), "native file invalid after close");
    }

    {
        io::NativeFileHandle file;
        Check(file.Open(absolutePath, io::eOpenFlag_Read), "native file reopen");

        char readBuffer[payloadSize] = {};
        vanguard::u32 bytesRead = 0;
        Check(file.Read(readBuffer, payloadSize, bytesRead), "native blocking read");
        Check(bytesRead == payloadSize, "native read byte count");
        Check(std::memcmp(readBuffer, payload, payloadSize) == 0, "native read payload");
        Check(file.Seek(0, io::eSeekOrigin_Set) && file.Tell() == 0, "native seek");
        Check(file.Close(), "native reader close");
    }

    Check(io::System().OpenFile("vanguard_io_file_that_does_not_exist.tmp") == io::InvalidFileHandle, "missing async file is rejected");

    io::AsyncIOStats statsBefore;
    io::System().GetStats(statsBefore);

    const io::FileHandle asyncFile = io::System().OpenFile(absolutePath);
    Check(asyncFile != io::InvalidFileHandle, "async file open");
    if (asyncFile != io::InvalidFileHandle)
    {
        Check(io::System().GetFileSize(asyncFile) == payloadSize, "async file size");
        Check(std::strcmp(io::System().GetFileName(asyncFile), absolutePath) == 0, "async file name");
        Check(io::System().GetAsyncFlags(asyncFile) == io::eAsyncFlag_None, "async file flags");

        io::System().AddRefFile(asyncFile);
        io::System().ReleaseFile(asyncFile);

        char asyncBuffer[payloadSize] = {};
        AsyncReadResult asyncResult;
        io::AsyncReadToken token;
        token.m_callback = &OnAsyncRead;
        token.m_userData = &asyncResult;
        token.m_buffer = asyncBuffer;
        token.m_debugLogicalFileName = "ioTests.AsyncRead";
        token.m_offset = 0;
        token.m_numberOfBytesToRead = payloadSize;
        token.m_requestSource = io::RequestSource::Tools;

        io::System().BeginRead(asyncFile, token, io::eAsyncPriority_GAME);
        Check(asyncResult.completed.TryWait(10000), "async read completion");
        Check(asyncResult.result == io::eAsyncResult_Success, "async read result");
        Check(asyncResult.bytesTransferred == payloadSize, "async read byte count");
        Check(std::memcmp(asyncBuffer, payload, payloadSize) == 0, "async read payload");

        AsyncReadResult ownedResult;
        io::AsyncReadToken ownedToken;
        ownedToken.m_callback = &OnAsyncRead;
        ownedToken.m_userData = &ownedResult;
        ownedToken.m_debugLogicalFileName = "ioTests.OwnedAsyncRead";
        ownedToken.m_offset = 0;
        ownedToken.m_numberOfBytesToRead = payloadSize;
        ownedToken.m_requestSource = io::RequestSource::Tools;

        io::System().BeginRead(asyncFile, ownedToken, io::eAsyncPriority_UI);
        Check(ownedResult.completed.TryWait(10000), "allocator-backed async read completion");
        Check(ownedResult.result == io::eAsyncResult_Success, "allocator-backed async read result");
        Check(ownedResult.bytesTransferred == payloadSize && ownedResult.memory.GetSize() >= payloadSize, "allocator-backed async memory");
        Check(ownedResult.memoryOffset == 0 && ownedResult.memory.GetBaseFileOffset() == 0, "allocator-backed async offsets");
        Check(std::memcmp(ownedResult.memory.Data(), payload, payloadSize) == 0, "allocator-backed async payload");
        ownedResult.memory.Reset();

        constexpr vanguard::u32 stressReadCount = 256;
        AsyncReadResult stressResults[stressReadCount];
        char stressBuffers[stressReadCount][payloadSize] = {};
        for (vanguard::u32 index = 0; index < stressReadCount; ++index)
        {
            io::AsyncReadToken stressToken;
            stressToken.m_callback = &OnAsyncRead;
            stressToken.m_userData = &stressResults[index];
            stressToken.m_buffer = stressBuffers[index];
            stressToken.m_debugLogicalFileName = "ioTests.QueueStress";
            stressToken.m_offset = 0;
            stressToken.m_numberOfBytesToRead = payloadSize;
            stressToken.m_requestSource = io::RequestSource::Tools;

            const io::AsyncPriority priority = index % 4 == 0   ? io::eAsyncPriority_GAME
                                               : index % 4 == 1 ? io::eAsyncPriority_UI
                                               : index % 4 == 2 ? io::eAsyncPriority_AUDIO
                                                                : io::eAsyncPriority_FULLSCREENVIDEO;
            io::System().BeginRead(asyncFile, stressToken, priority);
        }

        for (vanguard::u32 index = 0; index < stressReadCount; ++index)
        {
            Check(stressResults[index].completed.TryWait(10000), "queued async stress completion");
            Check(stressResults[index].result == io::eAsyncResult_Success, "queued async stress result");
            Check(stressResults[index].bytesTransferred == payloadSize && std::memcmp(stressBuffers[index], payload, payloadSize) == 0,
                  "queued async stress payload");
        }

        io::System().ReleaseFile(asyncFile);
    }

    io::AsyncIOStats statsAfter;
    io::System().GetStats(statsAfter);
    Check(statsAfter.bytesReadTotal >= statsBefore.bytesReadTotal + payloadSize * (2 + 256), "async byte telemetry");

    io::IOContext context;
    Check(context.GetLoadingState() == io::IOContext::LoadingState::Idle, "I/O context initial state");
    context.SetLoadingState(io::IOContext::LoadingState::QueuedForIO);
    Check(context.GetLoadingState() == io::IOContext::LoadingState::QueuedForIO, "I/O context loading state");
    Check(context.TryUpdateDistanceToObserverSquared(120, 1, 3), "I/O context first distance update");
    Check(context.TryUpdateDistanceToObserverSquared(80, 1, 2), "I/O context nearer distance update");
    Check(!context.TryUpdateDistanceToObserverSquared(160, 1, 4), "I/O context rejects farther same-generation update");
    context.SetAsyncOpId(1);
    context.RequestCancel();
    Check(context.IsCancelRequested(), "I/O context cancellation");

    Check(std::strcmp(io::GetRequestSourceDebugText(io::RequestSource::Tools), "Tools") == 0, "request source debug text");

    io::AsyncIO::SortFlags sortFlags;
    sortFlags.loadingMode = true;
    io::System().SortIOQueues(sortFlags);
    io::System().ADVANCED_BeginBulkReadThreadLocal();
    io::System().ADVANCED_FinishBulkReadThreadLocal();

    io::Shutdown();
    Check(!io::IsInitialized(), "I/O shutdown state");
    Check(!io::Initialize(), "I/O one-lifetime contract");

    diagnostics::Shutdown();
    static_cast<void>(::DeleteFileA(absolutePath));

    if (failures == 0)
    {
        std::puts("[ioTests] all RED I/O adaptation checks passed");
    }
    return failures == 0 ? 0 : 1;
}
