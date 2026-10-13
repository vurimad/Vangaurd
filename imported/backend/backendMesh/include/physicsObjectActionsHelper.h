/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/objectPersistentCollection.h"
#include "../../backendEngine/include/editAction.h"
#include "../../backendEngine/include/selectionFwd.h"

namespace tools
{
	class PhysicsObjectResolver;
	using TObjectIDs = red::DynArray< tools::EditorObjectID >;

	class BACKEND_MESH_API IPhysicsSimpleEditAction : public IEditAction
	{
	public:
		IPhysicsSimpleEditAction( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids );

		virtual String GetDescription() const override final;
		virtual Bool Undo() override final;

	protected:
		PhysicsObjectResolver*	m_resolver;
		SelectionHandler*		m_selectionHandler;

		TObjectIDs				m_ids;
		String					m_desc; // fluff

		red::SharedPtr< IObjectPersistentCollection >	m_capturedObjects;
		red::SharedPtr< SelectionState >				m_capturedOldSelection;

	};


	// Physics object snapping action - snaps an object to it's parent (shape to an actor, actor to scene, joint to parent actor, mesh to actor0 (for now).
	class BACKEND_MESH_API Action_SnapObject : public IPhysicsSimpleEditAction
	{
	public:
		Action_SnapObject(PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids, Bool alternativeMode = false );

		virtual Bool Do() override final;

	private:
		Bool					m_alternativeMode;
	};

	// Physics object parent selection action - selects object parent.
	class BACKEND_MESH_API Action_SelectParent : public IEditAction
	{
	public:
		Action_SelectParent( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids );

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;

	private:
		PhysicsObjectResolver*	m_resolver;
		SelectionHandler*		m_selectionHandler;

		TObjectIDs				m_ids;
		String					m_desc; // fluff

		red::SharedPtr< IObjectPersistentCollection >	m_capturedObjects;
		red::SharedPtr< SelectionState >				m_capturedOldSelection;
	};

	// Physics object parent selection action - selects object parent.
	class BACKEND_MESH_API Action_SelectObject : public IEditAction
	{
	public:
		Action_SelectObject( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids );

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;

	private:
		PhysicsObjectResolver*	m_resolver;
		SelectionHandler*		m_selectionHandler;

		TObjectIDs				m_ids;
		String					m_desc; // fluff

		red::SharedPtr< IObjectPersistentCollection >	m_capturedObjects;
		red::SharedPtr< SelectionState >				m_capturedOldSelection;
	};

	class BACKEND_MESH_API Action_RecomputeCOM : public IPhysicsSimpleEditAction
	{
	public:
		Action_RecomputeCOM( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids );

		virtual Bool Do() override final;
	};

	class BACKEND_MESH_API Action_GenerateDefaultDestructionHierarchy : public IEditAction
	{
	public:
		Action_GenerateDefaultDestructionHierarchy(PhysicsObjectResolver* resolver );
		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;

	protected:
		PhysicsObjectResolver*  m_resolver;
		String					m_desc;

		red::DynArray<Uint32>	m_previousHierarchy;
	};
}