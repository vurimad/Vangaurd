/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/generalEditorBackend/include/treeUtils.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/generalEditorBackend/include/filterTreeNodeData.h"

#include "../../common/renderData/include/debugFilter.h"

namespace tools
{
	class FilterDescriptionManager;
	class PreviewFilters;
	class ShortcutManager;

 	namespace utils
 	{
 		class SimpleTree;
 	}

	class DebugFilterShortcutDataContainer : public ISerializable
	{
		RTTI_DECLARE_TYPE( DebugFilterShortcutDataContainer );
		RED_BASE_CLASS( ISerializable );

	public:
		DebugFilterShortcutDataContainer();
		virtual ~DebugFilterShortcutDataContainer();

		red::DynArray< String > m_shortcuts;
		red::DynArray< String > m_filters;
	};

	class DebugFilterTree : public utils::SimpleTree
	{
		
	public:
		DebugFilterTree();
		~DebugFilterTree();

		DebugFilterTree( PreviewFilters& debugFilters, PreviewFilters& gameFilters, PreviewFilters& renderFilters, ShortcutManager* shortcutManager, const Bool renderTree );

		typedef std::function< void( ITreeNode* node ) > TSetFavorite;

		PreviewFilters* GetEditorFilters() const { return m_editorFilters; }
		PreviewFilters* GetRenderFilters() const { return m_renderFilters; }
		PreviewFilters* GetGameFilters() const { return m_gameFilters; }

		void SetNodeFavoriteStatus( const TreeNodeID nodeId, const Bool state );
		void UpdateFavorites( ITreeNode* node );

		void AddShortcut( const String& path, const String& shortcut, const Bool renderTree );
		void RemoveShortcut( const String& path, const Bool renderTree );
		void SaveShortcuts( const Bool renderTree );
		Bool LoadShortcuts();

		Bool IsDeprecated( const String& path );
		Bool SetDeprecated( const String& path, const Bool deprecated );

		String GetDescription( const String& path ) const;
		Bool SetDescription( const String& path, const String& description );

		String GetShortcut( const String& path ) const;

		Bool IsRenderTree() const { return m_renderTree; }

		void SetDescriptionManager( FilterDescriptionManager* descriptionManager ) { m_descriptionManager = descriptionManager; }

		void SetFavoritesChangedCallback( TSetFavorite callback ) { m_favoritesChanged = callback; }

		ShortcutManager* m_shortcutManager;

	private:
		Bool m_renderTree;

		PreviewFilters* m_editorFilters;
		PreviewFilters* m_gameFilters;
		PreviewFilters* m_renderFilters;

		TSetFavorite m_favoritesChanged;

		red::HashMap<String, String> m_shortcuts;
		FilterDescriptionManager* m_descriptionManager;
	};

	class DebugFilterTreeNode : public utils::NamedNode
	{
		DECLARE_INTEROP_DERIVED( DebugFilterTreeNode, NamedNode );

	public:
		DebugFilterTreeNode( const String& name, DebugFilterTree* parentTree );
		DebugFilterTreeNode( const String& name, DebugFilterTree* parentTree, const rend::DebugFilter& filter, const String& path );
		virtual ~DebugFilterTreeNode() override;

		// utils::NamedNode
		virtual ISerializable* GetSerializable() const override;
		void ToggleEditorFilter( const Bool state );
		void ToggleGameFilter( const Bool state );
		void ToggleRenderFilter( const Bool state );
		void HandleShortcutChange( DebugFilterTreeNode* node, const String& newShortcut );
		virtual CName GetNodeClass() const override;
		virtual void Initialize( const Bool favorite, const Bool editor, const Bool game );

		virtual THandle< TreeNodeData > CreateNodeDataStruct() const override;
		void ResetShortcut( const String& idString );
		Bool CreateShortcut( const String val );
		virtual void FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const override;
		virtual void SetNodeDataStruct( const THandle< TreeNodeData >& data ) override;

		void SetFavorite( const Bool state ) { m_filterData->m_favorite = state; }
		void SetShortcut( const String shortcut );
		void SetDescription( const String description );
		void SetDeprecated( const Bool deprecated );

		Bool IsFavorite() const { return m_filterData->m_favorite; }
		Bool IsEditor() const { return m_filterData->m_editor; }
		Bool IsGame() const { return m_filterData->m_game; }

		String GetPath() const { return m_path; }
		rend::DebugFilter GetFilter() const { return m_filter; }

		TreeNodeID GetFavoriteId() const { return m_favoriteId; }
		void SetFavoriteId( const TreeNodeID id ) { m_favoriteId = id; }

		Bool IsRenderNode() const { return m_filterData->m_renderNode; }
		void SetRenderNode( const Bool type ) { m_filterData->m_renderNode = type; }

	protected:
		THandle< FilterTreeNodeData > m_filterData;

		String m_path;
		rend::DebugFilter m_filter;

	private:
		virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );
		TreeNodeID m_favoriteId;

		SerializableValueNotifier m_serializableNotifier;
	};

	class FavoriteFilterTreeNode : public DebugFilterTreeNode
	{
		DECLARE_INTEROP_DERIVED( FavoriteFilterTreeNode, NamedNode );

	public:
		FavoriteFilterTreeNode( const String& name, DebugFilterTree* parentTree, const rend::DebugFilter& filter, const String& path );

		virtual void FillNodeDataStruct( const THandle<TreeNodeData>& nodeData ) const override;

		TreeNodeID GetHostId() const { return m_hostId; }
		void SetHostId( const TreeNodeID id ) { m_hostId = id; }

	private:
		TreeNodeID m_hostId;
	};
}
