/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptableCyclesDetector.h"
#include "../../redContainers/include/queue.h"
#include "scriptable.h"
#include "rttiPointerTypesImpl.h"

using namespace debug;

//////////////////////////////////////////////////////////////////////////

red::DynArray< const IScriptable* > ScriptableCyclesDetector::s_registeredObjects{ red::PoolDebug() };

//////////////////////////////////////////////////////////////////////////

void ScriptableCyclesDetector::Register( const IScriptable* object )
{
	RED_FATAL_ASSERT( !s_registeredObjects.Exist( object ), "IScriptable already registerd. Constructed twice?" );
	s_registeredObjects.PushBack( object );
}

void ScriptableCyclesDetector::Unregister( const IScriptable* object )
{
	RED_FATAL_ASSERT( s_registeredObjects.Exist( object ), "IScriptable not registered." );
	s_registeredObjects.Remove( object );
}

//////////////////////////////////////////////////////////////////////////

void ScriptableCyclesDetector::DetectCycles()
{
	struct Pred
	{
		const IScriptable*		m_obj;
		const rtti::Property*	m_prop;
	};

	const Uint32 totalObjects = s_registeredObjects.Size();
	red::Set< const IScriptable* > globallyVisited{ red::PoolDebug() };
	globallyVisited.Reserve( totalObjects );
	red::Set< const IScriptable*> locallyVisited{ red::PoolDebug() };
	red::DynArray< const rtti::Property* > props{ red::PoolDebug() };
	red::DynArray< Pred > cycle{ red::PoolDebug() };
	Uint32 cyclesNum = 0;

	for ( const IScriptable* root : s_registeredObjects )
	{
		if ( globallyVisited.Exist( root ) )
		{
			continue;
		}

		red::Queue< const IScriptable* > q{ red::PoolDebug() };
		q.Push( root );

		locallyVisited.Clear();
		red::HashMap< const IScriptable*, Pred > preds{ red::PoolDebug() };

		while ( !q.Empty() )
		{
			const IScriptable* parent = q.Pop();
			const rtti::ClassType* c = parent->GetClass();
			locallyVisited.Insert( parent );

			// enqueue all children
			props.Clear();
			c->GetProperties( props );
			for ( const rtti::Property* prop : props )
			{
				if ( prop->GetType()->GetType() != RT_Handle )
				{
					// Property is not a (strong) handle
					continue;
				}
				const rtti::HandleType* type = reinterpret_cast< const rtti::HandleType* >( prop->GetType() );
				if ( !type->GetPointedType()->IsA< IScriptable >() )
				{
					// Handle doesn't hold IScriptable type
					continue;
				}
				THandle< IScriptable > handle;
				prop->Get( parent, &handle );
				IScriptable* child = handle.Get();
				if ( child == nullptr )
				{
					continue;
				}
				if ( locallyVisited.Exist( child ) )
				{
					// We reached locally visited object, which means there's a cycle.
					cycle.Clear();
					cycle.PushBack( { parent, prop } );
					Pred pred;
					while ( preds.Find( parent, pred ) )
					{
						cycle.PushBack( pred );
						if ( pred.m_obj == child )
						{
							break;
						}
						parent = pred.m_obj;
					}
					RED_LOG( "Cycle found:" );
					for ( Pred& pred : cycle.Reverse() )
					{
						RED_LOG( "%s::%s : %s", pred.m_obj->GetClass()->GetName().AsChar(), pred.m_prop->GetName().AsChar(), pred.m_prop->GetType()->GetName().AsChar() );
					}
					cyclesNum++;
				}
				else if ( globallyVisited.Exist( child ) )
				{
					// We reached globally visited object, which means there's no cycle through that object
					// (otherwise we would reach "child" before), so we may skip visiting this branch.
					continue;
				}
				else
				{
					preds[ child ] = { parent, prop };
					q.Push( child );
				}
			}
		} // while( !q.Empty() )
		globallyVisited.Union( locallyVisited );
	}

	RED_LOG( "Number of cycles found: %d", cyclesNum );
	RED_LOG( "Visited objects %d, total objects %d", globallyVisited.Size(), s_registeredObjects.Size() );
}
