/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/****************************/
/* Version list				*/
/****************************/

// Restructure depot
#define VER_DEPOT_MAPPING_RESTRUCTURE 172

// Fixes for entities with a restructured depot
#define VER_DEPOT_MAPPING_INTERNAL_ENTITY_FIX 173

// Fixes for mesh material templates getting serialized inside mesh files
#define VER_DEPOT_MESH_MATERIAL_HANDLES_FIX 174

// Fix for faulty custom serialization of navigation nodes.
#define VER_NAVIGATION_NODE_RTTI_SERIALIZATION_FIX 175

// Fix for Font using RTTI instead of custom serialization
#define VER_FONT_RTTI_SUPPORT 176

#define VER_FIX_ENV_PROBES_SERIALIZATION_HACK 177

#define VER_TEXTURES_MESHES_UVFLIP_GLOBAL 178

#define VER_TRAFFIC_CONNECTIVITY_PROBABILITY_FIX 179

#define VER_TRAFFIC_CONNECTIVITY_LANE_UID_FIX 180

// TrafficCompiledNode started using bulk serialization for arrays in order to reduce resource size
#define VER_TRAFFIC_COMPILED_NODE_BULK_SERIALIZATION 181

#define VER_ENTITY_NEW_DATA_FORMAT 182

#define VER_MESHNODE_NEW_DATA_FORMAT 183

// New data included in TrafficLanes serialized as part of TrafficCompiledNode.
#define VER_TRAFFIC_COMPILED_NODE_LANE_SPOTS 184

// Resave of Persistent Data in Prefabs
#define VER_PREFAB_PERSISTENT_DATA 186

// New data format for mesh external materials
#define VER_MESH_EXTERNAL_MATERIALS_NEW_DATA_FORMAT 187

// New data format for mesh local materials
#define VER_MESH_LOCAL_MATERIALS_NEW_DATA_FORMAT 188

// Traffic lanes proximity grid
#define VER_TRAFFIC_LANES_GRID 189

#define VER_SPOT_LIGHT_BRIGHTNESS 190

// Traffic mid entry/exit points
#define VER_TRAFFIC_ENTRY_EXIT 191

// Added custom versioning for traffic resources
#define VER_TRAFFIC_CUSTOM_VERSIONING 192

// TweakDB IDs start using Hash instead of String for serialization
#define VER_TWEAKDB_ID_HASH_SERIALIZATION 193

// Fox for PrefabNodes using the same objects internally
#define VER_PREFAB_VARIANTS_LISTS_FIX 194

// Remove strings from resource paths
#define VER_RESOURCEPATHS_WITHOUT_STRINGS 195

/****************************/
/* Current version			*/
/****************************/

#define VER_CURRENT		VER_RESOURCEPATHS_WITHOUT_STRINGS

/********************************/
/* Minimal supported version	*/
/********************************/
#define VER_MINIMAL		VER_DEPOT_MAPPING_RESTRUCTURE
