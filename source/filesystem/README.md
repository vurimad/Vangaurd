# Vanguard Filesystem

Vanguard Filesystem carries RED's complete physical-filesystem image behind
`vanguard::filesystem`. It provides absolute paths, the global file manager,
raw and buffered streams, memory streams, safe and append writes, traversal,
copy/move/delete operations, and the development FileSync service.

This module owns physical files. Archive and package mounting remain a separate
future module because RED also separates `redFileSystem` from `archive`.

## Lifetime

Initialize Memory, Diagnostics, Containers, and I/O first:

```cpp
vanguard::memory::Initialize();
vanguard::diagnostics::Initialize();
vanguard::containers::Initialize();
vanguard::io::Initialize();

const auto root =
    vanguard::filesystem::paths::GetCurrentWorkingDirectory();
vanguard::filesystem::Initialize({root, root, root});
```

The application composition root owns initialization and shutdown. Shutdown
Filesystem before I/O and Diagnostics. Initialization is idempotent while the
service is running.

## Typical use

```cpp
const auto path = root.AddFilePath("data/example.bin");
auto writer = vanguard::filesystem::GetManager().CreateFileWriter(
    path,
    vanguard::filesystem::FOF_Buffered |
        vanguard::filesystem::FOF_SafeWrite);

writer->Serialize(data, size);
writer->Flush();
writer.Reset(); // publishes a safe write through RED's preserved contract
```

Memory streams accept Vanguard `DynamicArray<u8>` storage. Mapped readers, raw
streams, path utilities, string-file helpers, and traversal use the same RED
contracts under Vanguard names. Invalid seeks and ownership misuse retain RED's
explicit assertions.

RED chunked-LZ4 framing, RED file-version constants, compressed-number
serialization, and skippable-block helpers remain private compatibility
implementation. They are intentionally not Vanguard format contracts.

## Build configurations

Debug, Development, and Profile preserve RED FileSync support and stage
FileSync/UDT binaries beside executables. Shipping follows RED Final behavior
and compiles FileSync out. Debug uses the debug Oodle binary; the other
configurations use its release binary.

`filesystemTests` validates lifecycle, configured roots, memory streams, raw
and manager-backed disk I/O, buffering, mapped reads, safe-write publication,
traversal, copy/move/delete, read-only flags, path queries, null streams, and
repeated reader creation.
