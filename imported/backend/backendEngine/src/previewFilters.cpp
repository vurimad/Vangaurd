/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewFilters.h"
#include "debugFilterTree.h"
#include "treeInterop.h"

#include "../../common/renderData/include/debugFilter.h"

#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugFilterDataContainerItem, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_description );
	RTTI_PROPERTY( m_deprecated );
	RTTI_PROPERTY( m_defaultState );
	RTTI_PROPERTY( m_name );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugFilterDataContainer, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_items );
RTTI_END_TYPE();

namespace tools
{
	PreviewFiltersNodeValue::PreviewFiltersNodeValue(PreviewFilters* root, const rend::DebugFilterHierarchyNode* node)
		: m_root(root)
		, m_node(node)
		, m_name( RED_NAME( node->GetName() ) )
	{
	}

	const CName PreviewFiltersNodeValue::GetType() const
	{
		static CName theName = RED_NAME_CONSTEXPR("Bool");
		return theName;
	}

	void PreviewFiltersNodeValue::Get(interop::IContext& ctx, interop::InteropString& outValue) const
	{
		const auto filter = m_node->GetFilter();
		const bool state = m_root->IsFilterSet(filter);
		outValue = state ? "true" : "false";
	}

	void PreviewFiltersNodeValue::Set(interop::IContext& ctx, const interop::InteropString& newValue) const
	{
		const auto filter = m_node->GetFilter();
		const bool state = (newValue.EqualsNC("true"));
		m_root->SetFilter( filter, state, m_node->GetFullPath() );
	}

	const Bool PreviewFiltersNodeValue::IsReadOnly() const
	{
		return false;
	}

	const Uint8 PreviewFiltersNodeValue::IsDefault() const
	{
		return 0;
	}

	const CName PreviewFiltersNode::GetType() const
	{
		static CName theName = RED_NAME_CONSTEXPR("FilterGroup");
		return theName;
	}

	PreviewFiltersNode::PreviewFiltersNode(PreviewFilters* root, const rend::DebugFilterHierarchyNode* node)
		: m_node(node)
		, m_name( RED_NAME( node->GetName() ) )
		, m_root(root)
	{
	}

	void PreviewFiltersNode::GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const
	{
		ConditionalFillChildren();

		Uint32 childrenCount = m_groups.Size() + m_filters.Size();

		// for safety the root group does not have the "All" settings
		// Also, it makes no sense to have "All" flag if there is just one child node
		if (m_node->HasParent() && childrenCount > 1)
		{
			static CName theDataName = RED_NAME_CONSTEXPR("all");
			outInfo.PushBack(interop::ChildWithCategory(theDataName, CName()));
		}

		for (const auto& info : m_groups)
			outInfo.PushBack(interop::ChildWithCategory(info->GetName(), CName()));

		for (const auto& info : m_filters)
			outInfo.PushBack(interop::ChildWithCategory(info->GetName(), CName()));
	}

	interop::ObjectPtr PreviewFiltersNode::GetChild(interop::IContext& ctx, CName name) const
	{
		if (name == RED_NAME_CONSTEXPR_NOREG( "all") )
			return interop::ObjectPtr(RED_NEW(PreviewFiltersNodeValue)(m_root, m_node));

		ConditionalFillChildren();

		for (const auto& child : m_groups)
			if (child->GetName() == name)
				return child;

		for (const auto& child : m_filters)
			if (child->GetName() == name)
				return child;

		return interop::ObjectPtr();
	}

