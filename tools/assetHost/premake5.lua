local importedRoot = "../../imported"
local commonRoot = path.join(importedRoot, "common")
local archiveRoot = path.join(commonRoot, "archive")
local coreRoot = path.join(commonRoot, "core")
local redImageRoot = path.join(commonRoot, "redImage")
local renderBackendRoot = path.join(commonRoot, "renderBackend")
local renderDataRoot = path.join(commonRoot, "renderData")
local resourceMaterialRoot = path.join(commonRoot, "resourceMaterial")
local resourceMeshRoot = path.join(commonRoot, "resourceMesh")
local engineRoot = path.join(commonRoot, "engine")
local backendDataRoot = path.join(importedRoot, "backend/backendData")
local backendEngineRoot = path.join(importedRoot, "backend/backendEngine")
local backendMaterialRoot = path.join(importedRoot, "backend/backendMaterial")
local backendMeshRoot = path.join(importedRoot, "backend/backendMesh")
local backendResourcesRoot = path.join(importedRoot, "backend/backendResources")
local assetsRoot = "../../source/assets"
local assetStreamingRoot = "../../source/assetStreaming"
local directXTexRoot = path.getabsolute("../../imported/external/DirectXTex")
local fbxSdkRoot = path.getabsolute("../../imported/external/fbxsdk")
local nvToolsRoot = path.getabsolute("../../imported/external/nvToolsExt")
local oodleRoot = path.getabsolute("../../imported/external/oodle")

group "Tools/Assets"

