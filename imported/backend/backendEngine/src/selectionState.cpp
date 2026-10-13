/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "selectionState.h"
#include "selectionIterator.h"

namespace tools
{

	SelectionState::SelectionState()
		: m_proxyList{ red::PoolBackend() }
		, m_proxyMap{ red::PoolBackend() }
	{
	}

	SelectionStatePtr SelectionState::EmptySelection()
	{
		return red::CreateSharedPtr< SelectionState >();
	}

	SelectionStatePtr SelectionState::Clone() const
	{
		SelectionStatePtr ret = SelectionState::EmptySelection();
		ret->m_proxyList = m_proxyList;
		ret->m_proxyMap = m_proxyMap;
		return ret;
	}

	SelectionIterator SelectionState::Iterate( const CName selectionFilter /*= CName::NONE()*/ ) const
	{
		return SelectionIterator( selectionFilter, this );
	}

	Bool SelectionState::Restore()
	{
		for ( auto obj : m_proxyList )
			if ( !obj->RestoreObject() )
				return false;

		return true;
	}

	void SelectionState::Clear()
	{
		m_proxyList.Clear();
		m_proxyMap.Clear();
	}

	void SelectionState::Add( EditableProxyPtr ptr )
	{
		if ( ptr )
		{
			if ( !m_proxyMap.KeyExist( ptr->GetEditorObjectIDPath() ) )
			{
				m_proxyList.PushBack( ptr );
				m_proxyMap.Insert( ptr->GetEditorObjectIDPath(), ptr );
			}
		}
	}

	void SelectionState::Remove( const EditorObjectIDPath& path )
	{
		if ( !path.IsEmpty() )
		{
			auto it = m_proxyMap.Find( path );
			if ( it != m_proxyMap.End() )
			{
				m_proxyMap.Remove( it );
				m_proxyList.Remove( it.Value() );
			}
		}
	}

	Bool SelectionState::operator == ( const SelectionState& other ) const
	{
		if ( m_proxyList.Size() != other.Size() )
			return false;

		for ( const auto& item : m_proxyMap )
		{
			if ( !other.Contains( item.Key() ) )
				return false;
		}

		for ( const auto& item : other.m_proxyMap )
		{
			if ( !Contains( item.Key() ) )
				return false;
		}

		return true;
	}

	Bool SelectionState::operator != ( const SelectionState& other ) const
	{
		return !( *this == other );
	}

	bool SelectionState::Contains( const EditorObjectIDPath& path ) const
	{
		return m_proxyMap.KeyExist( path );
	}

	const Uint32 SelectionState::Size() const
	{
		return m_proxyList.Size();
	}

	void SelectionState::ListObjects( red::DynArray< EditorObjectIDPath >& paths ) const
	{
		paths.Reserve( paths.Size() );

		for ( const auto& item : m_proxyList )
		{
			paths.PushBack( item->GetEditorObjectIDPath() );
		}
	}

	void SelectionState::ListObjects( red::Set< EditorObjectIDPath >& paths ) const
	{
		paths.Reserve( paths.Size() );

		for ( const auto& item : m_proxyMap )
		{
			paths.Insert( item.Key() );
		}
	}

	Bool SelectionState::HasCommonParent() const
	{
		// empty set of objects has common root :)
		if ( IsEmpty() )
			return true;

		tools::EditorObjectIDPath referencePath;

		// get the common root
		Bool referencePathIsSet = false;
		for ( const auto& item : m_proxyMap )
		{
			const EditorObjectIDPath& path = item.Key();
			RED_ASSERT( !path.IsEmpty() );
			if ( path.IsEmpty() )
				continue;

			if ( !referencePathIsSet )
			{
				// use the first valid path as a reference for others
				referencePath = path;
				referencePathIsSet = true;
			}
			else
			{
				// make sure other objects are under the same root
				if ( !EditorObjectIDPath::HasCommonParent( referencePath, path ) )
				{
					return false;
				}
			}
		}

		return true;
	}

	EditorObjectIDPath SelectionState::GetCommonParent() const
	{
		if ( IsEmpty() || !HasCommonParent() )
		{
			return EditorObjectIDPath();
		}
		
		EditorObjectIDPath result = m_proxyList.Front()->GetEditorObjectIDPath();
		result.RemoveLastElements( 1 );
		return result;
	}

	Bool SelectionState::IsReadOnly() const
	{
		if ( m_proxyList.Empty() )
			return true;

		// whole selection state is read-only if at least one editable proxy is read-only
		for ( const EditableProxyPtr& proxy : m_proxyList )
		{
			RED_FATAL_ASSERT( proxy );
			if ( proxy->IsReadOnly() )
				return true;
		}

		return false;
	}

	void SelectionState::RemoveAll( const CName type )
	{
		auto keys = m_proxyMap.GetKeys();
		for( auto & key : keys )
		{
			if( key.GetFinalChildID().GetKind() == type )
			{
				Remove( key );
			}
		}
	}

} // tools