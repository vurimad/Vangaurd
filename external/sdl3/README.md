# SDL3 integration

Vanguard builds the repository-contained SDL 3.4.14 Visual Studio project from `external/sdl3/upstream`. The Premake
makefile wrapper maps Vanguard Debug to SDL Debug and Development, Profile, and Shipping to SDL Release. Object files
are redirected to `build/obj/SDL3/<configuration>` and `SDL3.lib`/`SDL3.dll` to the shared build-output directory; the
upstream tree remains source-only. SDL is an implementation dependency of platform adapters and must not appear in
portable engine APIs.
