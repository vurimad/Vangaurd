group "ThirdParty"

project "libtiff"
    kind "StaticLib"
    language "C"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files { "libtiff/*.h", "libtiff/*.c", "libtiff/LICENSE.md" }
    removefiles {
        "libtiff/mkspans.c", "libtiff/tif_jbig.c", "libtiff/tif_jpeg_12.c", "libtiff/tif_lerc.c",
        "libtiff/tif_lzma.c", "libtiff/tif_ojpeg.c", "libtiff/tif_webp.c", "libtiff/tif_zstd.c"
    }
    includedirs { "libtiff", "zlib", "libjpeg-turbo" }
    defines { "TIFF_STATIC", "Z_PREFIX", "_CRT_SECURE_NO_WARNINGS" }
    links { "zlib", "libjpegTurbo" }
    removefiles { "libtiff/tif_unix.c", "libtiff/tif_win32.c" }

project "openjph"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "On"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "openexr/OpenJPH/openjph/**.h", "openexr/OpenJPH/codestream/**.h", "openexr/OpenJPH/codestream/**.cpp",
        "openexr/OpenJPH/coding/**.h", "openexr/OpenJPH/coding/**.cpp", "openexr/OpenJPH/others/**.h",
        "openexr/OpenJPH/others/**.c", "openexr/OpenJPH/others/**.cpp", "openexr/OpenJPH/transform/**.h",
        "openexr/OpenJPH/transform/**.cpp", "openexr/LICENSE.OpenJPH"
    }
    removefiles {
        "openexr/OpenJPH/**/*_sse.cpp", "openexr/OpenJPH/**/*_sse2.cpp", "openexr/OpenJPH/**/*_ssse3.cpp",
        "openexr/OpenJPH/**/*_avx.cpp", "openexr/OpenJPH/**/*_avx2.cpp", "openexr/OpenJPH/**/*_avx512.cpp",
        "openexr/OpenJPH/**/*_wasm.cpp"
    }
    includedirs {
        "openexr/OpenJPH", "openexr/OpenJPH/openjph", "openexr/OpenJPH/codestream",
        "openexr/OpenJPH/coding", "openexr/OpenJPH/others", "openexr/OpenJPH/transform"
    }
    defines { "OJPH_DISABLE_SIMD", "_FILE_OFFSET_BITS=64", "_CRT_SECURE_NO_WARNINGS" }

project "openexrCore"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "On"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "openexr/OpenEXRCore/**.h", "openexr/OpenEXRCore/**.c", "openexr/OpenEXRCore/internal_ht.cpp",
        "openexr/OpenEXRCore/internal_ht_common.cpp", "openexr/Imath/**.h", "openexr/deflate/**.h",
        "openexr/LICENSE.OpenEXR.md", "openexr/LICENSE.Imath.md", "openexr/LICENSE.libdeflate"
    }
    includedirs { "openexr", "openexr/Imath", "openexr/OpenEXRCore", "openexr/OpenJPH", "openexr/deflate" }
    defines { "OPENEXRCORE_EXPORTS", "OJPH_DISABLE_SIMD", "_CRT_SECURE_NO_WARNINGS" }
    links { "openjph" }

project "zlib"
    kind "StaticLib"
    language "C"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "zlib/*.h", "zlib/adler32.c", "zlib/compress.c", "zlib/crc32.c", "zlib/deflate.c", "zlib/infback.c",
        "zlib/inffast.c", "zlib/inflate.c", "zlib/inftrees.c", "zlib/trees.c", "zlib/uncompr.c", "zlib/zutil.c",
        "zlib/LICENSE"
    }
    includedirs { "zlib" }
    defines { "Z_PREFIX" }

project "libpng"
    kind "StaticLib"
    language "C"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "libpng/*.h", "libpng/png.c", "libpng/pngerror.c", "libpng/pngget.c", "libpng/pngmem.c", "libpng/pngpread.c",
        "libpng/pngread.c", "libpng/pngrio.c", "libpng/pngrtran.c", "libpng/pngrutil.c", "libpng/pngset.c",
        "libpng/pngtrans.c", "libpng/pngwio.c", "libpng/pngwrite.c", "libpng/pngwtran.c", "libpng/pngwutil.c",
        "libpng/LICENSE.md"
    }
    includedirs { "libpng", "zlib" }
    defines { "PNG_STATIC", "PNG_ARM_NEON_OPT=0", "Z_PREFIX" }
    links { "zlib" }

project "libjpegTurbo"
    kind "StaticLib"
    language "C"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "libjpeg-turbo/*.h", "libjpeg-turbo/wrapper/j*.c",
        "libjpeg-turbo/jaricom.c", "libjpeg-turbo/jcarith.c", "libjpeg-turbo/jdarith.c",
        "libjpeg-turbo/jcapimin.c", "libjpeg-turbo/jchuff.c", "libjpeg-turbo/jcicc.c", "libjpeg-turbo/jcinit.c",
        "libjpeg-turbo/jclhuff.c", "libjpeg-turbo/jcmarker.c", "libjpeg-turbo/jcmaster.c", "libjpeg-turbo/jcomapi.c",
        "libjpeg-turbo/jcparam.c", "libjpeg-turbo/jcphuff.c", "libjpeg-turbo/jctrans.c",
        "libjpeg-turbo/jdapimin.c", "libjpeg-turbo/jdatadst.c", "libjpeg-turbo/jdatasrc.c", "libjpeg-turbo/jdhuff.c",
        "libjpeg-turbo/jdicc.c", "libjpeg-turbo/jdinput.c", "libjpeg-turbo/jdlhuff.c", "libjpeg-turbo/jdmarker.c",
        "libjpeg-turbo/jdmaster.c", "libjpeg-turbo/jdphuff.c", "libjpeg-turbo/jdtrans.c", "libjpeg-turbo/jerror.c",
        "libjpeg-turbo/jfdctflt.c", "libjpeg-turbo/jmemmgr.c", "libjpeg-turbo/jmemnobs.c",
        "libjpeg-turbo/jpeg_nbits.c", "libjpeg-turbo/LICENSE.md"
    }
    includedirs { "libjpeg-turbo" }
