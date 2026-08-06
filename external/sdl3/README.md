# SDL3 integration

Vanguard builds the unmodified SDL 3.4.14 Visual Studio project from `D:/vendors/SDL-release-3.4.14`. The Premake
makefile wrapper maps Vanguard Debug to SDL Debug and Development, Profile, and Shipping to SDL Release. Object files
are redirected to `build/obj/SDL3/<configuration>` and `SDL3.lib`/`SDL3.dll` to `bin/<configuration>`; the vendor tree
remains source-only. SDL is an implementation dependency of platform adapters and must not appear in portable engine APIs.
