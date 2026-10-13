/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../redContainers/include/string/stringUtils.h"

#include "messageToken.h"
#include "messageLocation.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( MessageTokenType, tools );
	RTTI_ENUM_OPTION( Text );
	RTTI_ENUM_OPTION( Location );
	RTTI_ENUM_OPTION( Tag );
RTTI_END_ENUM();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IMessageToken, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageToken_Text, tools );
	RTTI_PARENT_TYPE( IMessageToken );
	RTTI_PROPERTY( m_text );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageToken_Name, tools );
	RTTI_PARENT_TYPE( IMessageToken );
	RTTI_PROPERTY( m_name );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MessageToken_Location, tools );
	RTTI_PARENT_TYPE( IMessageToken );
	RTTI_PROPERTY( m_location );
RTTI_END_TYPE();

namespace tools
{
	IMessageToken::IMessageToken() = default;
	IMessageToken::~IMessageToken() = default;

	//////////////////////////////////////////////////////////////////////////

	MessageToken_Text::MessageToken_Text() = default;
	MessageToken_Text::MessageToken_Text( const String& text )
		: m_text( text )
	{
		m_text.Trim();
	}

	MessageToken_Text::MessageToken_Text( const red::StringView text )
	{
		red::StringView trimmed = red::Trim( text );
		m_text.Set( trimmed.Data(), trimmed.Length() );
	}

	String MessageToken_Text::ToString() const
	{
		return m_text;
	}

	MessageTokenType MessageToken_Text::GetType() const
	{
		return MessageTokenType::Text;
	}

	//////////////////////////////////////////////////////////////////////////

	MessageToken_Name::MessageToken_Name() = default;
	MessageToken_Name::MessageToken_Name( const CName name )
		: m_name( name )
	{}

	const CName& MessageToken_Name::GetTag() const
	{
		return m_name;
	}

	String MessageToken_Name::ToString() const
	{
		return m_name.AsStringView().ToString();
	}

	MessageTokenType MessageToken_Name::GetType() const
	{
		return MessageTokenType::Tag;
	}

	//////////////////////////////////////////////////////////////////////////

	MessageToken_Location::MessageToken_Location() = default;
	MessageToken_Location::MessageToken_Location( MessageLocationPtr location )
		: m_location( std::move( location ) )
	{}

	res::ResourcePath MessageToken_Location::GetResourcePath() const
	{
		return m_location->GetResourcePath();
	}

	void MessageToken_Location::Goto( IMessageLocationGotoFacilitator* facilitator ) const
	{
		m_location->Goto( facilitator );
	}

	String MessageToken_Location::ToString() const
	{
		return m_location->ToString();
	}

	MessageTokenType MessageToken_Location::GetType() const
	{
		return MessageTokenType::Location;
	}

} // tools
