/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/resource.h"
#include "../../redReflection/include/resourceMetricsBank.h"
#include "../../renderData/include/renderingMemory.h"

struct STextureGroupSetup;

//***************************************************************************************************************************//
//***************************************************************************************************************************//

/// Base texture interface
class RESOURCEMATERIAL_API ITexture : public CResource
{
	RTTI_DECLARE_TYPE( ITexture );
	RED_USE_MEMORY_POOL( rend::PoolTexture );

public:

	struct SizeDesc
	{
		Uint16		width;
		Uint16		height;
		Uint16		depth;
		Uint8		numMips;
	};

	using TResourcePtr = TRenderPtr<IRenderResource>;

public:

	ITexture();

	// - - - - - - - - - - - - - - - - - - - - -

	// get render resource to be implemented in subclasses
	virtual const TResourcePtr		CloneRenderResourceWithDynamicAssignment() const;

	virtual const TResourcePtr		GetRenderResource() const = 0;

	virtual void					CreateRenderResource() = 0;

	virtual void					ReleaseRenderResource() = 0;

	virtual Bool					IsRenderResourceCreated() const = 0;

	// Get default resource used when resource of given type has not been found
	virtual res::GatheredResource * GetDefaultResource() const override;

	//! Get the width of the bitmap
	virtual SizeDesc				GetSizeDesc() const = 0;

	virtual const STextureGroupSetup& GetTextureSetup() const = 0;

	// - - - - - - - - - - - - - - - - - - - - -


	static Uint8 CalcMipCount( Uint32 width, Uint32 height, Uint32 depth, Bool hasMipChain );

	static res::TextureGroup GetTextureGroup( GpuWrapApi::eTextureGroup );

	// - - - - - - - - - - - - - - - - - - - - -
};

//***************************************************************************************************************************//
//***************************************************************************************************************************//

RESOURCEMATERIAL_API void SetRecompileMaterialsCallback( const std::function< void( ITexture * ) > & callback );
RESOURCEMATERIAL_API void RecompileMaterials( ITexture * texture );

//***************************************************************************************************************************//
//***************************************************************************************************************************//
