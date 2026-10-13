/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/core/include/processStatusReporter.h"

namespace interop
{
	namespace srv
	{
		class InteropService;
	}
}

namespace red
{
	class CommandletInteropMessageHandler : public red::Thread
	{
		RED_USE_MEMORY_POOL( PoolBackend );

	public:
		CommandletInteropMessageHandler( Uint16 port );
		virtual ~CommandletInteropMessageHandler() override;
		virtual void ThreadFunc() override;

		void Stop();
		Bool IsRunning() const;

		interop::srv::InteropClient* GetInteropClient();

	private:
		red::UniquePtr< comm::NetworkConnection > m_networkConnection;
		red::UniquePtr< interop::srv::InteropClient > m_interopClient;

		red::Atomic< Bool > m_isRunning;
	};

	class BACKEND_ENGINE_API InteractiveCommandletCommunicationHandler : public IProcessStatusReporter
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		InteractiveCommandletCommunicationHandler( const CommandLine& options );
		~InteractiveCommandletCommunicationHandler();

		// IProcessStatusReporter
		virtual red::Stages DeclareStages( red::DynArray< String >&& names, Uint32 parent = red::InvalidStageId ) override final;
		virtual Uint32 GetCurrentStageId() const override final;

		virtual Uint32 GetTask( const String& title ) override final;
		virtual void SetTaskProgress( Uint32 taskId, Uint32 currentProgress, Uint32 totalProgress, Float processingTime = 0.f ) override final;
		virtual Uint32 GetMessage( Uint32 taskId, const String& text = String::EMPTY(), red::TaskTextMessage::Type type = TaskTextMessage::Info ) override final;
		virtual void SetTaskMessage( Uint32 taskId, Uint32 messageId, const String& text, red::TaskTextMessage::Type type = TaskTextMessage::Info ) override final;

		virtual void AddFileToP4( const res::ResourcePath& path ) override final;
		// TODO: remove this - this is risky, may end up as a separate channel of communication
		virtual void SendGenericMessage( const String& message ) override final;

		Bool IsRunning() const;

		red::SharedPtr< interop::utils::Object > GetInteropRoot() const;

	private:
		virtual void SetCurrentStageId( Uint32 stageId ) override final;
		virtual void StartStage( Uint32 stageId ) override final;
		virtual void FinishStage( Uint32 stageId ) override final;

		void SetupInterop( interop::srv::InteropClient& client, Uint32 commandletIndex );
		
		red::UniquePtr< interop::srv::InteropSink > m_sink;

		red::Atomic< Uint32 > m_nextStageId = 0;
		red::Atomic< Uint32 > m_nextTaskId = 0;
		red::Atomic< Uint32 > m_nextMessageId = 0;
		red::Atomic< Uint32 > m_currentStageId = red::InvalidStageId;
		red::DynArray< Uint32 > m_stagesIds{ PoolBackend() };

		red::Mutex m_lock;
		red::HashMap< Uint32, Float > m_cachedTasksProgress{ PoolBackend() };
		red::HashMap< Uint32, String > m_cachedStagesNames{ PoolBackend() };

		red::UniquePtr< CommandletInteropMessageHandler > m_processMsgNotifier;

		String m_handlerId;
	};
}