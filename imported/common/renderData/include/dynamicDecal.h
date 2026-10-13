/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// NOTE : Keep this synced to dynamicDecalGen.fx
enum ERenderDynamicDecalProjection
{
	RDDP_Ortho = 0,		// Project decal into a rectangular area
	RDDP_Sphere = 1,	// Project decal into an ellipsoid volume
};
RTTI_DECLARE_ENUM( ERenderDynamicDecalProjection );

enum EDynamicDecalSpawnPriority
{
	RDDS_Normal		= 0,	// Spawn as regular in-game decal (obey limits)
	RDDS_Highest	= 1,	// Force spawn as cutscene, dismemberment etc (do not obey)
};
RTTI_DECLARE_ENUM( EDynamicDecalSpawnPriority );

enum ERenderDynamicDecalAtlas
{
	RDDA_1x1 = 0,	
	RDDA_2x1 = 1,	
	RDDA_2x2 = 2,	
	RDDA_4x2 = 3,	
	RDDA_4x4 = 4,	
	RDDA_8x4 = 5,	
};
RTTI_DECLARE_ENUM( ERenderDynamicDecalAtlas );

struct RENDER_DATA_API SDynamicDecalMaterialInfo
{
	TRenderPtr<IRenderResource>		m_material;

	Color							m_specularColor;
	Float							m_specularScale;
	Float							m_specularBase;
	Float							m_specularity;

	Bool							m_additiveNormals;		// If normal texture is given, says whether decal normals replace or add to scene normals.

	Color							m_diffuseColor;
	Color							m_diffuseRandomColor0;		//!< Diffuse color used in randomization
	Color							m_diffuseRandomColor1;		//!< Diffuse color used in randomization
	ERenderDynamicDecalAtlas		m_subUVType;

	SDynamicDecalMaterialInfo();
	~SDynamicDecalMaterialInfo();

};

struct RENDER_DATA_API SDynamicDecalInitInfo
{
	SDynamicDecalMaterialInfo		m_staticMaterialInfo;			// Does not addref this.
	SDynamicDecalMaterialInfo		m_skinnedMaterialInfo;			// Does not addref this.

	class CRenderProxy_DynamicDecal*	m_parentDecal;

	Vector4							m_origin;				// Position of the decal.
	Vector4							m_dirFront;				// Direction the decal should be projected along.
	Vector4							m_dirUp;				// Decal's "up" direction, to adjust rotation around dirFront.
	Float							m_width;				// Width of the decal
	Float							m_height;				// Height of the decal
	Float							m_nearZ;				// "near" distance for decal projection. Can be negative to project behind the origin.
	Float							m_farZ;					// "far" distance for decal projection.

	Float							m_depthFadePower;		// Exponent applied to the distance from origin along "front" vector.
	Float							m_depthFadeOrigin;
	Float							m_normalFadeBias;		// 
	Float							m_normalFadeScale;		// 
	Float							m_roughnessScale;		//
	Float							m_normalThreshold;		//
	Bool							m_additiveNormals;		// If normal texture is given, says whether decal normals replace or add to scene normals.
	Bool							m_doubleSided;			// Whether the decal should be projected in both directions, or one-way.

	Bool							m_applyInLocalSpace;	// Whether the decal info above describes the decal in the local space of whatever it's being applied
															// to, or in world space. Most things should probably use world space, since multiple objects could
															// be affected by the same decal.

	Bool							m_projectOnStatic;		// Project on static geometry
	Bool							m_projectOnSkinned;		// Project on dynamic skinned geometry

	Bool							m_isDismembermentDecal;	// Is this a dismemberment decal

	ERenderDynamicDecalProjection	m_projectionMode;
	Bool							m_useVerticalProjection;

	Float							m_timeToLive;			// How long the decal lives until it is automatically removed. Defaults to Infinite, but most users
															// should use something finite.

	EDynamicDecalSpawnPriority		m_spawnPriority;		// What is the decal spawning manner / usage (in-game, cutscene etc)

	Float							m_fadeTime;				// How long the decal takes to fade out as TTL expires. Should be lower than TTL.
	Float							m_fadeInTime;			// How long the decal takes to fade in
	Float							m_startScale;			// starting scale of decal (startScale * size)
	Float							m_scaleTime;			// how long it takes the decal to reach full size
	Float							m_autoHideDistance;		// Auto hide distance for dynamic decal
	Vector4							m_atlasVector;			// Scale bias vector for decals UV (for random atlasing)

	red::DynArray< Float >			m_sizeSamples{ rend::PoolDecals() };			// Size samples extracted from size float evaluator
	red::DynArray< Float >			m_lifeTimeSamples{ rend::PoolDecals() };		// Lifetime samples extracted from lifetime float evaluator

	SDynamicDecalInitInfo();

	Matrix GetWorldToDecalMatrix() const;
	Matrix GetDecalToWorldMatrix() const;
	
	Box GetWorldBounds() const;
	void GetWorldBoundPoints( red::StaticArray< Vector4, 8 >& arr ) const;

	void SetAtlasVector( ERenderDynamicDecalAtlas atlasType );
	void SetAtlasVector( Uint32 scaleS, Uint32 scaleT, Uint32 index );

	RED_INLINE Bool IsValid() const
	{
		return ( m_staticMaterialInfo.m_material != nullptr ) || ( m_skinnedMaterialInfo.m_material != nullptr );
	}

private:
	RED_FORCE_INLINE void GetDecalVectors( Vector4& right, Vector4& up, Vector4& forward, Vector4& origin ) const
	{
		const Vector4 dirRight	= Vector4::Cross( m_dirFront, m_dirUp, 0.0f );
		right					= -dirRight * m_width * 0.5f;
		up						= m_dirUp * m_height * 0.5f;
		forward					= m_dirFront * ( m_farZ - m_nearZ );
		origin					= m_origin;
	}

	RED_FORCE_INLINE void GetDecalBoxCorners( Vector4& corner0, Vector4& corner1, Vector4& corner2, Vector4& corner3, Vector4& forward ) const
	{
		Vector4 vecRight;
		Vector4 vecUp;
		Vector4 origin;
		GetDecalVectors( vecRight, vecUp, forward, origin );

		corner0 = origin - vecRight - vecUp + forward * m_nearZ;
		corner1 = origin - vecRight + vecUp + forward * m_nearZ;
		corner2 = origin + vecRight + vecUp + forward * m_nearZ;
		corner3 = origin + vecRight - vecUp + forward * m_nearZ;
	}
};
