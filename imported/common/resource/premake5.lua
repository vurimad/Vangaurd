---
-- Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
---

-- globals resource

resource_location = '../../../temp/' .. _ACTION .. '/common/resource/'

-- files resource

resource_files = {
	["api"] = {
		"include/resourceApi.h",
		"include/resourcePublic.h",
		"src/resourceInternal.h",
		"src/resourceInit.cpp",
		"src/build.h",
		"src/build.cpp",
	},
	["depot"] = {
		"include/archiveLanguageMap.h",
		"include/backendResourceDepot.h",
		"include/directoryListingCache.h",
		"include/fileSyncResourceEditorDepot.h",
		"include/fileSystemResourceDepot.h",
		"include/resourceDepotMappingConfig.h",
		"include/resourceDepotUtils.h",
		"include/resourceEditorDepotImpl.h",
		"include/resourceGameDepot.h",
		"include/resourceSaver.h",
		"include/serializationAsyncSourceImpl.h",
		"include/singleTreeResourceDepot.h",
		"src/archiveHelperDurango.cpp",
		"src/archiveHelperDurango.h",
		"src/archiveHelperOrbis.cpp",
		"src/archiveHelperOrbis.h",
		"src/archiveLanguageMap.cpp",
		"src/backendResourceDepot.cpp",
		"src/directoryListingCache.cpp",
		"src/fileSyncResourceEditorDepot.cpp",
		"src/fileSystemResourceDepot.cpp",
		"src/resourceDepotMappingConfig.cpp",
		"src/resourceDepotUtils.cpp",
		"src/resourceEditorDepotImpl.cpp",
		"src/resourceGameDepot.cpp",
		"src/resourceSaver.cpp",	
		"src/serializationAsyncSourceImpl.cpp",
		"src/singleTreeResourceDepot.cpp",
	},
	
	-- XML - eXtensible Markup Language

	["resources/xml"] = {
		"include/xmlDocument.h",
		"include/xmlDocumentUtils.h",
		"include/xmlNewReader.h",
		"include/xmlNewWriter.h",
		"include/xmlResource.h",
		"src/xmlDocument.cpp",
		"src/xmlDocumentUtils.cpp",
		"src/xmlErrorHandler.cpp",
		"src/xmlNewReader.cpp",
		"src/xmlNewWriter.cpp",
		"src/xmlResource.cpp",
	},
	-- RapidXML is used by xmlNew* classes
	["resources/xml/rapidxml"] = {
		"src/rapidXml.h",
		"src/rapidxml/rapidxml.hpp",
		"src/rapidxml/rapidxml_iterators.hpp",
		"src/rapidxml/rapidxml_print.hpp",
		"src/rapidxml/rapidxml_utils.hpp",
	},
	
	-- JSON
	
	["json/wrappers"] = {
		"include/jsonAllocator.h",
		"include/jsonDataBuilder.h",
		"include/jsonFileHelper.h",
		"src/jsonAllocator.cpp",
		"src/jsonDataBuilder.cpp",
	},
	["json/serialization"] = {
		"include/jsonFormatResourceLoader.h",
		"include/jsonGenericLoader.h",
		"include/jsonParser.h",
		"include/jsonResource.h",
		"include/jsonSerializer.h",
		"include/textFormatResourceLoader.h",
		"src/jsonParser.cpp",
		"src/jsonResource.cpp",
		"src/jsonSerializer.cpp",
	},
	["json"] = 
	{
		"include/rapidjson.h",
	},
	
	-- TOML - Tom's Obvious, Minimal Language
	["toml"] = 
	{
		"include/toml.h",
		"include/tomlValues.h",
		"src/toml.hpp",
		"src/toml.cpp",
		"src/tomlValues.cpp"
	},

	-- MISC
	
	["resources/text"] = {
		"include/basicTextBuilder.h",
		"src/basicTextBuilder.cpp",
	},

	["resourceMetrics"] = {
		"include/resourceMetricsProcessor.h",
		"src/resourceMetricsProcessor.cpp"
	},

	-- CSV - Comma Separated Values

	["resources/csv"] = {
		"include/2darray.h",
		"include/2darraySaver.h",
		"src/2darray.cpp",
		"src/2darraySaver.cpp",
	},
	["misc"] = {
		"include/fileHelperLoader.h",
		"src/fileHelperLoader.cpp",
		"include/censorshipSwapList.h",
		"src/censorshipSwapList.cpp",
	},

	-- Bink

	["resources/bink"] = {
		"include/binkResource.h",
		"src/binkResource.cpp",
	}

}

-- configs resource

project "resource"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"

	location(resource_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs	{ "include", "src" }

	uses {
		"archive",
		"redCompression",
		"redReflection",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(resource_files)

	files(seq(resource_files))
	
	-- WinRT options
	filter "platforms:Durango"
		flags "NoMinimalRebuild" -- /ZW and MinimalRebuild" are not cpmpatibile
		buildoptions { "/ZW", "/EHsc" }
