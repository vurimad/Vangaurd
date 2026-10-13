/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editAction.h"
#include "editActionHistory.h"
#include "resourceEditor.h"

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/streamedResourceManager.h"
#include "../../common/core/include/message.h"

namespace tools
{
	Uint32 IEditActionHistory::TRANSACTION_COUNTER = 0;
	Uint32 IEditActionHistory::ROLLING_TRANSACTION = 1;

	IEditActionHistory::IEditActionHistory( IResourceEditor* editor )
		: m_editor( editor )
		, m_undoList { red::PoolBackend() }
		, m_redoList { red::PoolBackend() }
		, m_inOperation( false )
	{
	}

	IEditActionHistory::~IEditActionHistory() = default;

	void IEditActionHistory::Clear()
	{
		m_undoList.Clear();
		m_redoList.Clear();
		m_changedNotifier.Notify();
	}

	Bool IEditActionHistory::OnResourcesReloaded( const red::DynArray< res::ResourcePath >& paths )
	{
		red::DynArray< res::ResourcePath > sortedPaths = paths;
		std::sort( sortedPaths.Begin(), sortedPaths.End() );

		// TODO optimize if reloading multiple resources is too slow
		const auto clearFunc = [sortedPaths] ( TActionList& actionsList ) -> Bool
		{
			red::Set< res::ResourcePath > affectedResources;

			// index of a first action in 'actionsList' that modifies one of the resource from 'paths'
			Uint32 startIndex = std::numeric_limits< Uint32 >::max();

			// "top" of stack is at the end of the array, so iterate in reversed order
			for ( Uint32 i = actionsList.Size(); i-- > 0; )
			{
				actionsList[i]->GetAffectedResources( affectedResources );
				if ( !affectedResources.Empty() )
				{
					if ( res::ContainsCommonPath( sortedPaths, affectedResources.Elements() ) )
					{
						// found first action to remove
						startIndex = i;
						break;
					}

					affectedResources.Clear();
				}
			}

			if ( startIndex >= actionsList.Size() )
			{
				// no actions to remove
				return false;
			}

			// remove actions range
			actionsList.RemoveAt( 0, startIndex + 1 );
			return true;
		};

		const Bool removedFromUndoList = clearFunc( m_undoList );
		const Bool removedFromRedoList = clearFunc( m_redoList );

		if ( removedFromUndoList | removedFromUndoList )
		{
			m_changedNotifier.Notify();
			return true;
		}

		return false;
	}

	const Bool IEditActionHistory::HasUndo() const
	{
		return !m_undoList.Empty();
	}

	const Bool IEditActionHistory::HasRedo() const
	{
		return !m_redoList.Empty();
	}

	const String IEditActionHistory::GetUndoDisplayName() const
	{
		if ( m_undoList.Empty() )
			return String::EMPTY();

		return m_undoList.Back()->GetDescription();
	}

	const String IEditActionHistory::GetRedoDisplayName() const
	{
		if ( m_redoList.Empty() )
			return String::EMPTY();

		return m_redoList.Back()->GetDescription();
	}

	const IEditAction* IEditActionHistory::GetTopUndoAction() const
	{
		if ( !m_undoList.Empty() )
			return m_undoList.Back().Get();

		return nullptr;
	}

	Bool IEditActionHistory::Execute( red::UniquePtr< IEditAction > action )
	{
		RED_FATAL_ASSERT( !m_inOperation, "Execute() called while operation is being performed" );

		// NULL actions always execute (by definition)
		if ( !action )
			return true;

		// check if the action can be done in the first place
		if ( !action->CanDo() )
		{
			return false;
		}

		m_inOperation = true;

		IEditAction& currentAction = *action;

		// try to merge with current top undo action
		Bool mergeOK = false;
		EditGroupAction* transaction = m_transaction.Get();
		if ( !m_undoList.Empty() && !m_transaction )
		{
			IEditAction* topUndoAction = m_undoList.Back().Get();
			mergeOK = action->TryMerge( topUndoAction );
		}

		// execute action
		if ( !action->Do() )
		{
			m_inOperation = false;
			return false; // action failed to execute
		}

		const Bool redoListWasEmpty = m_redoList.Empty();

		// clear redo actions
		m_redoList.Clear();

		// since we merged results with previous undo action pop it back from the undo stack
		if ( mergeOK )
		{
			m_undoList.PopBack();
		}

		if ( transaction )
		{
			// Add the action to the active transaction
			// Later, the transaction will be added to the undo list
			transaction->AddAction( std::move( action ) );
		}
		else
		{
			// add just executed action to the undo stack
			m_undoList.PushBack( std::move( action ) );
		}

		PostExecute( currentAction );
		m_inOperation = false;

		// notify
		if ( m_editor != nullptr )
		{
			m_editor->OnActionExecute( currentAction );
		}

		Bool isChanged = !mergeOK || !redoListWasEmpty;
		if ( isChanged )
			m_changedNotifier.Notify();

		return true;
	}

