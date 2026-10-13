/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderData/include/debugFilterMask.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace rend
{
	class DebugFilterHierarchyNode;
}

namespace tools
{
	class PreviewFilters;
	class DebugFilterTree;
	class ITreeNode;
	class FavoriteFilterTreeNode;

	typedef Uint64 TreeNodeID;

	class DebugFilterDataContainerItem : public ISerializable
	{
		RTTI_DECLARE_TYPE( DebugFilterDataContainerItem );
		RED_BASE_CLASS( ISerializable );

	public:
		DebugFilterDataContainerItem();
		DebugFilterDataContainerItem( const String name );
		virtual ~DebugFilterDataContainerItem() override;

		Bool m_defaultState;
		Bool m_deprecated;
		String m_description;
		String m_name;
	};

	class DebugFilterDataContainer : public ISerializable
	{
		RTTI_DECLARE_TYPE( DebugFilterDataContainer );
		RED_BASE_CLASS( ISerializable );

	public:
		DebugFilterDataContainer();
		virtual ~DebugFilterDataContainer() override;

		red::DynArray< DebugFilterDataContainerItem > m_items;
	};

	// single filter value
	class BACKEND_ENGINE_API PreviewFiltersNodeValue : public interop::IValueObject
	{
	public:
		PreviewFiltersNodeValue(PreviewFilters* root, const rend::DebugFilterHierarchyNode* node);

		virtual const CName GetType() const override;
		virtual void Get(interop::IContext& ctx, interop::InteropString& outValue) const override final;
		virtual void Set(interop::IContext& ctx, const interop::InteropString& newValue) const override final;

		virtual const Bool IsReadOnly() const override final;
		virtual const Uint8 IsDefault() const override final;

		RED_FORCE_INLINE const CName GetName() const
		{
			return m_name;
		}

	private:
		CName										m_name;

		PreviewFilters*								m_root;
		const rend::DebugFilterHierarchyNode*		m_node;
	};

	// group filter
	class BACKEND_ENGINE_API PreviewFiltersNode : public interop::ICompoundObject
	{
	public:
		virtual const CName GetType() const override;

		PreviewFiltersNode(PreviewFilters* root, const rend::DebugFilterHierarchyNode* node);

		virtual void GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const override final;

		virtual interop::ObjectPtr GetChild(interop::IContext& ctx, CName name) const override final;

		RED_FORCE_INLINE const CName GetName() const
		{
			return m_name;
		}

	private:
		CName										m_name;

		PreviewFilters*								m_root;
		PreviewFiltersNode*							m_parent;
		const rend::DebugFilterHierarchyNode*		m_node;

		typedef red::DynArray< red::SharedPtr< PreviewFiltersNode > > TGroupChildren;
		mutable TGroupChildren						m_groups;

		typedef red::DynArray< red::SharedPtr< PreviewFiltersNodeValue > > TFilterChildren;
		mutable TFilterChildren						m_filters;

		void ConditionalFillChildren() const;
	};

	/// an interop wrapper for the DebugFilter hierarchy
	class BACKEND_ENGINE_API PreviewFilters
	{
		DECLARE_INTEROP( PreviewFilters );

	public:
		PreviewFilters();
		~PreviewFilters();

		// bind filters
		typedef std::function< void(rend::DebugFilterMask&) > TGetFilters;
		typedef std::function< void(const rend::DebugFilterMask&, TreeNodeID, TreeNodeID ) > TSetFilters;
		void Bind( const AnsiChar* rootCategory, const String& editorName, TSetFilters setFilters );

		// hide a filter category from the user
		void AddExcludedCategory( const AnsiChar* category );

		// unhide a filter category from the user
		void RemoveExcludedCategory( const AnsiChar* category );

		// is given category node excluded ?
		const Bool IsCategoryExcluded( const rend::DebugFilterHierarchyNode* node ) const;

		// get state of filter
		Bool IsFilterSet( const rend::DebugFilter& filter ) const;

		Bool IsFilterSet( const String& filterName ) const;

		void UpdateFilter( const rend::DebugFilter& filter, const String& name );

		void SetFilter( const rend::DebugFilter& filter, const Bool state, const String& name );

		void SetFilter( const String& filterName, const Bool state );

		void SetFilters( const red::DynArray< String >& filterNames, const Bool state );

		void ToggleFilter( const red::DynArray<String>& filterNames );

		void ResetFilters();

		void FindFiltersTurnedOn( const rend::DebugFilterHierarchyNode* node, red::DynArray<String>& filtersOn ) const;
		
		void FindFiltersTurnedOn( red::DynArray<String>& filtersOn ) const;

		void GetChangedFilters( red::DynArray< String >& changedFilters, const String& prefix = String::EMPTY() );

		void CreateFavoritesTreeNodes( red::SharedPtr< DebugFilterTree >& tree, red::DynArray< red::SharedPtr< FavoriteFilterTreeNode > >& favoriteNodes, red::DynArray<String> favoriteFilterPaths );
		void CreateFilterTree( red::SharedPtr< DebugFilterTree >& tree, const Bool rendering );

		red::HashMap < String, TreeNodeID >& GetNodeMap() { return m_nodeMap; }
		red::HashMap < String, TreeNodeID >& GetFavoriteMap() { return m_favoriteMap; }
		void SetNodeMap( red::HashMap < String, TreeNodeID>& nodemap ) { m_nodeMap = nodemap; }
		void SetFavoriteMap( red::HashMap < String, TreeNodeID>& nodemap );

		void CollectFilterData( THandle< DebugFilterDataContainer > items );

	private:
		void ResetFilter( const rend::DebugFilterHierarchyNode* node );
		void RecurseDebugFilterHierarchyNodes( const rend::DebugFilterHierarchyNode& node, ITreeNode* parentNode, red::SharedPtr< DebugFilterTree >& tree, const Bool rendering );
		void RecurseFilterData( THandle< DebugFilterDataContainer > items, const rend::DebugFilterHierarchyNode* node );

		// root category
		red::SharedPtr< PreviewFiltersNode >	m_root;

		// excluded categories
		red::DynArray< const rend::DebugFilterHierarchyNode* >	m_excludedNodes;

		// get/set functions
		TGetFilters								m_getFunc;
		TSetFilters								m_setFunc;

		String m_editorName;

		red::DynArray< String > m_changedFilters;

		red::HashMap< String, TreeNodeID > m_nodeMap;
		red::HashMap< String, TreeNodeID > m_favoriteMap;
	};
} // tools