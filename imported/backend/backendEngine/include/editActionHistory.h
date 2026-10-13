/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "undoFwd.h"
#include "backendMemory.h"

#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/variant.h"
#include "../../../common/redMemory/include/uniquePtr.h"

namespace tools
{
	class IResourceEditor;

	/// Undo history / action holder
	/// The actions ATM are always related to a resource being edited
	class BACKEND_ENGINE_API IEditActionHistory
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( IEditActionHistory );

	public:
		explicit IEditActionHistory( IResourceEditor* editor );
		virtual ~IEditActionHistory();

		/// Clear action history
		void Clear();

		/// Clear all actions that are dependent on resources being reloaded.
		/// For example, if the current actions are [A B C D E], where E is top of undo stack and C modifies one of the given resources,
		/// only [D E] actions will remain on the stack. The same rule applies to redo stack.
		/// Returns true if any actions has been removed.
		Bool OnResourcesReloaded( const red::DynArray< res::ResourcePath >& paths );

		/// Do we have anything to undo ATM ?
		const Bool HasUndo() const;

		/// Do we have anything to redo ATM ?
		const Bool HasRedo() const;

		/// Get name of the action to undo, returns empty string if there's nothing on the undo list
		const String GetUndoDisplayName() const;

		/// Get name of the action to undo, returns empty string if there's nothing on the undo list
		const String GetRedoDisplayName() const;

		/// Get top-most action from undo stack
		const IEditAction* GetTopUndoAction() const;

		/// Execute action, if it's successful it's registered in the undo list
		/// NOTE: the action pointer's ownership will be transfer to the action list
		/// The resource editor is notified every time an action is executed
		Bool Execute( red::UniquePtr< IEditAction > action );

		/// Undo top action from the undo list, returns true on success
		/// The resource editor is notified every time an action is undone
		Bool Undo();

		/// Redo top action from the undo list, returns true on success
		/// The resource editor is notified every time an action is redone
		Bool Redo();

		// Undo the most recent action and remove it from the history so it cannot be redone
		Bool CancelTopUndoAction();

		void StartTransaction( const String& description );
		void MoveIntoTransaction( const Uint32 count );
		void EndTransaction();
		Bool HasActiveTransaction() const;

		//---

		static void InitTransaction();

		static Uint32 TRANSACTION_COUNTER;
		static Uint32 ROLLING_TRANSACTION;

		const IResourceEditor* GetResourceEditor() const { return m_editor; }

	private:
		IEditActionHistory( const IEditActionHistory& ) = delete;
		IEditActionHistory( IEditActionHistory&& ) = delete;
		IEditActionHistory& operator=( const IEditActionHistory& ) = delete;
		IEditActionHistory& operator=( IEditActionHistory&& ) = delete;

		// called when action was done or undone
		void PostExecute( IEditAction& executedAction ) const;

		// attached editor
		IResourceEditor* m_editor; // NOT OWNED

		// internal queues
		typedef red::DynArray< red::UniquePtr< IEditAction > > TActionList;
		TActionList	m_undoList;
		TActionList	m_redoList;

		interop::InteropNotifier m_changedNotifier;

		red::UniquePtr< class EditGroupAction > m_transaction;

		// debug/asserts
		Bool m_inOperation;
	};

	class BACKEND_ENGINE_API ScopedTransaction
	{
	public:
		ScopedTransaction( IEditActionHistory& actionHistory, const String& description );
		~ScopedTransaction();

	private:
		IEditActionHistory& m_actionHistory;
	};

} // tools
