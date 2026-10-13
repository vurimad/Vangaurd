/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "message.h"

#include "messageToken.h"
#include "messageLocation.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( MessageSeverity, tools );
	RTTI_ENUM_OPTION( Success );
	RTTI_ENUM_OPTION( Info );
	RTTI_ENUM_OPTION( Warning );
	RTTI_ENUM_OPTION( Error );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Message, tools );
	RTTI_PROPERTY( m_severity );
	RTTI_PROPERTY( m_created );
	RTTI_PROPERTY( m_location );
	RTTI_PROPERTY( m_tokens );
RTTI_END_TYPE();

namespace tools
{
	Message::Message() = default;
	Message::~Message() = default;
	Message::Message( Message&& ) = default;
	Message& Message::operator = ( Message&& ) = default;
	Message::Message( const Message& ) = default;
	Message& Message::operator = ( const Message& ) = default;

	Message::Message( const MessageSeverity severity, MessageLocationPtr&& location )
		: m_severity( severity )
		, m_location( std::move( location ) )
	{
		m_created = std::time( nullptr );
	}

	void Message::Build( const red::StringView format )
	{
		for ( const AnsiChar c : format )
		{
			// TODO %% should turn into %
			RED_FATAL_ASSERT( c != '%', "Message argument not found" );
		}

		*this << format;
	}

	void Message::SetResourcePath( const res::ResourcePath &path )
	{
		m_path = path;
	}

	const res::ResourcePath& Message::GetResourcePath() const
	{
		return m_path;
	}

	void Message::SetMessageSource( CName source )
	{
		m_messageSource = source;
	}

	CName Message::GetMessageSource() const
	{
		return m_messageSource;
	}

	Message& Message::operator << ( const red::StringView text )
	{
		if( !text.Empty() )
			m_tokens.PushBack( message::CreateTokenPtr< MessageToken_Text >( text ) );

		return *this;
	}

	Message& Message::operator << ( const String& text )
	{
		if( !text.Empty() )
			m_tokens.PushBack( message::CreateTokenPtr< MessageToken_Text >( text ) );

		return *this;
	}

	Message& Message::operator << ( const AnsiChar* text )
	{
		if ( text && text[ 0 ] != '\0' )
			m_tokens.PushBack( message::CreateTokenPtr< MessageToken_Text >( red::StringView( text ) ) );

		return *this;
	}

	Message& Message::operator << ( const CName name )
	{
		if( !name.Empty() )
			m_tokens.PushBack( message::CreateTokenPtr< MessageToken_Name >( name ) );

		return *this;
	}

	Message& Message::operator << ( const MessageLocationPtr& location )
	{
		m_tokens.PushBack( message::CreateTokenPtr< MessageToken_Location >( std::move( location ) ) );
		return *this;
	}

	Message& Message::operator << ( const res::ResourcePath& resourcePath )
	{
		return *this << message::CreateLocationPtr< MessageLocation_Resource >( resourcePath );
	}

	Message& Message::operator << ( const tools::EditorObjectIDPath& objectPath )
	{
		return *this << message::CreateLocationPtr< MessageLocation_EditorObject >( objectPath );
	}

	Message& Message::operator << ( const tools::EditorObjectID& objectID )
	{
		return *this << message::CreateLocationPtr< MessageLocation_EditorObject >( objectID );
	}

	Message& Message::operator << ( const math::Vector3& point )
	{
		const res::ResourcePath path = m_location ? m_location->GetResourcePath() : res::ResourcePath();
		return *this << message::CreateLocationPtr< MessageLocation_Point >( path, point );
	}

	Message& Message::operator << ( const math::Box& boundingBox )
	{
		const res::ResourcePath path = m_location ? m_location->GetResourcePath() : res::ResourcePath();
		return *this << message::CreateLocationPtr< MessageLocation_BoundingBox >( path, boundingBox );
	}

	Message& Message::operator<<( const rtti::AccessPath& path )
	{
		return *this << path.ToString();
	}

	const AnsiChar* Message::SeverityToString( const MessageSeverity severity )
	{
		switch ( severity )
		{
		case MessageSeverity::Success:		return "Success";
		case MessageSeverity::Info:			return "Info";
		case MessageSeverity::Warning:		return "Warning";
		case MessageSeverity::Error:		return "Error";
		}

		RED_FATAL( "Invalid message severity" );
		return nullptr;
	}

	String Message::ToString() const
	{
		String result;
		for ( Uint32 i = 0; i < m_tokens.Size(); ++i )
		{
			result += m_tokens[i]->ToString();
			if ( i + 1 < m_tokens.Size() )
			{
				result += ' ';
			}
		}
		return result;
	}

	String Message::ToStringFull() const
	{
		struct tm* timeinfo;
		char buffer[64];

		timeinfo = localtime( &m_created );

		strftime( buffer, RED_ARRAY_COUNT( buffer ), "%T", timeinfo );

		return String::Printf( "%s - [%s] %s", buffer, SeverityToString( m_severity ), ToString().AsChar() );
	}

	IMessageReporter::~IMessageReporter() = default;

	IMessageReporter* IMessageReporter::s_reporter = nullptr;

	void IMessageReporter::SetReporter( IMessageReporter* reporter )
	{
		s_reporter = reporter;
	}

	Bool operator==( const tools::Message &lhs, const tools::Message &rhs )
	{
		return std::tie( lhs.m_severity, lhs.m_created, lhs.m_location, lhs.m_path, lhs.m_messageSource )
			== std::tie( rhs.m_severity, rhs.m_created, rhs.m_location, rhs.m_path, rhs.m_messageSource );
	}
} // tools
