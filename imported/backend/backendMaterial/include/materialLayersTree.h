/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "materialLibraryDataProvider.h"

#include "../../../common/generalEditorBackend/include/treeUtils.h"

namespace tools
{
	class MaterialBaseNode;
	class MaterialCategoryNode;
	class MaterialInstanceNode;
	class IEditActionHistory;
	class ObjectCollection;
	class LayerLibraryMaterialController;

	class MaterialLayersTree: public utils::SimpleTree, public IMaterialLibraryDataProviderListener
	{
		DECLARE_INTEROP_DERIVED( MaterialLayersTree, SimpleTree );
	public:
		MaterialLayersTree( LayerLibraryMaterialController* controller, tools::ResourceBackend* backend, const String& rootDirectory, IEditActionHistory* actionHistory );
		virtual ~MaterialLayersTree();
		void SetSelection( const TTreeNodeIDs& nodeIDs ) override;
		void SelectNodes( const MaterialBaseNode* node );
		void Reset();
		Bool Save();
		void AddLayer( const String& name );
		void RemoveLayer( MaterialInstanceNode* node );
		MaterialLibraryDataProviderPtr& GetDataProvider() { return m_dataProvider; }

		virtual void OnReset() override;
		virtual void OnCreateNode( MaterialLayerDescPtr& layerDesc ) override;
		virtual void OnRemoveNode( const String& name ) override;
	private:
		void CreateLayerNode( MaterialLayerDescPtr& layerDesc, MaterialCategoryNode* categoryNode );
		MaterialCategoryNode* GetCategoryNode( const String& name );
		MaterialCategoryNode* CreateCategoryNode( const String& name );
	private:
		MaterialLibraryDataProviderPtr m_dataProvider;
		ObjectCollection* m_selection;
		red::SharedPtr< MaterialBaseNode > m_root;
		LayerLibraryMaterialController* m_controller;
		Bool m_selectionRefresh;

		red::HashMap<String, MaterialCategoryNode*> m_categoryNodes;
		red::HashMap<String, MaterialInstanceNode* > m_layerNodes;
	};
}