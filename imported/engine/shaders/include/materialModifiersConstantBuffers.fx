#ifndef INCLUDE_GUARD_MATERIAL_MODIFIERS_CONSTANT_BUFFERS_FX
#define INCLUDE_GUARD_MATERIAL_MODIFIERS_CONSTANT_BUFFERS_FX

/// Register
#ifdef __cplusplus
	#pragma once

	#ifndef REG
		#define REG( _name, _reg, _type )				const red::Uint32 _name = (_reg);
		#define REGI( _name, _reg, _type )				const red::Uint32 _name = (_reg);
		#define REGB( _name, _reg, _type )				const red::Uint32 _name = (_reg);
		#define REG_ARRAY( _name,_reg, _type, _size )	const red::Uint32 _name = (_reg);
		#define SYS_SAMPLER( _name,_reg )				const red::Uint32 _name = (_reg);
		#define SYS_TEXTURE_NO_SAMPLER( _name,_reg )	const red::Uint32 _name = (_reg);
		#define VERTEXSHADER 1
		#define PIXELSHADER 1
	#endif

	#ifndef START_CB
		#define START_CB( _name, _reg )		// EMPTY
		#define END_CB						// EMPTY
	#endif
#else
	#include "common.fx"
#endif

#define MATERIAL_MODIFIER_CONSTANTS_LAST_INDEX 26
START_CB( MaterialModifiersConsts, 7 )
	REG_ARRAY( MM_Constants, 0, float4, MATERIAL_MODIFIER_CONSTANTS_LAST_INDEX )
END_CB

#define MM_HitProxy							( 0 )
#define MM_WindData							( 1 )
#define MM_ParticleParams					( 2 )
#define MM_RemoteCamera						( 3 )
#define MM_Mirror							( 4 )
#define MM_CustomStructBuffer				( 5 )
#define MM_EffectParams 					( 6 )
#define MM_MotionMatrix						( 7 )
#define MM_ColorAndTexture					( 8 )
#define MM_MaterialParams					( 9 )
#define MM_Eye								( 10 )
#define MM_Skin								( 11 )
#define MM_VehicleParams                    ( 12 )
#define MM_Dismemberment					( 13 )
#define MM_Garments							( 14 )
#define MM_ShadowsDebugParams               ( 15 )
#define MM_MultilayeredDebug                ( 16 )
#define MM_ParallaxParams                   ( 17 )
#define MM_HighlightsParams                 ( 18 )
#define MM_DebugColoring                    ( 19 ) // TODO: should replace MM_MultilayeredDebug and MM_ShadowsDebugParams?
#define MM_DrawBufferMask					( 20 )
#define MM_AutoSpawnData					( 21 )
#define MM_DestructionRegions				( 22 )
#define MM_FloatTracks						( 23 )
#define MM_AutoHideDistance					( 24 )
#define MM_Rain								( 25 )
#define MM_PlanarReflections				( 26 )

// all shaders
#define MTL_DiscardFlags					0

#define BEGIN_ADDITIONAL_SUPPORTED_MATERIAL_MODIFIERS( setName )	static const int ADDITIONAL_MATERIAL_MODIFIERS_##setName = ( 0

#define BEGIN_SUPPORTED_MATERIAL_MODIFIERS							static const int SUPPORTED_MATERIAL_MODIFIERS = ( 0
#define DECLARE_SUPPORTED_MATERIAL_MODIFIER( materialModifer )		| ( 1 << materialModifer )

// Use DECLARE_ADDITIONAL_MATERIAL_MODIFIERS to add modifiers built by BEGIN_ADDITIONAL_SUPPORTED_MATERIAL_MODIFIERS
// into another material modifiers block.
#define DECLARE_ADDITIONAL_MATERIAL_MODIFIERS_( setName )			| ( ADDITIONAL_MATERIAL_MODIFIERS_##setName )
#define DECLARE_ADDITIONAL_MATERIAL_MODIFIERS( setName )			DECLARE_ADDITIONAL_MATERIAL_MODIFIERS_( setName )

#define END_SUPPORTED_MATERIAL_MODIFIERS							);

#define MATMOD_PREFIX( name )										MatMod_##name

#define DECLARE_MATERIAL_MODIFIER_CUSTOM_PARAM( paramName, paramReg )	static const int MATMOD_PREFIX( paramName ) = paramReg
#define DECLARE_MATERIAL_MODIFIER_PARAM( paramName ) 					static const int MATMOD_PREFIX( paramName ) = __VECTOR_REGISTER_VALUE_##paramName
#define DECLARE_MATERIAL_MODIFIER_MATRIX_PARAM( paramName ) 			static const int MATMOD_PREFIX( paramName ) = __MATRIX_REGISTER_VALUE_##paramName
#define DECLARE_MATERIAL_MODIFIER_TEXTURE_PARAM( paramName )			static const int MATMOD_PREFIX( paramName ) = __TEXTURE_REGISTER_VALUE_##paramName
#define DECLARE_MATERIAL_MODIFIER_CONSTANT_BUFFER_PARAM( paramName )	static const int MATMOD_PREFIX( paramName ) = __CONSTANT_BUFFER_REGISTER_VALUE_##paramName
#define DECLARE_MATERIAL_MODIFIER_STRUCT_BUFFER_PARAM( paramName )		static const int MATMOD_PREFIX( paramName ) = __STRUCT_BUFFER_REGISTER_VALUE_##paramName
#define DECLARE_MATERIAL_MODIFIER_BYTE_BUFFER_PARAM( paramName )		static const int MATMOD_PREFIX( paramName ) = __BYTE_BUFFER_REGISTER_VALUE_##paramName

