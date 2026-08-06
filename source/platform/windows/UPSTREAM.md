# Upstream provenance

The adapter retains RED's proven policy that Windows products enter through `wWinMain`, command-line state is established at that native boundary, message pumping occurs once per application iteration, and process quit requests initiate orderly engine shutdown. It was informed by `launcher/src/mainWindows.cpp`, `editor/editorLauncher/src/main.cpp`, `appPlatformWindows.cpp`, and `GameAppInstance::Run` under `D:/root/R6.Root/Mainline/dev/src`.

Vanguard additionally normalizes the complete native command line to UTF-8 and uses an injected platform-host instance with typed pump results instead of static platform functions and global engine exit requests. Windows-only types remain private to this module.

SDL 3.4.14 now owns the native event pump and gamepad backend beneath `WindowsPlatformHost`. Vanguard builds the
unmodified official Visual Studio project through a Premake wrapper, redirects every artifact outside the vendor tree,
and keeps SDL event and device identities behind portable Vanguard contracts.
