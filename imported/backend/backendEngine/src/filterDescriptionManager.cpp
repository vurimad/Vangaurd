/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "filterDescriptionManager.h"
#include "previewFilters.h"

#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"

namespace tools
{
	const String FilterDescriptionManager::s_filename = "descriptions.json";

	Bool FilterDescriptionManager::LoadDescriptions()
	{
		String jsonData;
		if ( !LoadFileToString( res::GetDebugFilterDescriptionsFilePath( s_filename ), jsonData ) )
			return false;

		if ( !res::json::ParseObjectInline( std::move( jsonData ), m_descriptions ) )
			return false;

		for ( const DebugFilterDataContainerItem& item : m_descriptions->m_items )
		{
			m_descriptionHash[ item.m_name ] = item;
		}

		return true;
	}

	FilterDescriptionManager::FilterDescriptionManager()
	{
		LoadDescriptions();
	}

	FilterDescriptionManager::~FilterDescriptionManager()
	{
	}

	Bool FilterDescriptionManager::IsDeprecated( const String& path )
	{
		auto item = m_descriptionHash.FindPtr( path );
		if ( item != nullptr )
		{
			return m_descriptionHash[ path ].m_deprecated;
		}

		return false;
	}

	Bool FilterDescriptionManager::SetDeprecated( const String& path, const Bool deprecated )
	{
		auto item = m_descriptionHash.FindPtr( path );
		if ( item != nullptr )
		{
			m_descriptionHash[ path ].m_deprecated = deprecated;
			return true;
		}

		return false;
	}

	Bool FilterDescriptionManager::SetDescription( const String& path, const String& description )
	{
		auto item = m_descriptionHash.FindPtr( path );
		if ( item != nullptr )
		{
			m_descriptionHash[ path ].m_description = description;
			return true;
		}

		return false;
	}

	String FilterDescriptionManager::GetDescription( const String& path ) const
	{
		auto item = m_descriptionHash.FindPtr( path );
		if ( item != nullptr )
		{
			return m_descriptionHash[ path ].m_description;
		}

		return String::EMPTY();
	}

	Bool FilterDescriptionManager::SaveDescriptions()
	{
		m_descriptions->m_items = m_descriptionHash.GetValues();

		serialization::json::JsonSerializer jsonSerializer( m_descriptions );
		String result = String::EMPTY();
		jsonSerializer.Serialize( result );

		SaveStringToFile( res::GetDebugFilterDescriptionsFilePath( s_filename ), result );

		return true;
	}
}