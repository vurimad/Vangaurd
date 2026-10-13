/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugFilterTree.h"

#include "previewFilters.h"
#include "filterDescriptionManager.h"

#include "../../../common/generalEditorBackend/include/filterTreeNodeData.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redFileSystem/include/fileSys.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugFilterShortcutDataContainer, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_shortcuts );
	RTTI_PROPERTY( m_filters );
RTTI_END_TYPE();

namespace tools
{
	DebugFilterTreeNode::DebugFilterTreeNode( const String& name, DebugFilterTree* parentTree, const rend::DebugFilter& filter, const String& path )
		: utils::NamedNode( name, parentTree )
		, m_filter( filter )
		, m_path( path )
	{
		m_serializableNotifier.SetCallback( [ this ]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			OnNotifyPropertyChange( id, path );
		} );

		Initialize( false, parentTree->GetEditorFilters()->IsFilterSet( filter ), parentTree->GetGameFilters()->IsFilterSet( filter ) );
	}

	DebugFilterTreeNode::DebugFilterTreeNode( const String& name, DebugFilterTree* parentTree )
		: utils::NamedNode( name, parentTree )
		, m_filter( nullptr )
		, m_path( String::EMPTY() )
	{
		m_serializableNotifier.SetCallback( [ this ]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			OnNotifyPropertyChange( id, path );
		} );
	}

	DebugFilterTreeNode::~DebugFilterTreeNode()
	{
	}

	void DebugFilterTreeNode::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
	}

	ISerializable* DebugFilterTreeNode::GetSerializable() const
	{
		//TODO: where this is needed?
		return nullptr;
	}

	void DebugFilterTreeNode::ToggleEditorFilter( const Bool state )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->GetEditorFilters()->SetFilter( m_filter, state, m_path );
	}

	void DebugFilterTreeNode::ToggleGameFilter( const Bool state )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->GetGameFilters()->SetFilter( m_filter, state, m_path );
	}

	void DebugFilterTreeNode::ToggleRenderFilter( const Bool state )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->GetRenderFilters()->SetFilter( m_filter, state, m_path );
	}

	void DebugFilterTreeNode::HandleShortcutChange( DebugFilterTreeNode* node, const String& newShortcut )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( node->m_treeFallbackPtr );

		String idString = "idDebugFilter" + node->GetPath();
		idString.ReplaceAll( "/", "" );

		if ( newShortcut.Empty() )
		{
			node->m_filterData->m_shortcut = String::EMPTY();
			node->ResetShortcut( idString );
			tree->RemoveShortcut( node->GetPath(), node->m_filterData->m_renderNode );
		}
		else
		{
			if ( node->CreateShortcut( newShortcut ) )
			{
				node->m_filterData->m_shortcut = newShortcut;
				tree->AddShortcut( node->GetPath(), newShortcut, node->m_filterData->m_renderNode );
			}
		}
		
		tree->GetEditorFilters()->UpdateFilter( node->m_filter, node->m_path );

		tree->NotifyNodeInvalidated( node->GetId() );
	}

	void DebugFilterTreeNode::SetupInterop( DebugFilterTreeNode* node, ::interop::utils::Object& op )
	{
	}

	void DebugFilterTreeNode::Initialize( const Bool favorite, const Bool editor, const Bool game )
	{
		m_filterData = CreateHandle< FilterTreeNodeData >();
		m_filterData->m_editor = editor;
		m_filterData->m_favorite = favorite;
		m_filterData->m_game = game;
		m_filterData->m_shortcut = String::EMPTY();
	}

	CName DebugFilterTreeNode::GetNodeClass() const
	{
		return m_filterData->GetClass()->GetName();
	}

	void DebugFilterTreeNode::ResetShortcut( const String& idString )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->m_shortcutManager->SetCustomBinding( idString, tools::ShortcutBinding() );
	}
	
	Bool DebugFilterTreeNode::CreateShortcut( const String val )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		auto bind = tree->m_shortcutManager->ToBinding( val );
		
		ShortcutCallback callback = [ this, tree ]( const KeyEventData& )
		{
			if ( IsRenderNode() )
			{
				m_filterData->m_editor = !m_filterData->m_editor;
				ToggleRenderFilter( m_filterData->m_editor );
			}
			else
			{
				m_filterData->m_editor = !m_filterData->m_editor;
				ToggleEditorFilter( m_filterData->m_editor );
			}

			m_filterData->m_game = !m_filterData->m_game;
			ToggleGameFilter( m_filterData->m_editor );

			return true;
		};

		String idString = "idDebugFilter" + GetPath();
		String name = "Debug Filter: " + GetPath();
		idString.ReplaceAll( "/", "" );
		ShortcutBuilder shortcut = ShortcutBuilder().IdName( idString ).Name( name ).Binding( bind ).Callback( callback );
		String currentOwner;
		if ( tree->m_shortcutManager->IsBindingAvailable( shortcut, currentOwner ) )
		{
			ShortcutBuilder shortcut = ShortcutBuilder().IdName( idString ).Name( name ).Callback( callback ).Group( "DebugFilter" );
			tree->m_shortcutManager->Register( shortcut );
			tree->m_shortcutManager->SetCustomBinding( idString, bind );
		}
		else
		{
			//TODO: hack for duplicates, fix properly in shortcutmanager
			if ( name == currentOwner )
			{
				m_filterData->m_shortcut = val;
				return true;
			}

			m_filterData->m_shortcut = "Error" + currentOwner;
			return false;
		}

		return true;
	}

	THandle< TreeNodeData > DebugFilterTreeNode::CreateNodeDataStruct() const
	{
		return m_filterData;
	}

	void DebugFilterTreeNode::FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const
	{
		utils::NamedNode::FillNodeDataStruct( nodeData );

		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );

		FilterTreeNodeData* filterNodeData = Cast< FilterTreeNodeData >( nodeData.Get() );

		filterNodeData->m_editor = tree->GetEditorFilters()->IsFilterSet( m_filter );
		filterNodeData->m_game = tree->GetGameFilters()->IsFilterSet( m_filter );
		filterNodeData->m_favorite = m_filterData->m_favorite;
		filterNodeData->m_renderNode = m_filterData->m_renderNode;
		filterNodeData->m_description = tree->GetDescription( m_path );
		filterNodeData->m_deprecated = tree->IsDeprecated( m_path );
		filterNodeData->m_shortcut = m_filterData->m_shortcut;
		
		if ( !m_filterData->m_shortcut.BeginsWith( "Error" ) )
		{
			// shortcut might have been changed by favorites tab
			String idString = "idDebugFilter" + GetPath();
			idString.ReplaceAll( "/", "" );
			filterNodeData->m_shortcut = String::EMPTY();
			tree->m_shortcutManager->GetBindingAsString( idString, filterNodeData->m_shortcut );
		}	
	}

	void DebugFilterTreeNode::SetNodeDataStruct( const THandle< TreeNodeData >& data )
	{
		THandle< FilterTreeNodeData > filterNodeData = Cast< FilterTreeNodeData >( data );
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );

		if ( m_filterData->m_editor != filterNodeData->m_editor )
			tree->GetEditorFilters()->SetFilter( m_filter, filterNodeData->m_editor, m_path );

		if ( m_filterData->m_game != filterNodeData->m_game )
			tree->GetGameFilters()->SetFilter( m_filter, filterNodeData->m_game, m_path );

		if ( m_filterData->m_deprecated != filterNodeData->m_deprecated )
			tree->SetDeprecated( m_path, filterNodeData->m_deprecated );

		if ( m_filterData->m_description != filterNodeData->m_description )
			tree->SetDescription( m_path, filterNodeData->m_description );

		if ( m_filterData->m_shortcut != filterNodeData->m_shortcut )
			HandleShortcutChange( this, filterNodeData->m_shortcut );

		Bool isFavorite = m_filterData->m_favorite;
		m_filterData = filterNodeData;

		// this needs to be evaluated the last, because it may remove 'this' (no time to untangle it atm)
		if ( isFavorite != filterNodeData->m_favorite )
			tree->UpdateFavorites( this );
	}

	void DebugFilterTreeNode::SetShortcut( const String shortcut )
	{
		m_filterData->m_shortcut = shortcut;
		if( !shortcut.Empty() )
			CreateShortcut( shortcut );
	}

	void DebugFilterTreeNode::SetDescription( const String description )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->SetDescription( GetPath(), description );
		tree->NotifyNodeInvalidated( GetId() );
	}

	void DebugFilterTreeNode::SetDeprecated( const Bool deprecated )
	{
		DebugFilterTree* tree = static_cast< DebugFilterTree* >( m_treeFallbackPtr );
		tree->SetDeprecated( GetPath(), deprecated );
		tree->NotifyNodeInvalidated( GetId() );
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	FavoriteFilterTreeNode::FavoriteFilterTreeNode( const String& name, DebugFilterTree* parentTree, const rend::DebugFilter& filter, const String& path )
		: DebugFilterTreeNode( name, parentTree, filter, path )
	{
	}

	void FavoriteFilterTreeNode::FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const
	{
		DebugFilterTreeNode::FillNodeDataStruct( nodeData );
		
		nodeData->m_name = m_path;	
	}

	void FavoriteFilterTreeNode::SetupInterop( FavoriteFilterTreeNode* node, ::interop::utils::Object& op )
	{
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	DebugFilterTree::DebugFilterTree( PreviewFilters& debugFilters, PreviewFilters& gameFilters, PreviewFilters& renderFilters, ShortcutManager* shortcutManager, const Bool renderTree )
	{
		m_editorFilters = &debugFilters;
		m_gameFilters = &gameFilters;
		m_renderFilters = &renderFilters;
		m_shortcutManager = shortcutManager;

		m_renderTree = renderTree;
		LoadShortcuts();
	}

	DebugFilterTree::DebugFilterTree() = default;

	void DebugFilterTree::SetNodeFavoriteStatus( const TreeNodeID nodeId, const Bool state )
	{
		if ( GetNodes().Empty() )
			return;

		ITreeNode* node = GetNode( nodeId );

		if ( node != nullptr )
		{
			DebugFilterTreeNode* debugNode = static_cast< DebugFilterTreeNode* >( node );
			if ( debugNode )
			{
				debugNode->SetFavorite( state );
			}
		}
	}

	void DebugFilterTree::UpdateFavorites( ITreeNode* node )
	{
		if ( m_favoritesChanged )
		{
			m_favoritesChanged( node );
		}
	}

	void DebugFilterTree::AddShortcut( const String& path, const String& shortcut, const Bool renderTree )
	{
		m_shortcuts[ path ] = shortcut;
		SaveShortcuts( renderTree );
	}

	void DebugFilterTree::RemoveShortcut( const String& path, const Bool renderTree )
	{
		m_shortcuts.Remove( path );
		SaveShortcuts( renderTree );
	}

	void DebugFilterTree::SaveShortcuts( const Bool renderTree )
	{
  		THandle< DebugFilterShortcutDataContainer > filterData = CreateHandle<DebugFilterShortcutDataContainer>();
  		filterData->m_filters = m_shortcuts.GetKeys();
  		filterData->m_shortcuts = m_shortcuts.GetValues();
  
  		serialization::json::JsonSerializer jsonSerializer( filterData );
  		String result = String::EMPTY();
  		jsonSerializer.Serialize( result );
  
		String filename = "FilterShortcuts";
		SaveStringToFile( res::GetUserDefinedFilterFilePath( filename ), result );
	}

	Bool DebugFilterTree::LoadShortcuts()
	{
		THandle< DebugFilterShortcutDataContainer > filterData;

		String filename = "FilterShortcuts";

		String jsonData;
		if ( !LoadFileToString( res::GetUserDefinedFilterFilePath( filename ), jsonData ) )
			return false;

		if ( !res::json::ParseObjectInline( std::move( jsonData ), filterData ) )
			return false;

		for ( Uint32 i = 0; i < filterData->m_filters.Size(); ++i )
		{
			m_shortcuts[ filterData->m_filters[ i ] ] = filterData->m_shortcuts[ i ];
		}

		return true;
	}

	String DebugFilterTree::GetShortcut( const String& path ) const
	{
		auto it = m_shortcuts.Find( path );
		if ( it != m_shortcuts.End() )
		{
			if ( IsRenderTree() )
			{
				if ( it.Key().BeginsWith( "Rendering" ) )
				{
					return it.Value();
				}
			}
			else
			{
				if ( !it.Key().BeginsWith( "Rendering" ) )
				{
					return it.Value();
				}
			}
		}
		return String::EMPTY();
	}

	String DebugFilterTree::GetDescription( const String& path ) const
	{
		return m_descriptionManager->GetDescription( path );
	}

	Bool DebugFilterTree::SetDescription( const String& path, const String& description )
	{
		return m_descriptionManager->SetDescription( path, description );
	}

	Bool DebugFilterTree::SetDeprecated( const String& path, const Bool deprecated )
	{
		return m_descriptionManager->SetDeprecated( path, deprecated );
	}

	Bool DebugFilterTree::IsDeprecated( const String& path )
	{
		return m_descriptionManager->IsDeprecated( path );
	}

	DebugFilterTree::~DebugFilterTree() = default;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	DebugFilterShortcutDataContainer::DebugFilterShortcutDataContainer() = default;
	DebugFilterShortcutDataContainer::~DebugFilterShortcutDataContainer() = default;
}


