/**
* Copyright (c) 2018-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/core/include/message.h"
#include "../../../common/generalEditorBackend/include/databaseCommon.h"

namespace tools
{
	class BACKEND_ENGINE_API MessageLog : public IMessageReporter
	{
		DECLARE_INTEROP( MessageLog );

	public:
		MessageLog();
		~MessageLog();

		MessageLog( const MessageLog& ) = delete;
		MessageLog& operator = ( const MessageLog& ) = delete;

		virtual void Report( Message& message ) override final;

		void SetFacilitator( red::SharedPtr< IMessageLocationGotoFacilitator > facilitator ) { m_gotoFacilitator = facilitator; }
		virtual void Clear( MessageLocationPtr&& location ) override
		{
			Clear( location->GetPath() );
		}

	private:
		const red::SortedArray< Uint32 >* GetSeverityFilter( Uint32 severity );

		void Notify( const Message& message, Uint32 messageId );
		void Notify( Uint32 messageId );

		red::DynArray< struct MessageLogNotification > BuildFilteredList( CName tag, Uint32 severityFlags, const res::ResourcePath& resource, const String& searchPhrase );

		void DeleteMessage( Uint32 id );
		void Clear( const res::ResourcePath& path );

		Bool SatisfiesSearchPhrase( const Message& message ) const;

		interop::InteropNotifier m_newMessageReceived;

		Uint32 m_idPool;
		red::HashMap< Uint32, Message > m_messages;
		red::SharedPtr< IMessageLocationGotoFacilitator > m_gotoFacilitator;
		sql::DatabaseCommon m_database;
		String m_searchPhrase;
	};
}