	void PreviewFiltersNode::ConditionalFillChildren() const
	{
		if (!m_groups.Empty() || !m_filters.Empty())
			return;

		rend::DebugFilterHierarchyNode::TChildren children;
		m_node->GetChildren(children);

		for (const auto* child : children)
		{
			// filter
			if (m_root->IsCategoryExcluded(child))
				continue;

			// create wrapper, use the group wrapper if the element has more children
			if (child->HasChildren())
			{
				m_groups.PushBack(red::CreateSharedPtr<PreviewFiltersNode>(m_root, child));
			}
			else
			{
				m_filters.PushBack(red::CreateSharedPtr<PreviewFiltersNodeValue>(m_root, child));
			}
		}

		std::sort(m_groups.Begin(), m_groups.End(),
			[](const red::SharedPtr< PreviewFiltersNode >& a, const red::SharedPtr< PreviewFiltersNode >& b)
		{
			return red::Strcmp(a->GetName().AsChar(), b->GetName().AsChar()) < 0;
		});

		std::sort(m_filters.Begin(), m_filters.End(),
			[](const red::SharedPtr< PreviewFiltersNodeValue >& a, const red::SharedPtr< PreviewFiltersNodeValue >& b)
		{
			return red::Strcmp(a->GetName().AsChar(), b->GetName().AsChar()) < 0;
		});
	}

	//---

	PreviewFilters::PreviewFilters()
	{
	}

	PreviewFilters::~PreviewFilters()
	{
	}

	void PreviewFilters::Bind( const AnsiChar* rootCategory, const String& editorName, TSetFilters setFilters )
	{
		m_setFunc = setFilters;

		m_editorName = editorName;

		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		const auto* startNode = rootCategory ? rootNode->FindNode( rootCategory ) : rootNode;

		if ( startNode )
			m_root.Reset( RED_NEW(PreviewFiltersNode)( this, startNode ) );
		else
			m_root.Reset();
	}

	void PreviewFilters::AddExcludedCategory( const AnsiChar* category )
	{
		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		const auto* node = rootNode->FindNode( category );

		if ( nullptr != node )
			red::alg::PushBackUnique( m_excludedNodes, node );
	}

	void PreviewFilters::RemoveExcludedCategory( const AnsiChar* category )
	{
		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		const auto* node = rootNode->FindNode( category );

		m_excludedNodes.Remove( node );
	}

	const Bool PreviewFilters::IsCategoryExcluded( const rend::DebugFilterHierarchyNode* node ) const
	{
		return m_excludedNodes.Exist( node );
	}

	Bool PreviewFilters::IsFilterSet( const rend::DebugFilter& filter ) const
	{
		rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );

