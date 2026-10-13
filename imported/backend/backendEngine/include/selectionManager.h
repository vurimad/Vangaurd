/**
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace interop
{
	namespace utils
	{
		class Object;
	}
}

namespace tools
{
	class IEditActionHistory;
	class ObjectCollection;

	namespace consts
	{
		const CName selection = RED_NAME_CONSTEXPR( "Selection" );
	}

	class SelectionManager;
	class PropertyPageInfo
	{
		DECLARE_INTEROP( PropertyPageInfo );
		RED_USE_MEMORY_POOL( red::PoolBackend );

		SelectionManager* m_manager;
		Bool m_isActive;
		CName m_sourceName;
		Uint32 m_id;
		ObjectCollection* m_selection;

		Bool operator==( const PropertyPageInfo& other )
		{
			return m_id == other.m_id;
		}
	};

	class SelectionManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		SelectionManager( IEditActionHistory& actionHistory );

		/// Get one object collection to handle global selection that is displayed in main property grid in the editor frontend
		ObjectCollection* GetSharedSelection( CName id = consts::selection );

		void ExposeInterop( ::interop::utils::Object& op );

		const red::DynArray< CName >& GetSelectionSources() const;

		void ResetSharedViews();
		void RefreshSharedViews();
		void UpdateSharedViews( const red::DynArray< red::SharedPtr< IEditableView > >& views );
		void SetViews( const ObjectCollection* collection, const red::DynArray< red::SharedPtr< IEditableView > >& views );

	private:
		void SetViewsForActiveSource( const CName& sourceName, const red::DynArray< red::SharedPtr< IEditableView > >& views );
		void RefreshViewsForActiveSource( const CName& sourceName );
		void ResetViewsForActiveSource( const CName& sourceName );
		void CreateSelectionSource( const CName& sourceName, red::UniquePtr< ObjectCollection >&& objectCollection );
		void AddPage( const Uint32 pageId );
		void DuplicatePage( const Uint32 pageId, const Uint32 newPageId );
		Uint32 RemovePage( const Uint32 pageId );
		void ActivatePage( const Uint32 pageId );
		void DeactivatePages();

		Uint32 GetActivePageId() const;
		Bool IsPageActive( const Uint32 pageId ) const;
		Bool HasPageWithId( const Uint32 id ) const;
		PropertyPageInfo& GetPage( const Uint32 id );

		Uint32 UpdatePageIds( const Uint32 previouslySelectedPageId );

		IEditActionHistory& m_actionHistory;

		// list of selection for each registered control
		red::HashMap< CName, red::UniquePtr< ObjectCollection > > m_localSelections{ red::PoolBackend() };
		red::DynArray< CName > m_selectionSources{ red::PoolBackend() };

		red::DynArray< PropertyPageInfo > m_pageInstances{ red::PoolBackend() };
		red::HashMap< Uint32, red::UniquePtr< ObjectCollection > > m_pageCollections{ red::PoolBackend() };
	};
} // namespace tools
