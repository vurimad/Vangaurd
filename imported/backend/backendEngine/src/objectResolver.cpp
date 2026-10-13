/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectResolver.h"
#include "objectActionDeleteObjects.h"
#include "selectionState.h"
#include "objectPersistentCollection.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IObjectResolver, tools )
RTTI_END_TYPE()

namespace tools
{

	IObjectResolver::IObjectResolver()
		: m_handler(nullptr)
	{}

	IObjectResolver::~IObjectResolver() = default;

	void IObjectResolver::AttachedToHandler( ObjectHandler* objectHandler )
	{
		RED_FATAL_ASSERT( m_handler == nullptr, "Resolver already attached to handler" );
		m_handler = objectHandler;
	}

	void IObjectResolver::DetachedFromHandler( ObjectHandler* objectHandler )
	{
		RED_FATAL_ASSERT( m_handler == objectHandler, "Resolver not attached to a proper handler" );
		m_handler = nullptr;
	}

	Bool IObjectResolver::IsExclusive() const
	{
		return false;
	}

	EditorObjectIDPath IObjectResolver::GetActiveObjectIDPath() const
	{
		return EditorObjectIDPath();
	}
	
	void IObjectResolver::CollectResolvableObjects( red::DynArray< EditorObjectIDPath >& outPaths ) const
	{
		// Default resolver does not return anything.
		// Implement this function to support "select all" feature.

		RED_UNUSED( outPaths );
	}

	red::UniquePtr< IEditAction > IObjectResolver::GetPreDeleteAction( const red::ArraySpan< const EditorObjectIDPath >& paths ) const
	{
		RED_UNUSED( paths );
		return nullptr;
	}

	void IObjectResolver::FindSimilarObjects( const EditorObjectIDPath& templateObjectPath, const String& settings, red::DynArray< EditorObjectIDPath >& outSimilarObjectIds ) const
	{
	}

	EditorObjectIDPath IObjectResolver::FindNeighboringObject( const EditorObjectIDPath& currentObjectPath, Bool forward ) const
	{
		return EditorObjectIDPath();
	}

	red::UniquePtr< IEditAction > IObjectResolver::CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const
	{
		return red::CreateUniquePtr< tools::Action_DeleteObjects >( objectHandler, paths );
	}

	Bool IObjectResolver::CanUngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, String* outReason ) const
	{
		RED_UNUSED2( paths, outReason );
		return false;
	}

	Bool IObjectResolver::Action_UngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths )
	{
		RED_UNUSED( paths );
		return false;
	}

	Bool IObjectResolver::Action_GroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds )
	{
		RED_UNUSED2( paths, groupIds );
		return false;
	}
} // tools

