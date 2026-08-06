/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "scriptSnapshot.h"
#include "scriptingSystem.h"
#include "rttiArrayTypes.h"
#include "rttiArrayTypesImpl.h"

#include "../../redCore/include/profiler.h"
#include "../../redSystem/include/stopWatch.h"
#include "scriptable.h"


using red::DynArray;


CScriptSnapshot::ScriptableSnapshot::ScriptableSnapshot( const IScriptable* object )
:	m_scriptable( object )
,	m_properties( red::PoolScript() )
,	m_states( red::PoolScript() )
,	m_debugOrgAddress( object )
,	m_debugOrgClass( object->GetClass()->GetName() )
{
}

CScriptSnapshot::ScriptableSnapshot::~ScriptableSnapshot()
{
	red::alg::ClearPtr( m_properties );
}

CScriptSnapshot::CScriptSnapshot()
	: m_scriptedObjects( red::PoolScript() )
{
}

CScriptSnapshot::~CScriptSnapshot()
{
	red::alg::ClearPtr( m_scriptedObjects );
}

void CScriptSnapshot::CaptureScriptData( const DynArray< THandle< IScriptable > >& allScriptableObjects )
{
	red::StopWatch timeCounter;

	// Delete current snapshots
	red::alg::ClearPtr( m_scriptedObjects );

	// Process all scriptables
	Uint32 numPropertySnapshots = 0;
	for ( Uint32 i=0; i<allScriptableObjects.Size(); ++i )
	{
		IScriptable* scriptable = allScriptableObjects[i].Get();
		if ( nullptr != scriptable )
		{
			// Inform object
			scriptable->OnScriptPreCaptureSnapshot();

			// Build snapshots from object
			ScriptableSnapshot* snapshot = BuildObjectSnapshot( scriptable );
			if ( nullptr != snapshot )
			{
				numPropertySnapshots += snapshot->m_properties.Size();
				m_scriptedObjects.PushBack( snapshot );
			}

			// Inform object
			scriptable->OnScriptPostCaptureSnapshot();
		}
	}

	// Show stats
	RED_LOG( "Core: Snapshot from %i scriptables ( %i properties ) built in %1.2fs", allScriptableObjects.Size(), numPropertySnapshots, timeCounter.GetDelta() );
}

void CScriptSnapshot::RestorePropertySnapshot( IScriptable* scriptedObject, void* data, const rtti::IType* type, const PropertySnapshot* snapshot )
{
	// Simple type
	ERTTITypeType typeType = type->GetType();

	if ( typeType == RT_Name || typeType == RT_Enum || typeType == RT_Simple || typeType == RT_BitField || typeType == RT_Fundamental )
	{
		{
			Bool res = type->FromString( data, snapshot->m_valueString );
			if( !res )
			{
				RED_LOG_WARNING( "Core: RestorePropertySnapshot FromString error for property %hs", snapshot->m_name.AsChar() );
			}
		}
	}

	// Arrays
	else if ( typeType == RT_Array )
	{
		// Get the array type
		const rtti::ArrayType* arrayType = static_cast< const rtti::ArrayType* >( type );
		const rtti::IType* innerType = arrayType->GetInnerType();

		// Clean current array
		arrayType->Destruct( data );
		arrayType->Construct( data );

		// Create array
		const Uint32 numElements = snapshot->m_subValues.Size();
		if( numElements > 0 )
		{
			arrayType->AddArrayElement( data, numElements, red::PoolScript() );

			// Add elements
			for ( Uint32 i=0; i<numElements; i++ )
			{
				void* elementData = arrayType->GetArrayElement( data, i );
				RestorePropertySnapshot( scriptedObject, elementData, innerType, snapshot->m_subValues[i] );
			}
		}
	}

	// Structure
	else if ( typeType == RT_Class )
	{
		const rtti::ClassType* pointedClass = static_cast< const rtti::ClassType* >( type );

		// Restore properties
		for ( Uint32 i=0; i<snapshot->m_subValues.Size(); i++ )
		{
			// Find property to restore
			PropertySnapshot* subSnapshot = snapshot->m_subValues[i];			
			const rtti::Property* prop = pointedClass->FindProperty( subSnapshot->m_name );
			if ( prop )
			{
				// Get the target data
				void* propData = prop->GetOffsetPtr( data );
				RestorePropertySnapshot( scriptedObject, propData, prop->GetType(), subSnapshot );
			}
		}
	}

	// Handle
	else if ( typeType == RT_Handle )
	{
		type->Copy( data, &snapshot->m_valueHandle );
	}
	else if ( typeType == RT_WeakHandle )
	{
		WeakHandle< IScriptable > weak = snapshot->m_valueHandle;
		type->Copy( data, &weak );
	}
}

