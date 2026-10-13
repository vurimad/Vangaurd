/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	class DebugFilterDataContainer;
	class DebugFilterDataContainerItem;

	class FilterDescriptionManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		FilterDescriptionManager();
		~FilterDescriptionManager();

		Bool IsDeprecated( const String& path );
		Bool SetDeprecated( const String& path, const Bool deprecated );

		Bool SetDescription( const String& path, const String& description );
		String GetDescription( const String& path ) const;

	private:
		Bool LoadDescriptions();
		Bool SaveDescriptions();

		static const String s_filename;

		THandle< DebugFilterDataContainer > m_descriptions;

		using DescriptionsHash = red::HashMap<String, DebugFilterDataContainerItem>;
		DescriptionsHash m_descriptionHash;
	};
}
