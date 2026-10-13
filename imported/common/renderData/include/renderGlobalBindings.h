#pragma once

// ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO

// !!! THIS MUST BE KEPT IN SYNC WITH globalBindings.fx !!!!

// ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO ##YOLO


namespace rend
{
	namespace Binding
	{

		//****************************************************************************************//
		// SAMPLERS					Slots 8+
		//****************************************************************************************//
		namespace Samplers
		{	
			static constexpr	Uint32		MAX_CUSTOM					= 7;
		
			static constexpr	Uint32		SAMPLER_POINT				= 8;
			static constexpr	Uint32		SAMPLER_SHADOW_CMP			= 9;
			static constexpr	Uint32		SAMPLER_LINEAR				= 10;
			static constexpr	Uint32		SAMPLER_LINEAR_CLAMP		= 11;
			static constexpr	Uint32		SAMPLER_ENV_PROBE			= 12;
		}

		//****************************************************************************************//
		// TEXTURES					Slots 11+
		//****************************************************************************************//

		namespace Textures
		{	
			static constexpr	Uint32		MAX_CUSTOM					= 10;

			static constexpr	Uint32		SCENE_COLOR					= 11;
			static constexpr	Uint32		SCENE_DEPTH					= 12;
			static constexpr	Uint32		SCENE_NORMALS				= 13;

			static constexpr	Uint32		LIGHTS_ROUGH_GRID			= 13;
			static constexpr	Uint32		LIGHTS_DATA					= 14;
			static constexpr	Uint32		AMBIENT_BRDF				= 15;

			static constexpr	Uint32		FRAME_EXPOSURE				= 16;
			static constexpr	Uint32		FOG_SCATTERING				= 17;
			static constexpr	Uint32		GLOBAL_SHADOW_MAP			= 18;
			static constexpr	Uint32		LOCAL_SHADOWS				= 19;
			static constexpr	Uint32		GLOBAL_SHADOW_MASK			= 20;
			static constexpr	Uint32		DISTANT_SHADOWS				= 21;
			static constexpr	Uint32		SKY_IRRADIANCE				= 22;
			static constexpr	Uint32		ENV_REFLECTION				= 23;
			static constexpr	Uint32		ENV_INDICES					= 24;
			static constexpr	Uint32		IRRADIANCE_VOLUME			= 25;
			static constexpr	Uint32		DECOUPLED_PARTICLE_LIGHTING = 26;
			static constexpr	Uint32		RAIN_MAP					= 27;
			static constexpr	Uint32		HOLOGRAM_DEPTH				= 28;
			static constexpr	Uint32		GBUFFER2					= 29;
			static constexpr	Uint32		SCENE_STENCIL				= 30;
			static constexpr	Uint32		IES_PROFILES				= 31;
			static constexpr	Uint32		WIND_IMPULSE				= 32;
			static constexpr	Uint32		INTERIOR_MAP				= 33;
			static constexpr	Uint32		GRADIENT_MAP				= 34;
			static constexpr	Uint32		TRANSPARENT_REFLECTION		= 35;
			static constexpr	Uint32		TOP_DOWN_CAR_MAP			= 36;
			static constexpr	Uint32		RAIN_RIPPLES				= 37;
			static constexpr	Uint32		RAIN_LEAKS					= 38;
			static constexpr	Uint32		RAIN_DROPLETS				= 39;
			static constexpr	Uint32		WORLD_IRRADIANCE			= 40;
			static constexpr	Uint32		TERRAIN_HEIGHTMAP			= 41;
			static constexpr	Uint32		TOP_DOWN_RAIN_DEPTH			= 42;
			static constexpr	Uint32		CLOUD_SHADOW				= 43;
			static constexpr	Uint32		GLOBAL_ILLUMINATION_FWD		= 44;
			// Decal data buffer is at slot 45
			static constexpr	Uint32		BLUE_NOISE_ARRAY			= 46;
			static constexpr	Uint32		SKY_MODEL					= 47;
			static constexpr	Uint32		TOP_DOWN_CAR_BACK_FACE_MAP	= 48;
			static constexpr	Uint32		CAMERA_PERSISTENT_DATA		= 49;

			// deferred light list for forward materials (some utilize it to match properly with contact shadows)
			static constexpr	Uint32		LIGHTS_LIST_HEADS			= 50;
			static constexpr	Uint32		LIGHTS_LIST					= 51;
			static constexpr	Uint32		LIGHT_CHANNELS				= 52;

			// Begin/end of the forward shader globals. Not an actual binding, just marking the begin and end (one past the last) of the global bindings.
			static constexpr	Uint32		GLOBAL_FWD_BEGIN			= 15;
			static constexpr	Uint32		GLOBAL_FWD_END				= 29;

			// normal vectors feedback (for deferred decals)
			static constexpr	Uint32		GBUFFER1_COPY				= 53;
			static constexpr	Uint32		HAIR_PROFILES				= 54;

			// #NOTE: These can go up to 54. 55+ are used for material modifiers.
		}

		//****************************************************************************************//
		// BUFFERS					Slots 16+
		//****************************************************************************************//

		namespace Buffers
		{	
			static constexpr	Uint32		PREV_SKINNED_POSITIONS		= 9;
			static constexpr	Uint32		MAX_CUSTOM					= 15;
		
			static constexpr	Uint32		FORW_TILE_LIGHTS			= 20;
		}

		//****************************************************************************************//
		// CONSTANTS
		//****************************************************************************************//

		namespace Constants
		{
			static constexpr	Uint32		GLOBAL						= 0;
			static constexpr	Uint32		CAMERA						= 1;
			static constexpr	Uint32		CUSTOM						= 2;

			static constexpr	Uint32		GPUAPI_VS_LEGACY_CUSTOM		= 3;
			static constexpr	Uint32		GPUAPI_PS_LEGACY_CUSTOM		= 3;

			static constexpr	Uint32		MATERIAL_PARAMETERS			= 4;

			static constexpr	Uint32		GPUAPI_VS_FREQUENT			= 5;

			static constexpr 	Uint32 		GPUAPI_SHARED_CB			= 6;
			
			static constexpr	Uint32		MATERIAL_MODIFIERS			= 7;
			
			/******************************************************************/
			// CB Slots <8, 10> reserved for material modifiers
			/******************************************************************/

			static constexpr 	Uint32 		SHARED_CONSTANT 			= 12;
			static constexpr 	Uint32 		SHARED_COMPUTE				= 13;


			static constexpr	Uint32		SHARED_MAX_SIZE				= 1024;
		}

		//****************************************************************************************//
		//****************************************************************************************//

	}
	
}