void CScriptSnapshot::RestoreScriptData()
{
	// Restore snapshot of property values
	for ( Uint32 i = 0; i < m_scriptedObjects.Size(); ++i )
	{
		ScriptableSnapshot* snapshot = m_scriptedObjects[ i ];
		RestoreObjectSnapshot( snapshot );
	}
}

void CScriptSnapshot::RestoreObjectSnapshot( const ScriptableSnapshot* snapshot )
{
	// Get the snapshotted object
	IScriptable* scriptedObject = snapshot->m_scriptable.Get();
	if ( scriptedObject )
	{
		// Process properties
		for ( Uint32 j = 0; j < snapshot->m_properties.Size(); ++j )
		{
			const PropertySnapshot* propSnapshot = snapshot->m_properties[j];

			// Find property in the object
			const rtti::ClassType* objectClass = scriptedObject->GetClass();
			const rtti::Property* prop = objectClass->FindProperty( propSnapshot->m_name );
			if ( !prop )
			{
				RED_LOG( "Core: Unable to restore property '%hs' from snapshot of object '%hs'.", 
					propSnapshot->m_name.AsChar(), 
					scriptedObject->GetFriendlyName().AsChar() );
				continue;
			}

			// Property is no longer scripted
			if ( !prop->IsScripted() )
			{
				RED_LOG( "Core: Snapshot property '%hs' from object '%hs' is no longer scripted.", 
					propSnapshot->m_name.AsChar(), 
					scriptedObject->GetFriendlyName().AsChar() );

				continue;
			}

			// Get the data and restore property
			void* data = prop->GetOffsetPtr( scriptedObject );
			RestorePropertySnapshot( scriptedObject, data, prop->GetType(), propSnapshot );
		}
	}
}

CScriptSnapshot::PropertySnapshot* CScriptSnapshot::BuildPropertySnapshot( const IScriptable* scriptedObject, const rtti::IType* type, const void* data )
{
	// Simple type
	ERTTITypeType typeType = type->GetType();

	if ( typeType == RT_Name || typeType == RT_Enum || typeType == RT_Simple || typeType == RT_BitField || typeType == RT_Fundamental )
	{
		// Get the string value
		String valueString;
		if ( !type->ToString( data, valueString ) )
		{
			RED_LOG_WARNING( "Core: Unable to export snapshot of '%hs'", type->GetName().AsChar() );
			return NULL;
		}
		
		// Save as simple property snapshot
		PropertySnapshot* snapshot = RED_NEW( PropertySnapshot );
		snapshot->m_valueString = valueString;
		return snapshot;
	}

	// Handles
	if ( typeType == RT_Handle )
	{
		// Get handle value
		THandle< IScriptable > handle;
		type->Copy( &handle, data );

		// Save as simple property snapshot
		PropertySnapshot* snapshot = RED_NEW( PropertySnapshot );
		snapshot->m_valueHandle = handle;
		return snapshot;
	}
	if ( typeType == RT_WeakHandle )
	{
		// Get handle value
		WeakHandle< IScriptable > weak;
		type->Copy( &weak, data );

		// Save as simple property snapshot
		PropertySnapshot* snapshot = RED_NEW( PropertySnapshot );
		snapshot->m_valueHandle = weak.ToHandle();
		return snapshot;
	}

	// Dynamic array
	if ( typeType == RT_Array )
	{
		// Get the inner type of array
		const rtti::ArrayType* arrayType = static_cast< const rtti::ArrayType* >( type );
		const rtti::IType* innerType = arrayType->GetInnerType();
		PropertySnapshot* snapshot = RED_NEW( PropertySnapshot );

		// Create snapshots of sub elements
		const Uint32 numElements = arrayType->GetArraySize( data );
		for ( Uint32 i=0; i<numElements; i++ )
		{
			// Get element data
			const void* elementData = arrayType->GetArrayElement( data, i );

			// Create element snapshot
			PropertySnapshot* elementSnapshot = BuildPropertySnapshot( scriptedObject, innerType, elementData );
			if ( !elementSnapshot )
			{
				RED_LOG_WARNING( "Core: Unable to export snapshot of #%i in '%hs'", i, 
					type->GetName().AsChar() );

				RED_DELETE( snapshot );
				return NULL;
			}

			// Add to element snapshot
			snapshot->m_subValues.PushBack( elementSnapshot );
		}
		
		// Return created snapshot
		return snapshot;
	}

	// Structure
	if ( typeType == RT_Class )
	{
		// Get properties
		DynArray< const rtti::Property* > subProperties{ red::PoolScript() };
		(( const rtti::ClassType* ) type )->GetProperties( subProperties );

		// Create base snapshot
		PropertySnapshot* snapshot = RED_NEW( PropertySnapshot );

		// Create sub properties snapshots
		for ( Uint32 i = 0; i < subProperties.Size(); ++i ) 
		{
			const rtti::Property* subProperty = subProperties[i];
			
			// Collect only scripted properties
			if ( !subProperty->IsScripted() )
			{
				continue;
			}

			// Create sub-property snapshot
			const void* subPropertyData = subProperty->GetOffsetPtr( data );
			PropertySnapshot* subSnapshot = BuildPropertySnapshot( scriptedObject, subProperty->GetType(), subPropertyData );
			if ( !subSnapshot )
			{
				RED_DELETE( snapshot );
				return NULL;
			}

			// Name it and add to base property snapshot
			subSnapshot->m_name = subProperty->GetName();
			snapshot->m_subValues.PushBack( subSnapshot );
		}
		return snapshot;
	}

	// Invalid type
	RED_HALT( "Invalid type" );

	return NULL;
}

