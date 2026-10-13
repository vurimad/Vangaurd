/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"

namespace tools
{
	class IEditActionHistory;

	/// Any undoable action done in the resource editors is represented by an implementation of this class
	/// Life time of this object is controlled by the action list it's in. 
	/// Never even you should keep POINTERS to anything inside the actions (well, unless you want to crash horribly)
	/// Handles are OK.
	class BACKEND_ENGINE_API IEditAction
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( IEditAction );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IEditAction();
		virtual ~IEditAction();

		/// Get action description, sent to editor
		virtual String GetDescription() const = 0;

		/// Execute the action (Do/Redo step)
		/// Action can fail by returning false, errors are captured from log, it's enough to ERR_XXX them.
		virtual Bool Do() = 0;

		/// Undo execution of the action (Undo step)
		/// Action can fail by returning false, errors are captured from log, it's enough to ERR_XXX them.
		virtual Bool Undo() = 0;

		/// Try to merge action with already executed action
		/// If merging was successful return true, this action will then replace the action from the top of the undo stack
		virtual Bool TryMerge( const IEditAction* lastUndoAction );

		/// Check if the action can be executed without executing it
		virtual Bool CanDo() const;

		/// Indicates whether the action is modifying a resource.
		/// Most of the actions are modifying, but for example selection or entering edit mode are not.
		virtual Bool IsModification() const;

		/// Get list of resources modified by this action.
		/// Note: the function returns hash set, because the action can be compound or modify multiple objects
		/// from the same resource. This is more convenient.
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const;
	};

	/// Group action
	class BACKEND_ENGINE_API EditGroupAction : public IEditAction
	{
	public:
		EditGroupAction(const String& desc);

		/// add sub-action
		void AddAction( red::UniquePtr< IEditAction > action );
		virtual void OnSucces();

	private:
		EditGroupAction( const EditGroupAction& ) = delete;
		EditGroupAction& operator = ( const EditGroupAction& ) = delete;

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool TryMerge(const IEditAction* lastUndoAction) override final;
		virtual Bool CanDo() const override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

		String m_desc;
		red::DynArray< red::UniquePtr< IEditAction > > m_actions;
	};


} // tools