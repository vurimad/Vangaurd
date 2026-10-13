/**
* Copyright (c) 2018-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "messageLog.h"
#include "../../common/generalEditorBackend/include/messageLog.h"
#include "../../../common/core/include/clipboardBase.h"

namespace tools
{
	namespace
	{
		const Uint32 DATABASE_VERSION = 1;
	}

	MessageLog::MessageLog()
		: m_idPool( 0 )
		, m_gotoFacilitator( nullptr )
		, m_database( DATABASE_VERSION )
	{
		m_database.Open( ":memory:", sql::Database::Mode::ReadWrite );
		m_database.Create();

		m_database.Execute
		(
			R"#(CREATE TABLE entries( "messageId" INTEGER PRIMARY KEY NOT NULL, "resource" INTEGER NOT NULL, "timestamp" INTEGER NOT NULL, "severity" INTEGER NOT NULL );)#"
			R"#(CREATE TABLE tags( "id" INTEGER PRIMARY KEY NOT NULL, "hash" INTEGER NOT NULL, "messageId" INTEGER NOT NULL ); )#"
		);
	}

	MessageLog::~MessageLog() = default;

	RED_INLINE void MessageLog::Notify( const Message& message, Uint32 messageId )
	{
		const String resourcePath = message.GetLocation() ? message.GetLocation()->GetResourcePath().ToString() : String::EMPTY();
		m_newMessageReceived.Notify( MessageLogNotification{ message.GetCreated(), messageId, resourcePath, message.GetSeverity() } );
	}

	RED_INLINE void MessageLog::Notify( Uint32 messageId )
	{
		const Message& message = m_messages[ messageId ];
		Notify( message, messageId );
	}

	red::DynArray< MessageLogNotification > MessageLog::BuildFilteredList( CName tag, Uint32 severityFlags, const res::ResourcePath& resource, const String& searchPhrase )
	{
		m_searchPhrase = searchPhrase;
		red::DynArray< MessageLogNotification > filteredList;

		String sql = "SELECT entries.messageId FROM entries";

		if ( !tag.Empty() )
		{
			sql += " INNER JOIN tags ON tags.hash = :tag AND tags.messageId == entries.messageId";
		}

		sql += " WHERE entries.severity & :severity";

		if ( resource.IsValid() )
		{
			sql += " AND entries.resource = :resource";
		}

		sql += " ORDER BY entries.timestamp ASC";

		red::UniquePtr< sql::Query > query = m_database.Select( sql.AsChar() );

		query->Bind( ":severity", severityFlags );

		if ( resource.IsValid() )
		{
			query->Bind( ":resource", resource.GetHash() );
		}

		if ( !tag.Empty() )
		{
			query->Bind( ":tag", tag.GetHash() );
		}

		query->Next();

		if ( query->HasError() )
			return filteredList;

		while ( !query->End() )
		{
			Uint32 messageId;
			query->Value( "messageId", messageId );

			const Message& message = m_messages[ messageId ];

			const String resourcePath = message.GetLocation() ? message.GetLocation()->GetResourcePath().ToString() : String::EMPTY();

			if ( SatisfiesSearchPhrase( message ) )
				filteredList.PushBack( std::move( MessageLogNotification{ message.GetCreated(), messageId, resourcePath, message.GetSeverity() } ) );

			query->Next();
		}

		return filteredList;
	}

	void MessageLog::DeleteMessage( Uint32 id )
	{
		m_messages.Remove( id );

		m_database.StartTransaction();
			m_database.ExecuteSql( R"#(DELETE FROM entries WHERE entries.messageId = ? )#", id );
			m_database.ExecuteSql( R"#(DELETE FROM tags WHERE tags.messageId = ? )#", id );
		m_database.EndTransaction();
	}

	void MessageLog::Clear( const res::ResourcePath& path )
	{
		red::DynArray< Uint32 > keys = m_messages.GetKeys();

		for ( Uint32 key : keys )
		{
			if ( m_messages[ key ].GetLocation()->GetResourcePath() == path )
				m_messages.Remove( key );
		}

		m_database.StartTransaction();
			m_database.ExecuteSql( R"#(DELETE FROM tags WHERE tags.messageId IN (SELECT entries.messageId FROM entries WHERE entries.resource = ? ); )#", path.GetHash() );
			m_database.ExecuteSql( R"#(DELETE FROM entries WHERE entries.resource = ? )#", path.GetHash() );
		m_database.EndTransaction();
	}

	Bool MessageLog::SatisfiesSearchPhrase( const Message& message ) const
	{
		return m_searchPhrase.Empty() || message.ToString().ContainsNoCase( m_searchPhrase );
	}

	void MessageLog::Report( Message& receievedMessage )
	{
		if ( receievedMessage.GetLocation() == nullptr )
		{
			RED_LOG_ERROR( "Message Log message without location!" );

			return;
		}

		const Uint32 messageId = m_idPool;
		m_messages[ messageId ] = std::move( receievedMessage );
		const Message& storedMessage = m_messages[ messageId ];
		++m_idPool;

		m_database.StartTransaction();

		const Uint64 resourceHash = storedMessage.GetLocation()->GetResourcePath().GetHash();
		const time_t timestamp = storedMessage.GetCreated();
		const Uint32 severityFlag = MessageSeverityToBitflag( storedMessage.GetSeverity() );

		m_database.ExecuteSql( R"#(INSERT INTO entries VALUES ( ?, ?, ?, ? );)#", messageId, resourceHash, timestamp, severityFlag );

		for ( Uint32 i = 0; i < storedMessage.TokenCount(); ++i )
		{
			const IMessageToken& component = storedMessage[ i ];

			switch ( component.GetType() )
			{
			case MessageTokenType::Tag:
				CName tag = static_cast<const MessageToken_Name&>( component ).GetTag();

				m_database.ExecuteSql( R"#(INSERT INTO tags ( 'hash', 'messageId' ) VALUES ( ?, ? ); )#", tag.GetHash(), messageId );
			}
		}

		m_database.EndTransaction();

		if ( SatisfiesSearchPhrase( storedMessage ) )
		{
			Notify( storedMessage, messageId );
		}
	}

	void MessageLog::SetupInterop( MessageLog* ptr, ::interop::utils::Object& op )
	{
		op.RegisterNotifier( "onMessage", ptr->m_newMessageReceived );

		op.RegisterFunctor< MessageLogComponent, Uint32, Uint32 >( "component", [ ptr ]( ::interop::IContext& ctx, Uint32 id, Uint32 chunkId ) -> MessageLogComponent
		{
			const Message* const messagePtr = ptr->m_messages.FindPtr( id );
			if( !messagePtr )
			{
				ctx.ThrowException( "Message with id '%d' doesn't exist.", id );
				return MessageLogComponent{};
			}

			const Message& message = *messagePtr;

			MessageLogComponent component;
			component.m_messageId = id;
			component.m_componentId = chunkId;

			if( chunkId < message.TokenCount() )
			{
				const IMessageToken& token = message[chunkId];
				component.m_data = token.ToString();
				component.m_type = token.GetType();
			}
			else
			{
				ctx.ThrowException( "Message with id '%d' doesn't have token '%d' (Token count is '%d').", id, chunkId, message.TokenCount() );
				component.m_data = "INVALID";
				component.m_type = MessageTokenType::Text;
			}

			return component;
		} );

		op.RegisterFunctor< Uint32, Uint32 >( "componentCount", [ ptr ]( ::interop::IContext& ctx, Uint32 messageId )
		{
			return ptr->m_messages[ messageId ].TokenCount();
		} );

		op.RegisterFunctor< String, Uint32, Uint32 >( "getComponentResource", [ ptr ]( ::interop::IContext& ctx, Uint32 messageId, Uint32 componentId )
		{
			Message* message = ptr->m_messages.FindPtr( messageId );

			if ( !message )
			{
				ctx.ThrowException( "Invalid message log id specified: %u", messageId );

				return String::EMPTY();
			}

			if ( componentId >= message->TokenCount() )
			{
				ctx.ThrowException( "Message log (id: %u) does not have a component with id %u", messageId, componentId );

				return String::EMPTY();
			}

			const IMessageToken& component = ( *message )[ componentId ];

			if ( component.GetType() != MessageTokenType::Location )
			{
				ctx.ThrowException( "Message log (id: %u) component (id: %u) is not a location!", messageId, componentId );

				return String::EMPTY();
			}

			const MessageToken_Location& location = static_cast< const MessageToken_Location& >( component );

			return location.GetResourcePath().ToString();
		} );

		op.RegisterFunctor< void, Uint32, Uint32 >( "gotoLink", [ ptr ]( ::interop::IContext& ctx, Uint32 messageId, Uint32 componentId )
		{
			Message* message = ptr->m_messages.FindPtr( messageId );

			if ( !message )
			{
				ctx.ThrowException( "Invalid message log id specified: %u", messageId );
				return;
			}

			if ( componentId >= message->TokenCount() )
			{
				ctx.ThrowException( "Message log (id: %u) does not have a component with id %u", messageId, componentId );
				return;
			}

			const IMessageToken& component = ( *message )[ componentId ];

			if ( component.GetType() != MessageTokenType::Location )
			{
				ctx.ThrowException( "Message log (id: %u) component (id: %u) is not a location!", messageId, componentId );
				return;
			}

			const MessageToken_Location& location = static_cast<const MessageToken_Location&>( component );

			if ( ptr->m_gotoFacilitator )
			{
				location.Goto( ptr->m_gotoFacilitator.Get() );
			}
		} );

		op.RegisterFunctor< red::DynArray< MessageLogNotification >, CName, Uint32, String, String >( "listFiltered", [ ptr ]( ::interop::IContext& ctx, CName tag, Uint32 severityFlags, const String& resourcePath, const String& searchPhrase )
		{
			return ptr->BuildFilteredList( tag, severityFlags, res::ResourcePath::Build( resourcePath ), searchPhrase );
		} );

		op.RegisterFunctor< void, Uint32 >( "delete", [ ptr ]( ::interop::IContext& ctx, Uint32 messageId )
		{
			ptr->DeleteMessage( messageId );
		} );

		op.RegisterFunctor< void, Message >( "frontendMessage", [ ptr ]( ::interop::IContext&, const Message& message )
		{
			// It doesn't seem to be possible to remove the const from the lambda parameter
			Message& msg = const_cast<Message&>( message );

			ptr->Report( msg );
		} );

		op.RegisterFunctor< void, String >( "clear", [ ptr ]( ::interop::IContext& ctx, const String& rawResourcePath )
		{
			res::ResourcePath& resourcePath = res::ResourcePath::Build( rawResourcePath );

			ptr->Clear( resourcePath );
		} );

		op.RegisterFunctor< void, Uint32 >( "copyToClipboard", [ptr] ( ::interop::IContext& ctx, Uint32 messageId )
		{
			Message* message = ptr->m_messages.FindPtr( messageId );

			GClipboard->Open( CF_Text );
			GClipboard->Copy( Utf16String( message->ToStringFull().AsChar() ) );
			GClipboard->Close();
		} );

		op.RegisterFunctor< void, String >( "copyAll", [ptr] ( ::interop::IContext& ctx, const String& rawResourcePath )
		{
			res::ResourcePath& resourcePath = res::ResourcePath::Build( rawResourcePath );

			red::StringBuilder<String> builder;

			for ( const auto& key : ptr->m_messages.GetKeys() )
			{
				Message *message = ptr->m_messages.FindPtr( key );

				if ( !ptr->SatisfiesSearchPhrase( *message ) )
					continue;

				if ( message->GetLocation()->GetResourcePath() == resourcePath )
				{
					builder.Append( message->ToStringFull() );
					builder.Append( "\n" );
				}
			}

			if ( !builder.Empty() )
			{
				GClipboard->Open( CF_Text );
				GClipboard->Copy( Utf16String( builder.AsChar() ) );
				GClipboard->Close();
			}
		} );
	}
}