CScriptSnapshot::ScriptableSnapshot* CScriptSnapshot::BuildObjectSnapshot( const IScriptable* scriptedObject )
{
	// Get object class
	const rtti::ClassType* objectClass = scriptedObject->GetClass();
	const void* defaultObject = objectClass->GetDefaultObject();

	// Get properties
	DynArray< const rtti::Property* > properties{ red::PoolScript() };
	objectClass->GetProperties( properties );

	// Remember value of script properties that are different than from default value
	ScriptableSnapshot* objectSnapshot = nullptr;
	for ( Uint32 i = 0; i < properties.Size(); ++i )
	{
		const rtti::Property* prop = properties[i];
		if ( prop->IsScripted() )
		{
			const void* baseValue = prop->GetOffsetPtr( scriptedObject );
			const void* defaultValue = prop->GetOffsetPtr( defaultObject );

			// Skip property if it has the same value as in the base object
			if ( prop->GetType()->Compare( baseValue, defaultValue, 0 ) )
			{
				continue;
			}

			// Build property snapshot
			PropertySnapshot* propSnapshot = BuildPropertySnapshot( scriptedObject, prop->GetType(), baseValue );
			if ( propSnapshot )
			{
				// Create the object snapshot if not already created
				if ( !objectSnapshot )
				{
					objectSnapshot = RED_NEW( ScriptableSnapshot )( scriptedObject );
				}

				// Name it and add to the object snapshot
				propSnapshot->m_name = prop->GetName(); 
				objectSnapshot->m_properties.PushBack( propSnapshot );
			}
		}
	}

	// Return created or not object snapshot
	return objectSnapshot;
}

CScriptSnapshot::ScriptableSnapshot* CScriptSnapshot::BuildEditorObjectSnapshot( const IScriptable* scriptableObject )
{
	// Get object class
	const rtti::ClassType* objectClass = scriptableObject->GetClass();

	// Get properties
	DynArray< const rtti::Property* > properties{ red::PoolScript() };
	objectClass->GetProperties( properties );

	// Remember value of script properties that are different than from default value
	ScriptableSnapshot* objectSnapshot = NULL;
	for ( Uint32 i = 0; i < properties.Size(); ++i )
	{
		const rtti::Property* prop = properties[i];
		if ( prop->IsEditable() )
		{
			const void* baseValue = prop->GetOffsetPtr( scriptableObject );

			// Build property snapshot
			PropertySnapshot* propSnapshot = BuildPropertySnapshot( scriptableObject, prop->GetType(), baseValue );
			if ( propSnapshot )
			{
				// Create the object snapshot if not already created
				if ( !objectSnapshot )
				{
					objectSnapshot = RED_NEW( ScriptableSnapshot )( scriptableObject );
				}

				// Name it and add to the object snapshot
				propSnapshot->m_name = prop->GetName(); 
				objectSnapshot->m_properties.PushBack( propSnapshot );
			}
		}
	}

	// Return created or not object snapshot
	return objectSnapshot;
}

void CScriptSnapshot::RestoreEditorObjectSnapshot( IScriptable* scriptableObject, const ScriptableSnapshot* snapshot )
{
	// Process properties
	for ( Uint32 j=0; j<snapshot->m_properties.Size(); j++ )
	{
		const PropertySnapshot* propSnapshot = snapshot->m_properties[j];

		// Find property in the object
		const rtti::ClassType* objectClass = scriptableObject->GetClass();
		const rtti::Property* prop = objectClass->FindProperty( propSnapshot->m_name );
		if ( !prop )
		{
			RED_LOG( "Core: Unable to restore property '%hs' from snapshot of object '%hs'.", 
				propSnapshot->m_name.AsChar(), 
				scriptableObject->GetFriendlyName().AsChar() );

			continue;
		}

		// Property is no longer scripted ? WTF
		if ( !prop->IsEditable() )
		{
			RED_LOG( "Core: Snapshot property '%hs' from object '%hs' is no longer editable.", 
				propSnapshot->m_name.AsChar(), 
				scriptableObject->GetFriendlyName().AsChar() );

			continue;
		}

		// Get the data and restore property
		void* data = prop->GetOffsetPtr( scriptableObject );
		RestorePropertySnapshot( scriptableObject, data, prop->GetType(), propSnapshot );
	}
}