#define GENERATE_LINKAGE_NAME( objectName, paramName )				MatMod_ParamLinkage___##MatMod_##paramName##___##objectName
#define DECLARE_OBJECT_PARAM_LINKAGE( objectName, paramName ) 		static const int GENERATE_LINKAGE_NAME( objectName, paramName ) = 0

#define GET_MATERIAL_MODIFIER_PARAM( paramName ) ( MM_Constants[ min(MATMOD_PREFIX( paramName ), MATERIAL_MODIFIER_CONSTANTS_LAST_INDEX) ] )
#define GET_MATERIAL_MODIFIER_PARAM_MATRIX( paramName ) ( float4x4( MM_Constants[ MATMOD_PREFIX( paramName ) ], MM_Constants[ MATMOD_PREFIX( paramName ) + 1 ], MM_Constants[ MATMOD_PREFIX( paramName ) + 2 ], MM_Constants[ MATMOD_PREFIX( paramName ) + 3 ] ) )
#define GET_MATERIAL_MODIFIER_PARAM_BOOL( paramName ) ( MM_Constants[ min(MATMOD_PREFIX( paramName ), MATERIAL_MODIFIER_CONSTANTS_LAST_INDEX) ].x > 0.0 )

#ifdef RED_PLATFORM_ORBIS
	// PSSL
	#define START_CB_NAMED_REGISTER( cbName, paramName )									DECLARE_MATERIAL_MODIFIER_CONSTANT_BUFFER_PARAM( paramName );	\
																							DECLARE_OBJECT_PARAM_LINKAGE( cbName, paramName );				\
																							ConstantBuffer	cbName : register( __CONSTANT_BUFFER_REGISTER_TYPE_##paramName )	 {
														
	#define DECLARE_MATERIAL_MODIFIER_STRUCTBUFFER( type, bufferName, paramName )			DECLARE_MATERIAL_MODIFIER_STRUCT_BUFFER_PARAM( paramName );		\
																							DECLARE_OBJECT_PARAM_LINKAGE( bufferName, paramName );			\
																							RegularBuffer< type > bufferName : register( __STRUCT_BUFFER_REGISTER_TYPE_##paramName );	

	#define DECLARE_MATERIAL_MODIFIER_BYTEBUFFER( bufferName, paramName )					DECLARE_MATERIAL_MODIFIER_BYTE_BUFFER_PARAM( paramName );		\
																							DECLARE_OBJECT_PARAM_LINKAGE( bufferName, paramName );			\
																							ByteBuffer bufferName : register( __BYTE_BUFFER_REGISTER_TYPE_##paramName );
																								
	#define DECLARE_MATERIAL_MODIFIER_TEXTURE2D( textureName, paramName ) 					DECLARE_MATERIAL_MODIFIER_TEXTURE_PARAM( paramName );			\
																							DECLARE_OBJECT_PARAM_LINKAGE( textureName, paramName );			\
																							Texture2D textureName : register( __TEXTURE_REGISTER_TYPE_##paramName );

	#define DECLARE_MATERIAL_MODIFIER_TEXTURE2D_TYPED( textureName, paramName, paramType ) 	DECLARE_MATERIAL_MODIFIER_TEXTURE_PARAM( paramName );			\
																							DECLARE_OBJECT_PARAM_LINKAGE( textureName, paramName );			\
																							Texture2D<paramType> textureName : register( __TEXTURE_REGISTER_TYPE_##paramName );
#else
	// DX
	#define START_CB_NAMED_REGISTER( cbName, paramName )									DECLARE_MATERIAL_MODIFIER_CONSTANT_BUFFER_PARAM( paramName );	\
																							DECLARE_OBJECT_PARAM_LINKAGE( cbName, paramName );				\
																							cbuffer			cbName : register( __CONSTANT_BUFFER_REGISTER_TYPE_##paramName )	 {
														
	#define DECLARE_MATERIAL_MODIFIER_STRUCTBUFFER( type, bufferName, paramName )			DECLARE_MATERIAL_MODIFIER_STRUCT_BUFFER_PARAM( paramName );		\
																							DECLARE_OBJECT_PARAM_LINKAGE( bufferName, paramName );			\
																							StructuredBuffer< type > bufferName : register( __STRUCT_BUFFER_REGISTER_TYPE_##paramName );

	#define DECLARE_MATERIAL_MODIFIER_BYTEBUFFER( bufferName, paramName )					DECLARE_MATERIAL_MODIFIER_BYTE_BUFFER_PARAM( paramName );		\
																							DECLARE_OBJECT_PARAM_LINKAGE( bufferName, paramName );			\
																							ByteAddressBuffer bufferName : register( __BYTE_BUFFER_REGISTER_TYPE_##paramName );
		
	#define DECLARE_MATERIAL_MODIFIER_TEXTURE2D( textureName, paramName )					DECLARE_MATERIAL_MODIFIER_TEXTURE_PARAM( paramName );			\
																							DECLARE_OBJECT_PARAM_LINKAGE( textureName, paramName );			\
																							Texture2D textureName : register( __TEXTURE_REGISTER_TYPE_##paramName );

	#define DECLARE_MATERIAL_MODIFIER_TEXTURE2D_TYPED( textureName, paramName, paramType ) 	DECLARE_MATERIAL_MODIFIER_TEXTURE_PARAM( paramName );			\
																							DECLARE_OBJECT_PARAM_LINKAGE( textureName, paramName );			\
																							Texture2D<paramType> textureName : register( __TEXTURE_REGISTER_TYPE_##paramName );
																								
#endif // RED_PLATFORM_ORBIS

#define END_CB_NAMED_REGISTER  }

#endif // INCLUDE_GUARD_MATERIAL_MODIFIERS_CONSTANT_BUFFERS_FX
