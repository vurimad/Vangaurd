/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#ifndef REFLECTION_PROBE_RESOURCE_H
#define REFLECTION_PROBE_RESOURCE_H

#include "renderDataApi.h"
#include "renderTextureBlob.h"

#include "../../redReflection/include/streamedResource.h"
#include "../../redReflection/include/dataBuffer.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace envUtils
{
	enum class NeighborMode : Uint8
	{
		eCLOSEST,
		eONLY_GLOBAL,
		eONLY_SELF,
		eFILL_SURROUNDING,
	};

	enum class ReflectionProbeAmbientContributionMode : Uint8
	{
		eNO_AMBIENT_CONTRIBUTION,		// never provide ambient from this probe
		eALLOW_AMBIENT_CONTRIBUTION,	// provide ambient for this probe whenever GI is not available (override DistantGI ambient too)
		eOVERRIDE_GI_AMBIENT			// override all GI ambient
	};
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

class RENDER_DATA_API CReflectionProbeDataResource : public res::StreamedResource
{
	RTTI_DECLARE_TYPE( CReflectionProbeDataResource );
	IMPLEMENT_RESOURCE_INTERFACE( "envprobe", "envprobe", "Reflection Probe Data Resource" );
	RED_BASE_CLASS( res::StreamedResource );
	RED_USE_MEMORY_POOL( rend::PoolReflectionProbes );

	DataBuffer					m_data;
	rend::RenderTextureResource	m_textureData;

	Uint64						m_dataHash;
	Bool						m_haveSkyData;
	Float						m_faceDepth[6];

public:
	CReflectionProbeDataResource();

	virtual ~CReflectionProbeDataResource();

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	virtual void							RecreateInternalRenderResources( Uint32 flag ) override;

	virtual void							OnPreSave( const PreSaveContext& context ) override;

	// virtual Bool							IsSystemFile() override;

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	const DataBuffer&						GetData_LEGACY() const;

	const THandle< IRenderResourceBlob >	GetTextureDataBlob() const;

	RED_FORCE_INLINE Uint64					GetDataHash() const { return m_dataHash; };

	RED_FORCE_INLINE Bool					HaveSkyData() const { return m_haveSkyData; }

	RED_FORCE_INLINE red::ArraySpan<const Float> GetFaceDepth() const { return m_faceDepth; }

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

#ifndef NO_TEXTURE_EDITING
	void									SetEditorData( Uint64 dataHash, Bool haveSkyData, Float faceDepth[6], rend::RenderTextureResource&& data );

	static void								SetResourceResaver( Bool( *callback )(CReflectionProbeDataResource*, ECookingPlatform) );

	bool									Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform );
#endif

	static Quaternion						BuildEnvProbeRotation( Uint32 faceIndex );
	static Float							BuildEnvProbeFOV( Uint32 faceIndex, const Vector3& boxSize );
	static Float							BuildEnvProbeAspectRatio( Uint32 faceIndex, const Vector3& boxSize );
	static Vector2							BuildEnvProbeViewRatio( Uint32 faceIndex, const Vector3& boxSize );

	static void								BuildEnvProbeCamera( CRenderCamera &camera, const Vector3 &origin, const Quaternion& rotation, Uint32 faceIndex, const Vector3& boxSize = Vector3(1,1,1), Float nearClipDistance = 0.0f, Float farClipDistance = 4000.0f );

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

};

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

#endif