project "VanguardAssetHost"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    characterset "Unicode"
    warnings "Default"
    targetdir(tools_output_root)
    objdir(object_root)
    debugdir(tools_output_root)

    defines {
        "RED_COMPILER_MSC",
        "RED_VANGUARD_DISABLE_VTUNE",
        "VANGUARD_ASSET_HOST",
        "RED_MODULE_redCrypto",
        "RED_MODULE_archive",
        "RED_MODULE_core",
        "RED_MODULE_redImage",
        "RED_MODULE_renderData",
        "RED_MODULE_renderBackend",
        "RED_MODULE_resourceMaterial",
        "RED_MODULE_resourceMesh",
        "RED_MODULE_backendMaterial",
        "RED_MODULE_backendMesh",
        "RED_MODULE_backendResources",
        "NO_CONSOLE_SDK",
        "NO_DATA_VALIDATION",
        "NO_EDITOR",
        "NO_RUNTIME_MATERIAL_COMPILATION",
        "_HAS_EXCEPTIONS=0",
        "_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING",
        "_SILENCE_CXX20_IS_POD_DEPRECATION_WARNING"
    }

    filter "configurations:Debug"
        defines { "RED_CONFIGURATION_DEBUG" }
    filter "configurations:Development"
        defines { "RED_CONFIGURATION_RELEASE" }
    filter "configurations:Profile"
        defines { "RED_CONFIGURATION_RELEASE" }
    filter "configurations:Shipping"
        defines { "RED_CONFIGURATION_FINAL" }
    filter {}

    includedirs {
        "src",
        "private",
        "../../source/system/include",
        "../../source/memory/include",
        "../../source/diagnostics/include",
        "../../source/containers/include",
        "../../source/concurrency/include",
        "../../source/io/include",
        "../../source/filesystem/include",
        "../../source/jobs/include",
        "../../source/reflection/include",
        "../../source/assets/include",
        path.join(assetStreamingRoot, "private"),
        "../../source/crypto/include",
        path.join(commonRoot, "redSystem/include"),
        path.join(commonRoot, "redMemory/include"),
        path.join(commonRoot, "redMath/include"),
        path.join(commonRoot, "redContainers/include"),
        path.join(commonRoot, "redIO/include"),
        path.join(commonRoot, "redCore/include"),
        path.join(commonRoot, "redFileSystem/include"),
        path.join(commonRoot, "redJobs2/include"),
        path.join(commonRoot, "redReflection/include"),
        path.join(commonRoot, "redConfig/include"),
        path.join(commonRoot, "redNetwork/include"),
        path.join(commonRoot, "commProtocol/gen"),
        path.join(commonRoot, "redCrypto/include"),
        path.join(commonRoot, "resource/include"),
        path.join(commonRoot, "resource/src"),
        path.join(archiveRoot, "include"),
        path.join(coreRoot, "include"),
        path.join(redImageRoot, "include"),
        path.join(redImageRoot, "src"),
        path.join(renderBackendRoot, "include"),
        path.join(renderBackendRoot, "src"),
        path.join(renderDataRoot, "include"),
        path.join(renderDataRoot, "src"),
        path.join(resourceMaterialRoot, "include"),
        path.join(resourceMaterialRoot, "src"),
        path.join(resourceMeshRoot, "include"),
        path.join(resourceMeshRoot, "src"),
        path.join(backendDataRoot, "include"),
        path.join(backendDataRoot, "src"),
        path.join(backendEngineRoot, "include"),
        path.join(backendMaterialRoot, "include"),
        path.join(backendMaterialRoot, "src"),
        path.join(backendMeshRoot, "include"),
        path.join(backendMeshRoot, "src"),
        path.join(backendResourcesRoot, "include"),
        path.join(backendResourcesRoot, "src"),
        path.join(directXTexRoot, "include"),
        path.join(fbxSdkRoot, "include")
    }

    files {
        "src/**.cpp",
        "private/**.hpp",
        path.join(commonRoot, "resource/src/2darray.cpp"),
        path.join(commonRoot, "resource/src/2darraySaver.cpp"),
        path.join(commonRoot, "resource/src/backendResourceDepot.cpp"),
        path.join(commonRoot, "resource/src/directoryListingCache.cpp"),
        path.join(commonRoot, "resource/src/fileHelperLoader.cpp"),
        path.join(commonRoot, "resource/src/fileSystemResourceDepot.cpp"),
        path.join(commonRoot, "resource/src/resourceDepotMappingConfig.cpp"),
        path.join(commonRoot, "resource/src/resourceEditorDepotImpl.cpp"),
        path.join(commonRoot, "resource/src/resourceSaver.cpp"),
        path.join(commonRoot, "resource/src/serializationAsyncSourceImpl.cpp"),
        path.join(commonRoot, "resource/src/singleTreeResourceDepot.cpp"),
        path.join(commonRoot, "resource/src/xmlErrorHandler.cpp"),
        path.join(archiveRoot, "src/fileId.cpp"),
        path.join(archiveRoot, "src/fileIOImpl.cpp"),
        path.join(archiveRoot, "src/fileMetadata.cpp"),
        path.join(archiveRoot, "src/fileTable.cpp"),
        path.join(archiveRoot, "src/instance.cpp"),
        path.join(archiveRoot, "src/inspector.cpp"),
        path.join(archiveRoot, "src/utils.cpp"),
        path.join(coreRoot, "src/tagList.cpp"),
        path.join(commonRoot, "redCrypto/src/sha1.cpp"),
        path.join(commonRoot, "redCrypto/src/sha1prv.cpp"),
        path.join(redImageRoot, "src/image.cpp"),
        path.join(redImageRoot, "src/imageAccess.cpp"),
        path.join(redImageRoot, "src/imageFormats.cpp"),
        path.join(redImageRoot, "src/imageLoader.cpp"),
        path.join(redImageRoot, "src/imageLoaderPNG.cpp"),
        path.join(redImageRoot, "src/imageMemory.cpp"),
        path.join(redImageRoot, "src/imageSaver.cpp"),
        path.join(redImageRoot, "src/imageSaverPNG.cpp"),
        path.join(redImageRoot, "src/imageUtilsBoxDownscale.cpp"),
        path.join(redImageRoot, "src/imageUtilsChannels.cpp"),
        path.join(redImageRoot, "src/imageUtilsFlip.cpp"),
        path.join(redImageRoot, "src/imageUtilsGuassDownscale.cpp"),
        path.join(redImageRoot, "src/imageUtilsHistogramProcessor.cpp"),
        path.join(redImageRoot, "src/imageUtilsPacking.cpp"),
        path.join(redImageRoot, "src/imageUtilsResampleBicubic.cpp"),
        path.join(redImageRoot, "src/png/png.c"),
        path.join(redImageRoot, "src/png/pngerror.c"),
        path.join(redImageRoot, "src/png/pngget.c"),
        path.join(redImageRoot, "src/png/pngmem.c"),
        path.join(redImageRoot, "src/png/pngpread.c"),
        path.join(redImageRoot, "src/png/pngread.c"),
        path.join(redImageRoot, "src/png/pngrio.c"),
        path.join(redImageRoot, "src/png/pngrtran.c"),
        path.join(redImageRoot, "src/png/pngrutil.c"),
        path.join(redImageRoot, "src/png/pngset.c"),
        path.join(redImageRoot, "src/png/pngtrans.c"),
        path.join(redImageRoot, "src/png/pngwio.c"),
        path.join(redImageRoot, "src/png/pngwrite.c"),
        path.join(redImageRoot, "src/png/pngwtran.c"),
        path.join(redImageRoot, "src/png/pngwutil.c"),
        path.join(renderDataRoot, "src/imageProcessing.cpp"),
        path.join(renderDataRoot, "src/materialFeatureFlags.cpp"),
        path.join(renderDataRoot, "src/materialStructures.cpp"),
        path.join(renderDataRoot, "src/renderObject.cpp"),
        path.join(renderDataRoot, "src/pipelineStateObject.cpp"),
        path.join(renderDataRoot, "src/renderPublicEnums.cpp"),
        path.join(renderDataRoot, "src/renderResource.cpp"),
        path.join(renderDataRoot, "src/renderResourceBlob.cpp"),
        path.join(renderDataRoot, "src/renderMeshBlob.cpp"),
        path.join(renderDataRoot, "src/gpuWrapApiVertexPacking.cpp"),
        path.join(renderDataRoot, "src/vertexLayoutDesc.cpp"),
        path.join(renderDataRoot, "src/renderSimpleTypes.cpp"),
        path.join(renderDataRoot, "src/renderTextureBlob.cpp"),
        path.join(renderDataRoot, "src/renderingMemory.cpp"),
        path.join(renderDataRoot, "src/meshEnum.cpp"),
        path.join(renderDataRoot, "src/textureEnums.cpp"),
        path.join(renderDataRoot, "src/textureGroup.cpp"),
        path.join(renderBackendRoot, "src/meshPackingSource.cpp"),
        path.join(renderBackendRoot, "src/renderMeshCompiler.cpp"),
        path.join(renderBackendRoot, "src/renderTextureCompiler.cpp"),
        path.join(resourceMaterialRoot, "src/bitmapDataBuffer.cpp"),
        path.join(resourceMaterialRoot, "src/bitmapTexture.cpp"),
        path.join(resourceMaterialRoot, "src/material.cpp"),
        path.join(resourceMaterialRoot, "src/materialDefinition.cpp"),
        path.join(resourceMaterialRoot, "src/materialInstance.cpp"),
        path.join(resourceMaterialRoot, "src/materialMetaDefinition.cpp"),
        path.join(resourceMaterialRoot, "src/materialParameter.cpp"),
        path.join(resourceMaterialRoot, "src/materialParameterInstance.cpp"),
        path.join(resourceMaterialRoot, "src/materialTemplate.cpp"),
        path.join(resourceMaterialRoot, "src/sourceTextureData.cpp"),
        path.join(resourceMaterialRoot, "src/texture.cpp"),
        path.join(commonRoot, "core/src/memoryBufferFile.cpp"),
        path.join(commonRoot, "resource/src/xmlDocument.cpp"),
        path.join(resourceMeshRoot, "src/mesh.cpp"),
        path.join(resourceMeshRoot, "src/meshAppearance.cpp"),
        path.join(resourceMeshRoot, "src/meshChunk.cpp"),
        path.join(resourceMeshRoot, "src/meshDataViewRO.cpp"),
        path.join(resourceMeshRoot, "src/meshMaterialBuffer.cpp"),
        path.join(resourceMeshRoot, "src/meshMaterialBank.cpp"),
        path.join(resourceMeshRoot, "src/meshPacker.cpp"),
        path.join(resourceMeshRoot, "src/meshParamGpuBuffer.cpp"),
        path.join(resourceMeshRoot, "src/meshParameter.cpp"),
        path.join(resourceMeshRoot, "src/meshUtils.cpp"),
        path.join(resourceMeshRoot, "src/meshVertex.cpp"),
        path.join(engineRoot, "src/renderMeshBufferWriter.cpp"),
        path.join(backendEngineRoot, "src/importer.cpp"),
        path.join(backendEngineRoot, "src/resourceImportMetadata.cpp"),
        path.join(backendEngineRoot, "src/resourceMetadata.cpp"),
        path.join(backendEngineRoot, "src/resourceOptions.cpp"),
        path.join(backendMaterialRoot, "src/bitmapTextureCompilationSource.cpp"),
        path.join(backendMaterialRoot, "src/bitmapTextureCooker.cpp"),
        path.join(backendMaterialRoot, "src/importBitmapTexture.cpp"),
        path.join(backendMaterialRoot, "src/materialCooking.cpp"),
        path.join(backendMaterialRoot, "src/textureCooking.cpp"),
        path.join(backendMeshRoot, "src/meshCompilationSource.cpp"),
        path.join(backendMeshRoot, "src/meshCooking.cpp"),
        path.join(backendMeshRoot, "src/meshDataBuilder.cpp"),
        path.join(backendMeshRoot, "src/meshDataView.cpp"),
        path.join(backendMeshRoot, "src/meshOptimizer.cpp"),
        path.join(backendMeshRoot, "src/meshPacking.cpp"),
        path.join(backendMeshRoot, "src/meshRenderBlobUtils.cpp"),
        path.join(backendResourcesRoot, "src/fbxCommon.cpp"),
        path.join(backendResourcesRoot, "src/fbxMeshBuilder.cpp"),
        path.join(backendResourcesRoot, "src/fbxMeshImporter.cpp"),
        path.join(backendDataRoot, "src/cooker.cpp"),
        path.join(backendDataRoot, "src/cookerTask.cpp"),
        path.join(backendDataRoot, "src/dataContext.cpp"),
        path.join(backendDataRoot, "src/dataSystemContext.cpp"),
        path.join(backendDataRoot, "src/dataSystemInstance.cpp"),
        path.join(backendDataRoot, "src/dataTask.cpp"),
        path.join(backendDataRoot, "src/dataTaskContext.cpp"),
        path.join(backendDataRoot, "src/dataTaskSystem.cpp"),
        path.join(backendDataRoot, "src/dataErrorHandler.cpp"),
        path.join(backendDataRoot, "src/dataErrorHandlerImpl.cpp"),
        path.join(backendDataRoot, "src/dataTaskMetrics.cpp"),
        path.join(backendDataRoot, "src/dataTypes.cpp"),
        path.join(backendDataRoot, "src/generator.cpp"),
        path.join(backendDataRoot, "src/dependencyHash.cpp"),
        path.join(backendDataRoot, "src/dependencyUtils.cpp"),
        path.join(backendDataRoot, "src/dependencyCache.cpp"),
        path.join(backendDataRoot, "src/dependencyCollector.cpp"),
        path.join(backendDataRoot, "src/dependencyDatabase.cpp"),
        path.join(backendDataRoot, "src/dependencyOutputCollector.cpp"),
        path.join(backendDataRoot, "src/dependencyOutputDepotWrapper.cpp"),
        path.join(backendDataRoot, "src/dependencyTypes.cpp"),
        path.join(backendDataRoot, "src/dependencyTypesImpl.cpp"),
        path.join(backendDataRoot, "src/resourceDependency.cpp"),
        path.join(backendDataRoot, "src/dependencyBinaryDatabase.cpp"),
        path.join(backendDataRoot, "src/dependencyBinaryReader.cpp"),
        path.join(backendDataRoot, "src/dependencyBinaryWriter.cpp"),
        path.join(backendDataRoot, "src/fileSystemFilter.cpp"),
        path.join(backendDataRoot, "src/resourceDatabase.cpp"),
        path.join(assetsRoot, "src/asset_file_transaction.cpp"),
        path.join(assetStreamingRoot, "src/platform_directory_observer.cpp")
    }

    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }

    filter "configurations:Debug"
        libdirs {
            path.join(directXTexRoot, "lib/x64.Debug"),
            path.join(fbxSdkRoot, "lib/x64.Debug")
        }
    filter "configurations:not Debug"
        libdirs {
            path.join(directXTexRoot, "lib/x64.Release"),
            path.join(fbxSdkRoot, "lib/x64.Release")
        }
    filter {}

    links {
        "reflection",
        "reflectionCompat",
        "redReflectionCompat",
        "redConfigCompat",
        "commProtocolCompat",
        "commChannelCompat",
        "redLexerCompat",
        "redNetworkCompat",
        "crypto",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCompressionThirdPartyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "io",
        "ioCompat",
        "redIOCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "redContainersCompat",
        "redMathCompat",
        "concurrency",
        "concurrencyCompat",
        "memory",
        "redCoreCompat",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
        "DirectXTex_MT",
        "libfbxsdk",
        "windowscodecs",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Shell32",
        "Ole32",
        "User32",
        "Bcrypt",
        "Version",
        "ws2_32"
    }

    postbuildcommands {
        '{COPYFILE} "' .. path.join(nvToolsRoot, "lib/x64.Release/nvToolsExt64_1.dll") .. '" "%{cfg.targetdir}/nvToolsExt64_1.dll"'
    }
    filter "configurations:Debug"
        postbuildcommands {
            '{COPYFILE} "' .. path.join(fbxSdkRoot, "lib/x64.Debug/libfbxsdk.dll") .. '" "%{cfg.targetdir}/libfbxsdk.dll"',
            '{COPYFILE} "' .. path.join(oodleRoot, "lib/x64.Debug/oo2ext_7_win64_debug.dll") .. '" "%{cfg.targetdir}/oo2ext_7_win64_debug.dll"'
        }
    filter "configurations:not Debug"
        postbuildcommands {
            '{COPYFILE} "' .. path.join(fbxSdkRoot, "lib/x64.Release/libfbxsdk.dll") .. '" "%{cfg.targetdir}/libfbxsdk.dll"',
            '{COPYFILE} "' .. path.join(oodleRoot, "lib/x64.Release/oo2ext_7_win64.dll") .. '" "%{cfg.targetdir}/oo2ext_7_win64.dll"'
        }
    filter {}

    vpaths {
        ["Host/*"] = { "src/**.cpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Imported/RED Resource/*"] = {
            path.join(commonRoot, "resource/src/2darray.cpp"),
            path.join(commonRoot, "resource/src/2darraySaver.cpp"),
            path.join(commonRoot, "resource/src/resourceSaver.cpp")
        },
        ["Vanguard Durability/*"] = { path.join(assetsRoot, "src/asset_file_transaction.cpp") },
        ["Vanguard Source Observation/*"] = { path.join(assetStreamingRoot, "src/platform_directory_observer.cpp") },
        ["Imported/RED Crypto/*"] = {
            path.join(commonRoot, "redCrypto/src/sha1.cpp"),
            path.join(commonRoot, "redCrypto/src/sha1prv.cpp")
        },
        ["Imported/RED backendData/*"] = { path.join(backendDataRoot, "src/**.cpp") }
    }
