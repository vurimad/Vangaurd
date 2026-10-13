/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/resourceImportMetadata.h"


// Really simple extension to import metadata for Multilayer_Mask resources.
class BACKEND_MATERIAL_API Multilayer_MaskResourceImportMetadata: public res::ResourceImportMetadata
{
	RTTI_DECLARE_TYPE( Multilayer_MaskResourceImportMetadata );
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	Multilayer_MaskResourceImportMetadata() = default;

	// Multilayer_Mask can be created from many different source image files, not the normal "one in, one out" pattern.
	// So this gives a way to get all image files that were used. Depending on how the mask was imported, this could be
	// a single path, the same as ResourceImportMetadata::GetAssetPath(), or it may be something different.
	RED_INLINE const red::DynArray<String>& GetSourceImagePaths() const { return m_sourceImagePaths; }

	void SetSourceImagePaths( const red::ArraySpan<const red::AbsolutePath>& paths );

private:
	red::DynArray<String> m_sourceImagePaths;
};
