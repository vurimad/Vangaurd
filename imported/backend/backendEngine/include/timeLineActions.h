/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editAction.h"
#include "selectionHandler.h"
#include "timeLine.h"

namespace tools
{
	class TimelineSelectionAction : public IEditAction
	{
	public:
		TimelineSelectionAction( ISelectionHandler* selectionHandler, const String desc, Uint32 actionBatchIndex, SelectionStatePtr oldState, SelectionStatePtr newState );
		virtual ~TimelineSelectionAction();

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;
		virtual Bool IsModification() const override;

	private:
		static String s_description;

		ISelectionHandler* m_handler; // actual selection handler that is responsible for the selection state

		String m_desc; // user description for the undo/redo actions
		Uint32 m_batchIndex; // "batch index" of selection action, actions happening withing the same instant (frame) have the same batch index

		SelectionStatePtr m_oldState; // old selection state
		SelectionStatePtr m_newState; // new selection state
	};

	///
	///
	///

	class TimelineMoveEvents : public IEditAction
	{
	public:
		TimelineMoveEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, Float timeChange );

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:
		red::DynArray< TimelineId > m_eventsId;

		Float m_timeOffset;
		ITimeLine* m_timeline;
	};

	///
	///
	///

	class TimelineMuteSelectedEvents : public IEditAction
	{
	public:
		TimelineMuteSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, Bool isMuted );

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:
		red::DynArray< TimelineId > m_eventsId;
		ITimeLine* m_timeline;
		Bool m_isMuted;
		red::HashMap< TimelineId, Bool > m_previousMuteState;
	};

	///
	///
	///

	class TimelineToggleMuteSelectedEvents : public IEditAction
	{
	public:
		TimelineToggleMuteSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids );

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:
		red::DynArray< TimelineId > m_eventsId;
		ITimeLine* m_timeline;
		red::HashMap< TimelineId, Bool > m_previousMuteState;

	};

	///
	///
	///

	class TimelineResizeSelectedEvents : public IEditAction
	{
	public:
		TimelineResizeSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, TimelineResizeMode mode, Float timeChange );

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:
		red::DynArray< TimelineId > m_eventsId;

		Float m_timeOffset;
		TimelineResizeMode m_mode;

		ITimeLine* m_timeline;
	};

	///
	///
	///

	class TimelineClipSelectedEvents : public IEditAction
	{
	public:
		TimelineClipSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, TimelineResizeMode mode, Float timeChange );

		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:
		red::DynArray< TimelineId > m_eventsId;

		Float m_timeOffset;
		TimelineResizeMode m_mode;

		ITimeLine* m_timeline;
	};
}