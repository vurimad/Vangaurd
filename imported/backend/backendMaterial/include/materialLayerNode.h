/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "materialLibraryDataProvider.h"

#include "../../../common/generalEditorBackend/include/treeUtils.h"

namespace tools
{
	enum class NodeState
	{
		Readonly,
		Added,
		Checkedout,
	};

	class MaterialLayersTree;

	class MaterialBaseNode : public utils::SimpleNode
	{
		DECLARE_INTEROP_DERIVED( MaterialBaseNode, SimpleNode );
	public:
		MaterialBaseNode( MaterialLayersTree* tree, const String& name, const String& path );
		virtual ~MaterialBaseNode();
		void AddChildNode( const red::SharedPtr< utils::SimpleNode >& node );
		virtual String GetName() const override { return GetNodeName(); }
		const String& GetNodeName() const { return m_name; };
		virtual void SetNodeName( const String& name ) = 0;
		const String& GetPath() { return m_path; }
		void SetPath( const String& path ) { m_path = path; }
		MaterialLayersTree* GetTree() { return m_tree; };
	protected:
		MaterialLayersTree* m_tree;
		String m_name;
		String m_path;
		interop::InteropNotifier m_nodeNameChanged;
		interop::InteropNotifier m_pathChanged;
		MaterialLibraryDataProviderPtr m_dataProvider;
	};

	class MaterialInstanceNode : public MaterialBaseNode
	{
		DECLARE_INTEROP_DERIVED( MaterialInstanceNode, MaterialBaseNode );
	public:
		MaterialInstanceNode( MaterialLayersTree* tree, const String& name, const String& path );
		virtual ~MaterialInstanceNode();
		virtual void SetNodeName( const String& name ) override;
		virtual void OnSelected() override;
		void SetState( NodeState state );
		NodeState GetState() const { return m_state; }
		void RequestCheckout();
		
		MaterialLayerDescPtr GetDesc();
	private:
		interop::InteropNotifier m_checkoutRequestNotifier;
		interop::InteropNotifier m_revertRequestNotifier;
		NodeState m_state;
	};

	class MaterialCategoryNode : public MaterialBaseNode
	{
		DECLARE_INTEROP_DERIVED( MaterialCategoryNode, MaterialBaseNode );
	public:
		MaterialCategoryNode( MaterialLayersTree* tree, const String& name, const String& path );
		virtual ~MaterialCategoryNode() {}
		virtual void SetNodeName( const String& name ) override;
	};

}