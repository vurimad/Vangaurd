/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../common/core/include/tagList.h"
#include "../../common/generalEditorBackend/include/tagsProxy.h"

namespace tools
{
	class VisualTagHelper
	{
		DECLARE_INTEROP( VisualTagHelper );

	public:

		VisualTagHelper();
		~VisualTagHelper();

		void Refresh( Bool force = false );

	private:
		Bool LoadFromConfigFile();
		Bool LoadWeaponConfigFile();

		red::DynArray< CName > GetSchemas();
		red::DynArray< proxy::VisualTagItem > GetSchemaTags( const String& schema );
		red::DynArray< proxy::VisualTagItem > GetSchemaPresets( const String& schema );

		THandle< class VisualTagsRoot > m_configRoot;
	};
}