	Bool IEditActionHistory::Undo()
	{
		RED_FATAL_ASSERT( !m_transaction, "Cannot undo whilst a transaction is being created" );
		RED_FATAL_ASSERT( !m_inOperation, "Undo() called while operation is being performed" );

		// nothing to undo - considered successful undo
		if ( m_undoList.Empty() )
			return true;

		// get the last action from the undo list
		red::UniquePtr< IEditAction > lastAction = m_undoList.PopBack();

		// mark reentry flag
		m_inOperation = true;

		// undo it
		RED_LOG( "Core: Undoing: '%hs'", lastAction->GetDescription().AsChar() );
		if ( !lastAction->Undo() )
		{
			m_inOperation = false;
			RED_LOG_ERROR( "Core: Failed undo: '%hs'", lastAction->GetDescription().AsChar() );

			RED_REPORT_ERROR( RED_LOCATION( m_editor->GetPath() ), "% failed: %", RED_NAME_CONSTEXPR( "Undo" ), lastAction->GetDescription() );

			// revert back to the undo list - it may work in a moment (usually related to user loading/unloading layers)
			m_undoList.PushBack( std::move( lastAction ) );
			return false;
		}

		// finish operation
		m_inOperation = false;

		// move to redo - list
		IEditAction& executedAction = *lastAction;
		m_redoList.PushBack( std::move( lastAction ) );

		PostExecute( executedAction );

		// notify
		if ( m_editor != nullptr )
		{
			m_editor->OnActionExecute( executedAction );
		}
		m_changedNotifier.Notify();

		RED_REPORT_SUCCESS( RED_LOCATION( m_editor->GetPath() ), "%: %", RED_NAME_CONSTEXPR( "Undo" ), executedAction.GetDescription() );

		return true;
	}

	Bool IEditActionHistory::Redo()
	{
		RED_FATAL_ASSERT( !m_transaction, "Cannot redo whilst a transaction is being created" );
		RED_FATAL_ASSERT( !m_inOperation, "Redo() called while operation is being performed" );

		// nothing to redo - considered successful redo 
		if ( m_redoList.Empty() )
			return true;

		// get the last action from the redo list
		red::UniquePtr< IEditAction > lastAction = m_redoList.PopBack();

		// mark flag
		m_inOperation = true;

		// redo it
		RED_LOG( "Core: Redoing: '%hs'", lastAction->GetDescription().AsChar() );
		if ( !lastAction->Do() )
		{
			m_inOperation = false;

			RED_LOG_ERROR( "Core: Failed redo: '%hs'", lastAction->GetDescription().AsChar() );

			RED_REPORT_ERROR( RED_LOCATION( m_editor->GetPath() ), "% failed: %", RED_NAME_CONSTEXPR( "Redo" ), lastAction->GetDescription() );

			// revert back to the redo list - it may work in a moment (usually related to user loading/unloading layers)
			m_redoList.PushBack( std::move( lastAction ) );
			return false;
		}

		// finish operation
		m_inOperation = false;

		// move to undo - list
		IEditAction& executedAction = *lastAction;
		m_undoList.PushBack( std::move( lastAction ) );

		PostExecute( executedAction );

		// notify
		if ( m_editor != nullptr )
		{
			m_editor->OnActionExecute( executedAction );
		}
		m_changedNotifier.Notify();

		RED_REPORT_SUCCESS( RED_LOCATION( m_editor->GetPath() ), "%: %", RED_NAME_CONSTEXPR( "Redo" ), executedAction.GetDescription() );

		return true;
	}

	void IEditActionHistory::PostExecute( IEditAction& executedAction ) const
	{
		// update streaming data
		if ( auto* streamingManager = GetStreamingResourceManager() )
		{
			red::Set< res::ResourcePath > affectedResources;
			executedAction.GetAffectedResources( affectedResources );
			for ( const res::ResourcePath& path : affectedResources )
			{
				// LEFT FOR WARNING: Don't do this here because we already refresh the streaming
				// data when transform properties change. If this is needed for some other action,
				// then it shouldn't be done for transforms. Refreshing the streaming data twice adds up,
				// especially if there's a lot.
				//streamingManager->RefreshStreamingData( path );
			}
		}
	}

	Bool IEditActionHistory::CancelTopUndoAction()
	{
		if( !Undo() )
			return false;

		m_redoList.Clear();

		return true;
	}

