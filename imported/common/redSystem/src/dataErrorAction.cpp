/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataErrorAction.h"

namespace red
{
	const char* DataErrorAction::s_actionTypeSeparator = "[,]";
	const char* DataErrorAction::s_actionDescriptionSeparator = "[:]";
	const char* DataErrorAction::s_actionSeparator = "[;]";
	const Uint32 DataErrorAction::s_actionSeparatorSize = 3;

	DataErrorAction::DataErrorAction( const DataErrorActionType& type, const char* description )
		: m_type( type )
	{
		RED_FATAL_ASSERT( description != nullptr, "Given invalid action description" );
		RED_FATAL_ASSERT( ValidateDescription( description ), "Given action description contains reserved strings like \"[;]\", \"[,]\" or\"[:]\"");
		red::Strcpy( m_description, description, s_descriptionMaxSize );
	}

	DataErrorAction::~DataErrorAction()
	{}

	Bool DataErrorAction::Serialize( char* buffer, Uint32 bufferSize, Uint32& bytesStored ) const
	{
		if ( !HasEnoughSpaceToStoreAction( bufferSize ) )
		{
			RED_FATAL_ASSERT( "Failed to serialize action: not enough space" );
			return false;
		}

		// serialize action type
		snprintf( buffer, bufferSize - bytesStored, "%u", static_cast< Uint32 >( m_type ) );
		const Uint32 typeLength = static_cast< Uint32 >( red::Strlen( buffer ) );
		buffer += typeLength;
		snprintf( buffer, bufferSize - bytesStored, "%s", s_actionTypeSeparator );

		buffer += s_actionSeparatorSize;
		bytesStored += typeLength + s_actionSeparatorSize;

		// serialize action description
		const Uint32 descriptionLength = static_cast< Uint32 >( red::Strlen( m_description ) );
		snprintf( buffer, bufferSize - bytesStored, "%s", m_description );
		buffer += descriptionLength;
		bytesStored += descriptionLength;
		snprintf( buffer, bufferSize - bytesStored, "%s", s_actionDescriptionSeparator );
		bytesStored += s_actionSeparatorSize;
		return true;
	}

	Bool DataErrorAction::HasEnoughSpaceToStoreAction( Uint32 bufferSize ) const
	{
		return bufferSize >= s_typeMaxSize + s_actionSeparatorSize + s_descriptionMaxSize + s_actionSeparatorSize + GetMaximumActionSize();
	}

	Bool DataErrorAction::ValidateDescription( const char* description ) const
	{
		return red::Strstr( description, s_actionTypeSeparator ) == nullptr && red::Strstr( description, s_actionDescriptionSeparator ) == nullptr && red::Strstr( description, s_actionSeparator ) == nullptr;
	}

	ShowAssetDataErrorAction::ShowAssetDataErrorAction( const char* description, const char* assetPath )
		: DataErrorAction( DataErrorActionType::ShowAsset, description )
	{
		// Otherwise crash, but should still report something
		if (!assetPath)
		{
			assetPath = "unknown";
		}

		RED_FATAL_ASSERT( std::strlen( description ) < s_descriptionMaxSize, "Provided data error description is too long!" );
		RED_FATAL_ASSERT( std::strlen( assetPath ) < s_assetPathMaxSize, "Provided data error asset path is too long!" );
		red::Memzero( m_assetPath, s_assetPathMaxSize );
		red::Strcpy( m_assetPath, assetPath, s_assetPathMaxSize );
	}

	Bool ShowAssetDataErrorAction::Serialize( char* buffer, Uint32 bufferSize, Uint32& bytesStored ) const
	{
		if ( !DataErrorAction::Serialize( buffer, bufferSize, bytesStored ) )
		{
			return false;
		}

		buffer += bytesStored;
		const Uint32 assetPathLength = static_cast< Uint32 >( red::Strlen( m_assetPath ) );
		snprintf( buffer, bufferSize - bytesStored, "%s", m_assetPath );
		buffer += assetPathLength;
		bytesStored += assetPathLength;
		snprintf( buffer, bufferSize - bytesStored, "%s", s_actionSeparator );
		bytesStored += s_actionSeparatorSize;
		return true;
	}

	Uint32 ShowAssetDataErrorAction::GetMaximumActionSize() const
	{
		return s_assetPathMaxSize;
	}
}