#include "build.h"
#include "processStatusReporter.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( StageMessage, red )
RTTI_PROPERTY( m_parent );
RTTI_PROPERTY( m_reset );
RTTI_PROPERTY( m_names );
RTTI_PROPERTY( m_ids );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TaskNameMessage, red )
RTTI_PROPERTY( m_id );
RTTI_PROPERTY( m_title );
RTTI_PROPERTY( m_uniqueName );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TaskProgressMessage, red )
RTTI_PROPERTY( m_id );
RTTI_PROPERTY( m_progress );
RTTI_PROPERTY( m_processingTime );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TaskTextMessage, red )
RTTI_PROPERTY( m_taskId );
RTTI_PROPERTY( m_text );
RTTI_PROPERTY( m_type );
RTTI_END_TYPE();

RTTI_BEGIN_ENUM_IN_NAMESPACE( Type, red, TaskTextMessage );
RTTI_ENUM_OPTION( Info );
RTTI_ENUM_OPTION( Error );
RTTI_END_ENUM();

namespace red
{
	static NullStatusReporter s_nullStatusReporter;

	red::Stages NullStatusReporter::DeclareStages( red::DynArray<String>&& names, Uint32 parent )
	{
		return red::Stages( *this, {} );
	}

	Uint32 NullStatusReporter::GetCurrentStageId() const
	{
		return InvalidStageId;
	}

	Uint32 NullStatusReporter::GetTask( const String& title )
	{
		return InvalidTaskId;
	}

	Uint32 NullStatusReporter::GetMessage( Uint32 taskId, const String& text, red::TaskTextMessage::Type type )
	{
		return InvalidMessageId;
	}

	//////////////////////////////////////////////////////////////////////////

	Stages::Stages( IProcessStatusReporter& reporter, const red::IndexRange& stages )
		: m_reporter( reporter )
		, m_scopeStages( stages )
		, m_currentStageId( m_scopeStages.StartIndex() )
	{
		m_reporter.StartStage( m_currentStageId );
	}

	Stages::~Stages()
	{
		FinishAllStages();
	}

	void Stages::FinishCurrentStage()
	{
		if ( !m_scopeStages.Contains( m_currentStageId ) )
		{
			return;
		}

		m_reporter.FinishStage( m_currentStageId );
		m_reporter.SetCurrentStageId( InvalidStageId );
		++m_currentStageId;

		if ( m_scopeStages.Contains( m_currentStageId ) )
		{
			m_reporter.StartStage( m_currentStageId );
			m_reporter.SetCurrentStageId( m_currentStageId );
		}
	}

	Uint32 Stages::GetCurrentStageId() const
	{
		if ( !m_scopeStages.Contains( m_currentStageId ) )
		{
			return InvalidStageId;
		}

		return m_currentStageId;
	}

	void Stages::FinishAllStages()
	{
		for (; m_currentStageId <= m_scopeStages.EndIndex(); ++m_currentStageId )
		{
			m_reporter.FinishStage( m_currentStageId );
		}

		m_reporter.SetCurrentStageId( InvalidStageId );
	}
}