	void IEditActionHistory::StartTransaction( const String& description )
	{
		if ( m_transaction )
		{
			RED_LOG_INFO( "%hs: Transaction already in progress", __FUNCTION__ );
			return;
		}

		RED_FATAL_ASSERT( !m_transaction );

		m_transaction = red::CreateUniquePtr< EditGroupAction >( description );
	}

	void IEditActionHistory::MoveIntoTransaction( const Uint32 count )
	{
		for ( Uint32 i = 0; i < count && HasUndo(); ++i )
		{
			red::UniquePtr< IEditAction > action = m_undoList.PopBack();
			m_transaction->AddAction( std::move( action ) );
		}
	}

	void IEditActionHistory::EndTransaction()
	{
		if ( !m_transaction )
		{
			RED_LOG_INFO( "%hs: No transaction currently in progress", __FUNCTION__ );
			return;
		}

		m_undoList.PushBack( std::move( m_transaction ) );
	}

	Bool IEditActionHistory::HasActiveTransaction() const
	{
		return m_transaction;
	}

	void IEditActionHistory::SetupInterop( tools::IEditActionHistory* ptr, interop::utils::Object& op )
	{
		op.RegisterValueRO<Bool>( "hasUndo", 
			[ptr]( ::interop::IContext& ctx, bool& outValue )
			{
				outValue = ptr->HasUndo();
			}, 
			&ptr->m_changedNotifier );
		op.RegisterValueRO<Bool>( "hasRedo",
			[ptr]( ::interop::IContext& ctx, bool& outValue )
			{
				outValue = ptr->HasRedo();
			},
			&ptr->m_changedNotifier );

		op.RegisterValueRO<Int32>( "undoCount",
			[ptr]( ::interop::IContext& ctx, Int32& outValue )
			{
				outValue = ptr->m_undoList.Size();
			},
			&ptr->m_changedNotifier );
		op.RegisterValueRO<Int32>( "redoCount",
			[ptr]( ::interop::IContext& ctx, Int32& outValue )
			{
				outValue = ptr->m_redoList.Size();
			},
			&ptr->m_changedNotifier );

		op.RegisterValueRO<String>( "undoDesc",
			[ptr]( ::interop::IContext& ctx, String& outValue )
			{
				outValue = ptr->GetUndoDisplayName();
			},
			&ptr->m_changedNotifier );
		op.RegisterValueRO<String>( "redoDesc",
			[ptr]( ::interop::IContext& ctx, String& outValue )
			{
				outValue = ptr->GetRedoDisplayName();
			},
			&ptr->m_changedNotifier );

		op.RegisterFunctor<String>( "undo",
			[ptr](::interop::IContext& ctx)
			{
				if ( ptr->HasUndo() )
				{
					if ( ptr->Undo() )
					{
						return ptr->GetRedoDisplayName();
					}
					else
					{
						ctx.ThrowException( "Failed to undo last action." );
					}
				}
				else
					ctx.ThrowException("Nothing to undo");

				return String::EMPTY();
			});

		op.RegisterFunctor<String>( "redo",
			[ptr](::interop::IContext& ctx)
			{
				if ( ptr->HasRedo() )
				{
					if ( ptr->Redo() )
					{
						return ptr->GetUndoDisplayName();
					}
					else
					{
						ctx.ThrowException( "Failed to redo action." );
					}
				}
				else
					ctx.ThrowException( "Nothing to redo" );

				return String::EMPTY();
			});

		op.RegisterFunctor<void, String>( "startTransaction", [ptr]( ::interop::IContext& ctx, const String& description ) { ptr->StartTransaction( description ); } );
		op.RegisterFunctor<void>( "endTransaction", [ptr]( ::interop::IContext& ctx ) { ptr->EndTransaction(); } );
	}

	void IEditActionHistory::InitTransaction()
	{
		IEditActionHistory::TRANSACTION_COUNTER++;

		if ( IEditActionHistory::TRANSACTION_COUNTER == 0 )
			IEditActionHistory::TRANSACTION_COUNTER++;

		IEditActionHistory::ROLLING_TRANSACTION = IEditActionHistory::TRANSACTION_COUNTER;
	}

	//////////////////////////////////////////////////////////////////////////

	ScopedTransaction::ScopedTransaction( IEditActionHistory& actionHistory, const String& description )
		: m_actionHistory( actionHistory )
	{
		m_actionHistory.StartTransaction( description );
	}

	ScopedTransaction::~ScopedTransaction()
	{
		if ( m_actionHistory.HasActiveTransaction() )
		{
			m_actionHistory.EndTransaction();
			if ( !m_actionHistory.CancelTopUndoAction() )
			{
				RED_LOG_ERROR( "ScopedTransaction: Couldn't rollback a failed transaction" );
			}
		}
	}

}  // done
