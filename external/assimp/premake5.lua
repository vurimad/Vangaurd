group "ThirdParty"

local disabledImporters = {
    "3D", "3DS", "3MF", "AC", "AMF", "ASE", "ASSBIN", "B3D", "BLEND", "BVH", "C4D", "COB",
    "COLLADA", "CSM", "DXF", "HMP", "IFC", "IQM", "IRR", "IRRMESH", "LWO", "LWS", "M3D",
    "MD2", "MD3", "MD5", "MDC", "MDL", "MMD", "MS3D", "NDO", "NFF", "OFF", "OGRE",
    "OPENGEX", "PLY", "Q3BSP", "Q3D", "RAW", "SIB", "SMD", "STL", "TERRAGEN", "USD", "X",
    "X3D", "XGL"
}

local disabledImporterDefines = {}
for _, importer in ipairs(disabledImporters) do
    table.insert(disabledImporterDefines, "ASSIMP_BUILD_NO_" .. importer .. "_IMPORTER")
end

project "assimp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    exceptionhandling "On"
    rtti "On"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)

    includedirs {
        ".",
        "include",
        "code",
        "contrib",
        "contrib/rapidjson/include",
        "contrib/utf8cpp/source",
        "contrib/pugixml/src",
        "contrib/unzip",
        "contrib/zlib",
        "contrib/poly2tri"
    }

    defines {
        "ASSIMP_STATIC",
        "ASSIMP_BUILD_NO_EXPORT",
        "RAPIDJSON_HAS_STDSTRING=1",
        "RAPIDJSON_NOMEMBERITERATORCLASS",
        "_CRT_SECURE_NO_WARNINGS",
        "_CRT_NONSTDC_NO_DEPRECATE",
        "OPENDDL_STATIC_LIBARY",
        "P2T_STATIC_EXPORTS"
    }
    defines(disabledImporterDefines)

    files {
        "include/assimp/**.h",
        "include/assimp/**.hpp",
        "include/assimp/**.inl",
        "code/Common/**.h",
        "code/Common/**.cpp",
        "code/CApi/CInterfaceIOWrapper.h",
        "code/CApi/CInterfaceIOWrapper.cpp",
        "code/Geometry/**.h",
        "code/Geometry/**.cpp",
        "code/Material/**.h",
        "code/Material/**.cpp",
        "code/PostProcessing/**.h",
        "code/PostProcessing/**.cpp",
        "code/AssetLib/FBX/**.h",
        "code/AssetLib/FBX/**.cpp",
        "code/AssetLib/Obj/**.h",
        "code/AssetLib/Obj/**.cpp",
        "code/AssetLib/glTF/**.h",
        "code/AssetLib/glTF/**.inl",
        "code/AssetLib/glTF/**.cpp",
        "code/AssetLib/glTF2/**.h",
        "code/AssetLib/glTF2/**.inl",
        "code/AssetLib/glTF2/**.cpp",
        "code/AssetLib/glTFCommon/**.h",
        "code/AssetLib/glTFCommon/**.cpp",
        "contrib/zlib/**.h",
        "contrib/zlib/*.c",
        "contrib/unzip/**.h",
        "contrib/unzip/*.c",
        "contrib/rapidjson/include/**.h",
        "contrib/utf8cpp/source/**.h",
        "contrib/pugixml/src/**.hpp",
        "contrib/pugixml/src/pugixml.cpp",
        "contrib/clipper/**.hpp",
        "contrib/clipper/clipper.cpp",
        "contrib/earcut-hpp/**.hpp",
        "contrib/poly2tri/**.h",
        "contrib/poly2tri/**.cc",
        "contrib/stb/stb_image.h",
        "vanguard/**.hpp",
        "vanguard/**.cpp",
        "LICENSE",
        "UPSTREAM.md"
    }

    removefiles {
        "code/Common/Exporter.cpp",
        "code/AssetLib/FBX/FBXExport*.cpp",
        "code/AssetLib/Obj/ObjExporter.cpp",
        "code/AssetLib/glTF/glTFExporter.cpp",
        "code/AssetLib/glTF2/glTF2Exporter.cpp"
    }

    filter "system:windows"
        defines { "WIN32", "_WINDOWS" }
    filter {}

    vpaths {
        ["Public API/*"] = { "include/assimp/**.h", "include/assimp/**.hpp", "include/assimp/**.inl" },
        ["Source/*"] = { "code/**.h", "code/**.cpp" },
        ["Contrib/*"] = { "contrib/**.h", "contrib/**.hpp", "contrib/**.c", "contrib/**.cc", "contrib/**.cpp" },
        ["Vanguard Integration/*"] = { "vanguard/**.hpp", "vanguard/**.cpp" },
        ["Documentation"] = { "LICENSE", "UPSTREAM.md" }
    }

group "Tests/Third Party"

project "assimpSmoke"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "On"
    rtti "On"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/assimp_smoke.cpp" }
    includedirs { "include" }
    defines { "ASSIMP_STATIC" }
    links { "assimp" }
    dependson { "assimp" }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
