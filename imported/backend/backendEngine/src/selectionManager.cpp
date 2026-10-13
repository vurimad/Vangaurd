/**
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../include/selectionManager.h"
#include "../include/objectCollection.h"

namespace tools
{
	SelectionManager::SelectionManager( IEditActionHistory& actionHistory )
		: m_actionHistory( actionHistory )
	{
		CreateSelectionSource( consts::selection, red::CreateUniquePtr< ObjectCollection >( m_actionHistory ) );
	}

	ObjectCollection* SelectionManager::GetSharedSelection( CName id )
	{
		if ( !m_localSelections.KeyExist( id ) )
		{
			CreateSelectionSource( id, red::CreateUniquePtr< SharedObjectCollection >( this, m_actionHistory, m_localSelections[ consts::selection ].Get() ) );
		}

		return m_localSelections[ id ].Get();
	}

	void SelectionManager::ExposeInterop( ::interop::utils::Object& op )
	{
		op.RegisterMap( "selection", [this]( interop::IContext& ctx, const String& idStr )
		{
			const CName id = RED_NAME( idStr.AsChar() );
			return GetSharedSelection( id )->GetRootInterop();
		} );

		op.RegisterMap( "pages", [this]( interop::IContext& ctx, const String& idStr )
		{
			const Uint32 id = FromStringDirect< Uint32 >( idStr );
			if ( HasPageWithId( id ) )
			{
				PropertyPageInfo& pageInstance = GetPage( id );
				return pageInstance.CreateInterop();
			}

			return interop::ObjectPtr();
		} );

		op.RegisterFunctor< void, Uint32 >( "addPage", [this]( interop::IContext& ctx, const Uint32 pageId )
		{
			AddPage( pageId );
		} );

		op.RegisterFunctor< void, Uint32, Uint32 >( "duplicatePage", [this]( interop::IContext& ctx, const Uint32 pageId, const Uint32 newPageId )
		{
			DuplicatePage( pageId, newPageId );
		} );

		op.RegisterFunctor< Uint32, Uint32 >( "removePage", [this]( interop::IContext& ctx, const Uint32 pageId )
		{
			return RemovePage( pageId );
		} );

		op.RegisterFunctor< void, Uint32 >( "activatePage", [this]( interop::IContext& ctx, const Uint32 pageId )
		{
			ActivatePage( pageId );
		} );
	}

	const red::DynArray< CName >& SelectionManager::GetSelectionSources() const
	{
		return m_selectionSources;
	}

	void SelectionManager::ResetSharedViews()
	{
		ResetViewsForActiveSource( consts::selection );
	}

	void SelectionManager::RefreshSharedViews()
	{
		RefreshViewsForActiveSource( consts::selection );
	}

	void SelectionManager::UpdateSharedViews( const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		SetViewsForActiveSource( consts::selection, views );
	}

	void SelectionManager::SetViews( const ObjectCollection* collection, const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		const auto pair = std::find_if( m_localSelections.Begin(), m_localSelections.End(), [collection]( const auto& pair ) { return pair.Value().Get() == collection; } );
		const CName& sourceName = pair.Key();

		SetViewsForActiveSource( consts::selection, views );
		SetViewsForActiveSource( sourceName, views );
	}

	void SelectionManager::SetViewsForActiveSource( const CName& sourceName, const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		for ( PropertyPageInfo& page : m_pageInstances )
		{
			if ( page.m_sourceName == sourceName && page.m_isActive )
			{
				page.m_selection->SetViews( views );
			}
		}
	}

	void SelectionManager::RefreshViewsForActiveSource( const CName& sourceName )
	{
		for ( PropertyPageInfo& page : m_pageInstances )
		{
			if ( page.m_sourceName == sourceName && page.m_isActive )
			{
				page.m_selection->Refresh();
			}
		}
	}

	void SelectionManager::ResetViewsForActiveSource( const CName& sourceName )
	{
		for ( PropertyPageInfo& page : m_pageInstances )
		{
			if ( page.m_sourceName == sourceName && page.m_isActive )
			{
				page.m_selection->Reset();
			}
		}
	}

	void SelectionManager::CreateSelectionSource( const CName& sourceName, red::UniquePtr< ObjectCollection >&& objectCollection )
	{
		m_selectionSources.PushBack( sourceName );
		m_localSelections.Insert( sourceName, std::move( objectCollection ) );
	}

	void SelectionManager::AddPage( const Uint32 pageId )
	{
		const CName sourceName = RED_NAME_CONSTEXPR( "Selection" );

		DeactivatePages();

		if ( HasPageWithId( pageId ) )
		{
			PropertyPageInfo& pageInfo = GetPage( pageId );
			pageInfo.m_isActive = true;
		}
		else
		{
			PropertyPageInfo pageInfo;

			pageInfo.m_id = pageId;
			pageInfo.m_manager = this;
			pageInfo.m_sourceName = sourceName;

			pageInfo.m_isActive = true;

			red::UniquePtr< ObjectCollection > pageCollection = red::CreateUniquePtr< ObjectCollection >( m_actionHistory );
			pageCollection->SetViews( GetSharedSelection( sourceName )->GetViews() );
			pageInfo.m_selection = pageCollection.Get();

			m_pageCollections.Insert( pageId, std::move( pageCollection ) );
			m_pageInstances.PushBack( std::move( pageInfo ) );
		}
	}

	void SelectionManager::DuplicatePage( const Uint32 pageId, const Uint32 newPageId )
	{
		DeactivatePages();

		if ( HasPageWithId( newPageId ) )
		{
			PropertyPageInfo& pageInfo = GetPage( newPageId );
			pageInfo.m_isActive = true;
		}
		else
		{
			PropertyPageInfo pageInfo = GetPage( pageId );

			pageInfo.m_id = newPageId;
			pageInfo.m_isActive = true;

			red::UniquePtr< ObjectCollection > pageCollection = red::CreateUniquePtr< ObjectCollection >( m_actionHistory );
			pageCollection->SetViews( GetSharedSelection( pageInfo.m_sourceName )->GetViews() );
			pageInfo.m_selection = pageCollection.Get();

			m_pageCollections.Insert( newPageId, std::move( pageCollection ) );
			m_pageInstances.PushBack( std::move( pageInfo ) );
		}
	}

	Uint32 SelectionManager::RemovePage( const Uint32 pageId )
	{
		Uint32 previouslyActivePageId = GetActivePageId();

		DeactivatePages();

		auto foundIt = std::find_if( m_pageInstances.Begin(), m_pageInstances.End(), [pageId]( const PropertyPageInfo& pageInfo ) { return pageInfo.m_id == pageId; } );

		if ( foundIt != m_pageInstances.End() )
		{
			m_pageCollections.Remove( pageId );
			m_pageInstances.RemoveReorder( foundIt );

			previouslyActivePageId = UpdatePageIds( previouslyActivePageId );

			if ( previouslyActivePageId == pageId && !m_pageInstances.Empty() )
			{
				PropertyPageInfo& pageInfo = m_pageInstances[ 0 ];
				pageInfo.m_isActive = true;
				return pageInfo.m_id;
			}
		}

		return previouslyActivePageId;
	}

	void SelectionManager::ActivatePage( const Uint32 pageId )
	{
		DeactivatePages();

		auto foundIt = std::find_if( m_pageInstances.Begin(), m_pageInstances.End(), [pageId]( const PropertyPageInfo& pageInfo ) { return pageInfo.m_id == pageId; } );

		if ( foundIt != m_pageInstances.End() )
		{
			foundIt->m_isActive = true;
		}
	}

	void SelectionManager::DeactivatePages()
	{
		for ( PropertyPageInfo& pageInfo : m_pageInstances )
		{
			if ( pageInfo.m_isActive )
			{
				pageInfo.m_isActive = false;
			}
		}
	}

	Uint32 SelectionManager::GetActivePageId() const
	{
		const auto foundIt = std::find_if( m_pageInstances.Begin(), m_pageInstances.End(), []( const PropertyPageInfo& pageInfo ) { return pageInfo.m_isActive; } );
		return foundIt != m_pageInstances.End() ? foundIt->m_id : -1;
	}

	Bool SelectionManager::IsPageActive( const Uint32 pageId ) const
	{
		return m_pageInstances.End() != std::find_if( m_pageInstances.Begin(), m_pageInstances.End(), [pageId]( const PropertyPageInfo& pageInfo ) { return pageInfo.m_id == pageId && pageInfo.m_isActive; } );
	}

	Bool SelectionManager::HasPageWithId( const Uint32 id ) const
	{
		return m_pageInstances.End() != std::find_if( m_pageInstances.Begin(), m_pageInstances.End(), [id]( const PropertyPageInfo& pageInfo ) { return pageInfo.m_id == id; } );
	}

	PropertyPageInfo& SelectionManager::GetPage( const Uint32 id )
	{
		for ( PropertyPageInfo& pageInfo : m_pageInstances )
		{
			if ( pageInfo.m_id == id )
				return pageInfo;
		}

		RED_FATAL( "Unable to find page instance with specified id!" );
		static PropertyPageInfo empty;
		return empty;
	}

	Uint32 SelectionManager::UpdatePageIds( const Uint32 previouslySelectedPageId )
	{
		Uint32 remappedPreviouslySelectedPageId = previouslySelectedPageId;
		Uint32 id = 0;
		red::HashMap< Uint32, red::UniquePtr< ObjectCollection > >  tempPageCollections = std::move( m_pageCollections );

		for ( PropertyPageInfo& pageInstance : m_pageInstances )
		{
			const Uint32 oldId = pageInstance.m_id;
			const Uint32 newId = id++;
			if ( oldId == previouslySelectedPageId )
			{
				remappedPreviouslySelectedPageId = newId;
			}
			m_pageCollections.Insert( newId, std::move( tempPageCollections[ oldId ] ) );
			pageInstance.m_id = newId;
		}

		return remappedPreviouslySelectedPageId;
	}

	void PropertyPageInfo::SetupInterop( PropertyPageInfo* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "selection", ptr->m_selection->GetRootInterop() );
	}
} // namespace tools