		return filters.GetSingleBitState( filter );
	}

	Bool PreviewFilters::IsFilterSet( const String& filterName ) const
	{
		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		const auto* node = rootNode->FindNode( filterName.AsChar() );
		if ( node )
		{
			const rend::DebugFilter& filter = node->GetFilter();
			return IsFilterSet( filter );
		}

		return false;
	}

	void PreviewFilters::UpdateFilter( const rend::DebugFilter& filter, const String& name )
	{
		rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );
		m_setFunc( filters, m_nodeMap[ name ], m_favoriteMap[ name ] );
	}

	void PreviewFilters::SetFilter( const rend::DebugFilter& filter, const Bool state, const String& name )
	{
		rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );

		if ( state != filters.GetSingleBitState( filter ) )
		{
			if ( state )
				filters |= filter;
			else
				filters -= filter;

			rend::DebugFilterMask::GetFilterMask( m_editorName ) = filters;

			m_changedFilters.PushBack( name );

			m_setFunc( filters, m_nodeMap[ name ], m_favoriteMap[ name ] );
		}
	}

	void PreviewFilters::SetFilter( const String& filterName, const Bool state )
	{
		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		const auto* node = rootNode->FindNode( filterName.AsChar() );
		if ( node != nullptr )
		{
			const rend::DebugFilter& filter = node->GetFilter();
			SetFilter( filter, state, filterName );
		}
	}

	void PreviewFilters::SetFilters( const red::DynArray< String >& filterNames, const Bool state )
	{
		rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );

		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();

		for ( const String& filterName : filterNames )
		{
			const auto* node = rootNode->FindNode( filterName.AsChar() );
			if ( nullptr != node )
			{
				const rend::DebugFilter& filter = node->GetFilter();

				if ( state != filters.GetSingleBitState( filter ) )
				{
					if ( state )
						filters |= filter;
					else
						filters -= filter;
				}
			}
		}
		
		rend::DebugFilterMask::GetFilterMask( m_editorName ) = filters;
	}

	void PreviewFilters::ToggleFilter( const red::DynArray<String>& filterNames )
	{
		const auto* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );

		for ( const String& filterName : filterNames )
		{
			const auto* node = rootNode->FindNode( filterName.AsChar() );

			if ( nullptr != node )
			{
				const rend::DebugFilter& filter = node->GetFilter();
				Bool state = !IsFilterSet( filter );
				if ( state )
					filters |= filter;
				else
					filters -= filter;
			}
		}

		rend::DebugFilterMask::GetFilterMask( m_editorName ) = filters;
	}

	void PreviewFilters::ResetFilter( const rend::DebugFilterHierarchyNode* node )
	{
		rend::DebugFilterHierarchyNode::TChildren children;
		node->GetChildren( children );

		if ( IsFilterSet( node->GetFilter() ) )
		{
			rend::DebugFilterMask filters = rend::DebugFilterMask::GetFilterMask( m_editorName );
			filters -= node->GetFilter();
			rend::DebugFilterMask::GetFilterMask( m_editorName ) = filters;
		}

		for ( const rend::DebugFilterHierarchyNode* child : children )
		{
			ResetFilter( child );
		}
	}

	void PreviewFilters::ResetFilters()
	{
		ResetFilter( &rend::DebugFilterHierarchyNode::GetRoot() );
	}

	void PreviewFilters::FindFiltersTurnedOn( const rend::DebugFilterHierarchyNode* node, red::DynArray<String>& filtersOn ) const
	{
		if ( IsFilterSet( node->GetFilter() ) )
		{
			filtersOn.PushBack( node->GetFullPath() );
		}

		rend::DebugFilterHierarchyNode::TChildren children;
		node->GetChildren( children );

		for ( const rend::DebugFilterHierarchyNode* child : children )
		{
			FindFiltersTurnedOn( child, filtersOn );
		}
	}

	void PreviewFilters::FindFiltersTurnedOn( red::DynArray<String>& filtersOn ) const
	{
		FindFiltersTurnedOn( &rend::DebugFilterHierarchyNode::GetRoot(), filtersOn );
	}

	void PreviewFilters::GetChangedFilters( red::DynArray< String >& changedFilters, const String& prefix )
	{
		for ( const String& filter : m_changedFilters )
		{
			changedFilters.PushBack( prefix + filter );
		}

		m_changedFilters.Clear();
	}

	void PreviewFilters::RecurseFilterData( THandle< DebugFilterDataContainer > items, const rend::DebugFilterHierarchyNode* node )
	{
		rend::DebugFilterHierarchyNode::TChildren children;
		node->GetChildren( children );
		for ( const rend::DebugFilterHierarchyNode* child : children )
		{
			DebugFilterDataContainerItem childItem( child->GetFullPath() );
			items->m_items.PushBack( childItem );
			RecurseFilterData( items, child );
		}
	}

	void PreviewFilters::CollectFilterData( THandle< DebugFilterDataContainer > items )
	{
		const rend::DebugFilterHierarchyNode* rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		rend::DebugFilterHierarchyNode::TChildren children;
		rootNode->GetChildren( children );
		for ( const rend::DebugFilterHierarchyNode* child : children )
		{
			DebugFilterDataContainerItem item( child->GetFullPath() );
			items->m_items.PushBack( item );
			RecurseFilterData( items, child );
		}
	}

	void PreviewFilters::RecurseDebugFilterHierarchyNodes( const rend::DebugFilterHierarchyNode& hierarchyNode, ITreeNode* parentNode, red::SharedPtr< DebugFilterTree >& tree, const Bool rendering )
	{
		rend::DebugFilterHierarchyNode::TChildren children;
		hierarchyNode.GetChildren( children );
		for ( const auto& child : children )
		{
			const String fullPath = child->GetFullPath();
			auto treeNode = red::CreateSharedPtr< DebugFilterTreeNode >( child->GetName(), tree.Get(), child->GetFilter(), fullPath );
			String childName = child->GetName();
			treeNode->SetRenderNode( rendering );
			treeNode->SetShortcut( tree->GetShortcut( fullPath ) );
			tree->AddNode( treeNode, *parentNode );
			if ( m_nodeMap[ fullPath ] > 0 )
			{
				treeNode->SetFavorite( true );
			}
			m_nodeMap[ fullPath ] = treeNode->GetId();
			RecurseDebugFilterHierarchyNodes( *child, treeNode.Get(), tree, rendering );
		}
	}

	void PreviewFilters::CreateFavoritesTreeNodes( red::SharedPtr< DebugFilterTree >& tree, red::DynArray< red::SharedPtr< FavoriteFilterTreeNode > >& favoriteNodes, red::DynArray<String> favoriteFilterPaths )
	{
		for ( const String& path : favoriteFilterPaths )
		{
			const rend::DebugFilterHierarchyNode* node = rend::DebugFilterHierarchyNode::FindNode( path.AsChar() );
			if ( node != nullptr )
			{
				const String fullPath = node->GetFullPath();
				auto treeNode = red::CreateSharedPtr< FavoriteFilterTreeNode >( node->GetName(), tree.Get(), node->GetFilter(), fullPath );
				favoriteNodes.PushBack( treeNode );
				treeNode->SetHostId( m_nodeMap[ fullPath ] );
				m_favoriteMap[ fullPath ]= treeNode->GetId();
			}
		}
	}

	void PreviewFilters::CreateFilterTree( red::SharedPtr< DebugFilterTree >& tree, const Bool rendering )
	{
		const rend::DebugFilterHierarchyNode* rootNode;
		if ( rendering )
		{
			rootNode = rend::DebugFilterHierarchyNode::FindNode( "Rendering" );
		}
		else
		{
			rootNode = &rend::DebugFilterHierarchyNode::GetRoot();
		}
		
		rend::DebugFilterHierarchyNode::TChildren children;
		rootNode->GetChildren( children );

		for ( const rend::DebugFilterHierarchyNode* child : children )
		{
			const String fullPath = child->GetFullPath();
			auto node = red::CreateSharedPtr< DebugFilterTreeNode >( child->GetName(), tree.Get(), child->GetFilter(), fullPath );
			node->SetRenderNode( rendering );
			node->SetShortcut( tree->GetShortcut( fullPath ) );
			String childName = child->GetName();
			tree->AddNodeToRoot( node );
			
			m_nodeMap[ fullPath ] = node->GetId();
			RecurseDebugFilterHierarchyNodes( *child, node.Get(), tree, rendering );
		}
	}

	void PreviewFilters::SetFavoriteMap( red::HashMap < String, TreeNodeID>& nodemap )
	{
		for ( auto& item : nodemap )
		{
			m_favoriteMap[ item.Key() ] = item.Value();
		}
	}

	void PreviewFilters::SetupInterop( PreviewFilters* ptr, ::interop::utils::Object& op )
	{
	}

	DebugFilterDataContainerItem::DebugFilterDataContainerItem( const String name )
		: m_name( name )
		, m_defaultState( false )
		, m_deprecated( false )
		, m_description( String::EMPTY() )
	{ 
	}

	DebugFilterDataContainerItem::DebugFilterDataContainerItem() = default;
	DebugFilterDataContainerItem::~DebugFilterDataContainerItem() = default;

	DebugFilterDataContainer::DebugFilterDataContainer() = default;
	DebugFilterDataContainer::~DebugFilterDataContainer() = default;

} // tools