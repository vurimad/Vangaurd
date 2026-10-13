/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerMaskMetadata.h"


RTTI_BEGIN_TYPE( Multilayer_MaskResourceImportMetadata )
RTTI_PARENT_TYPE( res::ResourceImportMetadata );
{
	RTTI_PROPERTY( m_sourceImagePaths );
}
RTTI_END_TYPE()


void Multilayer_MaskResourceImportMetadata::SetSourceImagePaths( const red::ArraySpan<const red::AbsolutePath>& paths )
{
	m_sourceImagePaths.Clear();
	m_sourceImagePaths.Reserve(paths.Size());
	for ( const red::AbsolutePath& path : paths )
	{
		m_sourceImagePaths.PushBack( path.AsString() );
	}
}
