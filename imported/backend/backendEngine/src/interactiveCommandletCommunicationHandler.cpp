/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "interactiveCommandletCommunicationHandler.h"

#include "../../common/redCore/include/commandline.h"
#include "../../common/interopServices/include/interopService.h"
#include "../../common/commChannel/include/networkConnection.h"

using namespace interop::srv;

namespace red
{
	CommandletInteropMessageHandler::CommandletInteropMessageHandler( Uint16 port )
		: red::Thread( "CmdletInterop" )
	{
		const String connectionName = "Commandlet"; // TODO: does this have to be unique
		const String connectionDisplayName = "Commandlet";
		m_networkConnection = red::CreateUniquePtr< comm::NetworkConnection >( connectionName, connectionDisplayName );

		if ( !m_networkConnection->Initialize() )
		{

		}

		InteropClient::Setup setup;
		setup.m_clientName = "CommandletInteropClient";
		setup.m_hostAddress = "127.0.0.1";
		setup.m_hostPort = port;

		m_interopClient = red::CreateUniquePtr< InteropClient >( m_networkConnection->GetChannelFactory() );
		m_interopClient->Intialize( setup );
		Bool result = m_interopClient->Connect();

		m_isRunning.SetValue( result );
	}

	CommandletInteropMessageHandler::~CommandletInteropMessageHandler()
	{
		m_interopClient->Shutdown();
		m_networkConnection->Shutdown();
	}

	void CommandletInteropMessageHandler::ThreadFunc()
	{
		while ( m_isRunning.GetValue() )
		{
			m_interopClient->Update();
			SleepOnCurrentThread( 100 );
		}
	}

	void CommandletInteropMessageHandler::Stop()
	{
		m_isRunning.SetValue( false );
	}

	Bool CommandletInteropMessageHandler::IsRunning() const
	{
		return m_isRunning.GetValue();
	}

	InteropClient* CommandletInteropMessageHandler::GetInteropClient()
	{
		return m_interopClient.Get();
	}

	//////////////////////////////////////////////////////////////////////////

	InteractiveCommandletCommunicationHandler::InteractiveCommandletCommunicationHandler( const CommandLine& options )
	{
		String portStr;
		options.GetFirstOption( "interopPort", portStr );

		String communicationIdStr;
		options.GetFirstOption( "communicationId", communicationIdStr );

		if ( !portStr.Empty() && !communicationIdStr.Empty() )
		{
			Uint16 port = FromStringDirect< Uint16 >( portStr );
			Uint32 communicationId = FromStringDirect< Uint32 >( communicationIdStr );

			m_processMsgNotifier = red::CreateUniquePtr< CommandletInteropMessageHandler >( port );
			if ( IsRunning() )
			{
				SetupInterop( *m_processMsgNotifier->GetInteropClient(), communicationId );
				m_processMsgNotifier->InitThread();

				// TODO: poor man's synchronization, this will most likely cause problems depending on hardware
				SleepOnCurrentThread( 1000 );
			}
		}
		else
		{
			RED_LOG( "InteractiveCommandletCommunicationHandler: No port specified, communication won't be processed" );
		}

		String uniqueIdStr;
		if ( options.GetFirstOption( "uniqueId", uniqueIdStr ) )
		{
			m_handlerId = uniqueIdStr;
		}
	}

	InteractiveCommandletCommunicationHandler::~InteractiveCommandletCommunicationHandler()
	{
		if ( m_processMsgNotifier )
		{
			// TODO: poor man's synchronization, this will most likely cause problems depending on hardware
			SleepOnCurrentThread( 100 );

			m_processMsgNotifier->Stop();
			m_processMsgNotifier->JoinThread();
		}
	}

	red::Stages InteractiveCommandletCommunicationHandler::DeclareStages( red::DynArray< String >&& names, Uint32 parent )
	{
		if ( !IsRunning() )
		{
			return red::Stages( *this, {} );
		}

		StageMessage stages;
		stages.m_ids.Reserve( names.Size() );

		const Uint32 startId = m_nextStageId.GetValue();
		for ( const String& name : names )
		{
			const Uint32 id = m_nextStageId.PostIncrement();
			stages.m_ids.PushBack( id );

			m_cachedStagesNames.Insert( id, name );
		}
		const Uint32 endId = m_nextStageId.GetValue();

		stages.m_parent = parent;
		stages.m_names = std::move( names );

		if ( parent == -1 )
		{
			stages.m_reset = true;
		}

		const Uint32 index = m_stagesIds.GetIndex( parent ) + 1;
		for ( const Uint32 id : red::ReverseIndexRange{ endId, startId } )
		{
			m_stagesIds.InsertAt( index, id );
		}

		m_sink->Dispatch( InteropPath().Call( "declareStages", stages ) );
		return red::Stages( *this, { startId, endId } );
	}

