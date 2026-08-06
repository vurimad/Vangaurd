/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/string/string.h"
#include "handle.h"

/// Snapshot of the scripting state
class RED_REFLECTION_API CScriptSnapshot
{
public:
	//! Snapshot of a scripted field
	struct PropertySnapshot
	{
		RED_USE_MEMORY_POOL( red::PoolScript );

		typedef red::DynArray< PropertySnapshot* > SubProperties;

		CName							m_name;				//!< Name of the cached property
		String							m_valueString;		//!< Value - in case of simple types
		THandle< IScriptable >			m_valueHandle;		//!< Value - in case of object pointer
		SubProperties					m_subValues;		//!< Sub values - arrays and structures

		PropertySnapshot()
			: m_subValues( red::PoolScript() )
		{
		}

		~PropertySnapshot()
		{
			red::alg::ClearPtr( m_subValues );
		}
	};

	//! Snapshot of a scripted object
	struct RED_REFLECTION_API ScriptableSnapshot
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

		typedef red::DynArray< ScriptableSnapshot* > States;

		THandle< IScriptable >				m_scriptable;		//!< Object being snapshotted
		PropertySnapshot::SubProperties		m_properties;		//!< Properties

		States								m_states;
		ScriptableSnapshot*					m_activeState;

		const void*							m_debugOrgAddress;
		CName								m_debugOrgClass;

		ScriptableSnapshot( const IScriptable* object );
		~ScriptableSnapshot();
	};

protected:
	typedef red::DynArray< ScriptableSnapshot* > TObjectList;

	TObjectList m_scriptedObjects;		//!< Snapshots of scripted objects

public:
	CScriptSnapshot();
	~CScriptSnapshot();

	//! Create a snapshot, will clear all scripting data
	void CaptureScriptData( const red::DynArray< THandle< IScriptable > >& allScriptables );

	//! Restore scripting data
	void RestoreScriptData();

public:
	//! Build single object snapshot of editable properties
	ScriptableSnapshot* BuildEditorObjectSnapshot( const IScriptable* scriptedObject );

	//! Restore snapshot of single an object
	void RestoreEditorObjectSnapshot( IScriptable* scriptedObject, const ScriptableSnapshot* snapshot );

protected:
	//! Build a snapshot for a property
	PropertySnapshot* BuildPropertySnapshot( const IScriptable* scriptedObject, const rtti::IType* type, const void* data );

	//! Build a script snapshot for an object
	ScriptableSnapshot* BuildObjectSnapshot( const IScriptable* scriptedObject );

	//! Restore property snapshot
	void RestorePropertySnapshot( IScriptable* object, void* data, const rtti::IType* type, const PropertySnapshot* snapshot );

	//! Restore properties from object snapshot
	void RestoreObjectSnapshot( const ScriptableSnapshot* snapshot );	
};
