/**
* Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceEditorUserContext.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceEditorUserContext, tools )
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE()

namespace tools
{
	ResourceEditorUserContext::ResourceEditorUserContext()
		: ResourceEditorUserContext( nullptr )
	{
	}

	ResourceEditorUserContext::ResourceEditorUserContext( IResourceEditor* editor )
		: m_editor( editor )
	{
	}

	ResourceEditorUserContext::~ResourceEditorUserContext() = default;

	void ResourceEditorUserContext::SetIncludedTagsProvider( IIncludedTagsProvider* provider )
	{
		m_includedTagsProvider.Reset( provider );
	}

	void ResourceEditorUserContext::SetConvertProvider( IConvertProvider* convertProvider )
	{
		m_convertProvider.Reset( convertProvider );
	}
}