	Uint32 InteractiveCommandletCommunicationHandler::GetCurrentStageId() const
	{
		return m_currentStageId.GetValue();
	}

	void InteractiveCommandletCommunicationHandler::SetCurrentStageId( Uint32 stageId )
	{
		if ( !IsRunning() )
		{
			return;
		}

		m_currentStageId.SetValue( stageId );
	}

	void InteractiveCommandletCommunicationHandler::StartStage( Uint32 stageId )
	{
		if ( !IsRunning() )
		{
			return;
		}

		m_sink->Dispatch( InteropPath().Call( "startStage", stageId ) );
	}

	void InteractiveCommandletCommunicationHandler::FinishStage( Uint32 stageId )
	{
		if ( !IsRunning() )
		{
			return;
		}

		{
			red::ScopedLock< red::Mutex > lock( m_lock );
			m_cachedTasksProgress.Clear();
		}

		m_sink->Dispatch( InteropPath().Call( "finishStage", stageId ) );
	}

	Uint32 InteractiveCommandletCommunicationHandler::GetTask( const String& title )
	{
		if ( !IsRunning() )
		{
			return red::InvalidTaskId;
		}

		TaskNameMessage taskMessage;
		taskMessage.m_id = m_nextTaskId.PostIncrement();
		taskMessage.m_title = title;

		const String stageName = m_cachedStagesNames[ GetCurrentStageId() ];
		taskMessage.m_uniqueName = RED_NAME( String::Printf( "%hs/%hs/%hs", m_handlerId.AsChar(), stageName.AsChar(), title.AsChar() ).AsChar() );

		m_sink->Dispatch( InteropPath().Call( "declareTask", taskMessage ) );
		return taskMessage.m_id;
	}

	void InteractiveCommandletCommunicationHandler::SetTaskProgress( Uint32 taskId, Uint32 currentProgress, Uint32 totalProgress, Float processingTime )
	{
		if ( !IsRunning() )
		{
			return;
		}

		red::ScopedLock< red::Mutex > lock( m_lock );

		static Float s_epsilon = 0.01;
		const Float normalizedProgress = (Float)currentProgress / totalProgress;
		if ( m_cachedTasksProgress.KeyExist( taskId ) && math::Abs( normalizedProgress - m_cachedTasksProgress[ taskId ] ) < s_epsilon && normalizedProgress != 1.0f )
		{
			return;
		}

		TaskProgressMessage taskMessage;
		taskMessage.m_id = taskId;
		taskMessage.m_progress = normalizedProgress;
		taskMessage.m_processingTime = processingTime;

		m_cachedTasksProgress[ taskId ] = normalizedProgress;
		m_sink->Dispatch( InteropPath().Call( "progressTask", taskMessage ) );
	}

	Uint32 InteractiveCommandletCommunicationHandler::GetMessage( Uint32 taskId, const String& text, red::TaskTextMessage::Type type )
	{
		if ( !IsRunning() )
		{
			return red::InvalidMessageId;
		}

		TaskTextMessage taskMessage;
		taskMessage.m_taskId = taskId;
		taskMessage.m_messageId = m_nextMessageId.PostIncrement();
		taskMessage.m_text = text;
		taskMessage.m_type = type;

		m_sink->Dispatch( InteropPath().Call( "declareTaskMessage", taskMessage ) );
		return taskMessage.m_messageId;
	}

	void InteractiveCommandletCommunicationHandler::SetTaskMessage( Uint32 taskId, Uint32 messageId, const String& text, red::TaskTextMessage::Type type )
	{
		if ( !IsRunning() )
		{
			return;
		}

		TaskTextMessage taskMessage;
		taskMessage.m_taskId = taskId;
		taskMessage.m_messageId = messageId;
		taskMessage.m_text = text;
		taskMessage.m_type = type;

		m_sink->Dispatch( InteropPath().Call( "declareTaskMessage", taskMessage ) );
	}

	void InteractiveCommandletCommunicationHandler::AddFileToP4( const res::ResourcePath& path )
	{
		if ( !IsRunning() )
		{
			return;
		}

		m_sink->Dispatch( InteropPath().Call( "addFile", path.ToString() ) );
	}

	void InteractiveCommandletCommunicationHandler::SendGenericMessage( const String& message )
	{
		if ( !IsRunning() )
		{
			return;
		}

		m_sink->Dispatch( InteropPath().Call( "genericMessage", message ) );
	}

	void InteractiveCommandletCommunicationHandler::SetupInterop( interop::srv::InteropClient& client, Uint32 communicationId )
	{
		const InteropPath path = InteropPath().GetRoot().GetChild( "commandlets" ).GetMapElement( communicationId );
		m_sink = red::CreateUniquePtr< InteropSink >( client, path );
	}

	Bool InteractiveCommandletCommunicationHandler::IsRunning() const
	{
		return m_processMsgNotifier && m_processMsgNotifier->IsRunning();
	}
}