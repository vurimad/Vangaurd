
#pragma once

namespace red
{
	class IProcessStatusReporter;

	const Uint32 InvalidStageId = static_cast<Uint32>( -1 );
	const Uint32 InvalidTaskId = static_cast<Uint32>( -1 );
	const Uint32 InvalidMessageId = static_cast<Uint32>( -1 );

	struct RED_CORE_API StageMessage
	{
		RTTI_DECLARE_TYPE( StageMessage );

		Uint32 m_parent = InvalidStageId;
		Bool m_reset = false;

		red::DynArray< Uint32 > m_ids{ PoolBackend() };
		red::DynArray< String > m_names{ PoolBackend() };
	};

	struct RED_CORE_API TaskNameMessage
	{
		RTTI_DECLARE_TYPE( TaskNameMessage );

		Uint32 m_id = InvalidTaskId;
		String m_title;
		CName m_uniqueName;
	};

	struct RED_CORE_API TaskProgressMessage
	{
		RTTI_DECLARE_TYPE( TaskProgressMessage );

		Uint32 m_id = InvalidTaskId;
		Float m_progress = 0.f;
		Float m_processingTime = 0.f;
	};

	struct RED_CORE_API TaskTextMessage
	{
		enum Type
		{
			Info,
			Error
		};

		RTTI_DECLARE_TYPE( TaskTextMessage );

		Uint32 m_taskId = InvalidTaskId;
		Uint32 m_messageId = InvalidMessageId;
		String m_text;
		Type m_type = Info;
	};

	class RED_CORE_API Stages
	{
	public:
		Stages( IProcessStatusReporter& reporter, const red::IndexRange& stages );
		~Stages();

		void FinishCurrentStage();
		Uint32 GetCurrentStageId() const;

	private:
		void FinishAllStages();

		IProcessStatusReporter& m_reporter;
		red::IndexRange m_scopeStages;
		Uint32 m_currentStageId;
	};

	template< typename T >
	class ReporterSetter
	{
	public:
		ReporterSetter( IProcessStatusReporter* reporter, T& reportedObject )
			: m_reportedObject( reportedObject )
		{
			if ( reporter )
			{
				m_reportedObject.SetReporter( reporter );
			}
		}

		~ReporterSetter()
		{
			m_reportedObject.ResetReporter();
		}

	private:
		T& m_reportedObject;
	};

	class RED_CORE_API IProcessStatusReporter
	{
		friend class Stages;

	public:
		virtual ~IProcessStatusReporter() {}

		// stages are processed sequentially, tasks can be processed in parallel
		virtual red::Stages DeclareStages( red::DynArray< String >&& names, Uint32 parent = InvalidStageId ) = 0;
		virtual Uint32 GetCurrentStageId() const = 0;

		// tasks are always created in the current stage
		// |uniqueName| can be used to collect and save average processing time metadata
		virtual Uint32 GetTask( const String& title ) = 0;
		virtual void SetTaskProgress( Uint32 taskId, Uint32 currentProgress, Uint32 totalProgress, Float processingTime = 0.f ) = 0;
		virtual Uint32 GetMessage( Uint32 taskId, const String& text = String::EMPTY(), red::TaskTextMessage::Type type = TaskTextMessage::Info ) = 0;
		virtual void SetTaskMessage( Uint32 taskId, Uint32 messageId, const String& text, red::TaskTextMessage::Type type = TaskTextMessage::Info ) = 0;

		virtual void AddFileToP4( const res::ResourcePath& path ) = 0;
		// TODO: remove this - this is risky, may end up as an interface breach
		virtual void SendGenericMessage( const String& message ) = 0;

	protected:
		virtual void SetCurrentStageId( Uint32 stageId ) = 0;
		virtual void StartStage( Uint32 stageId ) = 0;
		virtual void FinishStage( Uint32 stageId ) = 0;
	};

	class RED_CORE_API NullStatusReporter : public IProcessStatusReporter
	{
	public:
		virtual red::Stages DeclareStages( red::DynArray<String>&& names, Uint32 parent = InvalidStageId ) override;
		virtual Uint32 GetCurrentStageId() const override;
		virtual void SetCurrentStageId( Uint32 stageId ) override {};
		virtual void StartStage( Uint32 stageId ) override {};
		virtual void FinishStage( Uint32 stageId ) override {};
		virtual Uint32 GetTask( const String& title ) override;
		virtual void SetTaskProgress( Uint32 taskId, Uint32 currentProgress, Uint32 totalProgress, Float processingTime ) override {};
		virtual Uint32 GetMessage( Uint32 taskId, const String& text = String::EMPTY(), red::TaskTextMessage::Type type = TaskTextMessage::Info ) override;
		virtual void SetTaskMessage( Uint32 taskId, Uint32 messageId, const String& text, red::TaskTextMessage::Type type = TaskTextMessage::Info ) override {};
		virtual void AddFileToP4( const res::ResourcePath& path ) override {};
		virtual void SendGenericMessage( const String& message ) override {};
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( Type, red, TaskTextMessage );