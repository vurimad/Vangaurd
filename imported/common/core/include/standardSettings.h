/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once


/////////////////////////////////////////////////////////////////////////////
// Global Configuration
/////////////////////////////////////////////////////////////////////////////

//#define NO_ASYNCHRONOUS_MATERIALS	// Disable asynchronous material compilation

/////////////////////////////////////////////////////////////////////////////
// Per Platform Configuration
/////////////////////////////////////////////////////////////////////////////

#if defined( RED_PLATFORM_CONSOLE ) || defined( RED_PLATFORM_LINUX )
	#define NO_HEIGHTMAP_EDIT	
	#define NO_CUBEMAP_GENERATION
	#define NO_TEXTURE_EDITING
	#define NO_RUNTIME_MATERIAL_COMPILATION
#endif

#if defined( RED_CONFIGURATION_FINAL ) && defined( RED_PLATFORM_WINPC )
	#define NO_HEIGHTMAP_EDIT	
	#define NO_CUBEMAP_GENERATION
	#define NO_TEXTURE_EDITING
#endif

#if defined( RED_CONFIGURATION_FINAL )
	#define NO_DEBUG_PAGES
#endif


/////////////////////////////////////////////////////////////////////////////
// Per Target Configuration
/////////////////////////////////////////////////////////////////////////////

#ifdef RED_CONFIGURATION_FINAL

// ctremblay: fragment legacy macro. This cannot be define to compile fragment....
	#define NO_DEBUG_FRAGMENTS	
	#define NO_DATA_ASSERTS 					// No Data Asserts
	#define NO_DEBUG_PAGES						// Disable debug pages
	#define NO_RUNTIME_MATERIAL_COMPILATION		// Disable material compilation
	#define NO_ASYNCHRONOUS_MATERIALS			// Disable spawning jobs to collect materials
	#define NO_ERROR_STATE						// Disable error state reporting
	#define NO_OBSTACLE_MESH_DATA				// For dynamic generation of static mesh obstacles
	#define NO_CUBEMAP_GENERATION		
	#define NO_TEXTURECACHE_COOKER	
	#define NO_TEXTURE_EDITING					// required for cubemap building right now. Disable only in final.

#if !defined( USE_PROFILER )
	#define NO_DEBUG_CONSOLE
#endif

#endif

/////////////////////////////////////////////////////////////////////////////
// No Editor Configuration
/////////////////////////////////////////////////////////////////////////////

#ifdef NO_EDITOR

	#define NO_RESOURCE_IMPORT					
	#define NO_HEIGHTMAP_EDIT		
	#define NO_COMPONENT_GRAPH				
	#define NO_DATA_VALIDATION
	#define NO_EDITOR_GRAPH_SUPPORT
	#define NO_EDITOR_STEERING_SUPPORT
	#define NO_EDITOR_PROPERTY_SUPPORT
	#define NO_RESOURCE_COOKING	

#endif
