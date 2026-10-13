/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialLayerNode.h"

#include "materialLibraryDataProvider.h"
#include "materialLayersTree.h"

#include "../../../backend/backendEngine/include/backendEnginePublic.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/interop/include/interopNotifier.h"

namespace tools
{
	void MaterialBaseNode::SetupInterop( MaterialBaseNode* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValue< String >( "nodeName",
			[ptr]( interop::IContext& ctx, String& outVal )
			{
				outVal = ptr->GetNodeName();
			},
			[ptr]( interop::IContext& ctx, const String& val )
			{
				ptr->SetNodeName( val );
			},
			&ptr->m_nodeNameChanged );

		op.RegisterValue< String >( "path",
			[ptr]( interop::IContext& ctx, String& outVal )
		{
			outVal = ptr->GetPath();
		},
			[ptr]( interop::IContext& ctx, const String& val )
		{
			ptr->SetPath( val );
		},
			&ptr->m_pathChanged );
	}

	void MaterialInstanceNode::SetupInterop( MaterialInstanceNode* ptr, ::interop::utils::Object& op )
	{
		op.RegisterNotifier( "_checkoutRequest", ptr->m_checkoutRequestNotifier );
		op.RegisterNotifier( "_revertRequest", ptr->m_revertRequestNotifier );
		op.RegisterFunctor< void, String >( "ChangeState", [ptr]( ::interop::IContext& ctx, const String& state )
		{
			if ( state == "Readonly" )
			{
				ptr->SetState( NodeState::Readonly );
			}
			else if ( state == "CheckedOut" )
			{
				ptr->SetState( NodeState::Checkedout );
			}
			else if ( state == "Added" )
			{
				ptr->SetState( NodeState::Added );
			}
		} );

		op.RegisterFunctor< void, String >( "createLayer", [ptr]( ::interop::IContext& ctx, const String& name )
		{
			ptr->GetTree()->AddLayer( name );
		} );
		op.RegisterFunctor< void >( "removeLayer", [ptr]( ::interop::IContext& ctx )
		{
			// TODO schedule deletion instead
			ptr->GetTree()->RemoveLayer( ptr );
		} );
	}

	void MaterialCategoryNode::SetupInterop( MaterialCategoryNode* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void, String >( "createLayer", [ptr]( ::interop::IContext& ctx, const String& name )
		{
			ptr->GetTree()->AddLayer( name );
		} );
	}

	MaterialCategoryNode::MaterialCategoryNode( MaterialLayersTree* tree, const String& name, const String& path )
		: MaterialBaseNode( tree, name, path )
	{
	}

	void MaterialCategoryNode::SetNodeName( const String& name )
	{
		//TODO
	}

	MaterialInstanceNode::MaterialInstanceNode( MaterialLayersTree* tree, const String& name, const String& path )
		: MaterialBaseNode( tree, name, path )
		, m_state( NodeState::Readonly )
	{
	}

	MaterialInstanceNode::~MaterialInstanceNode()
	{
	}

	void MaterialInstanceNode::SetNodeName( const String& name )
	{
		//TODO FileMove
	}

	void MaterialInstanceNode::OnSelected()
	{
	}

	void MaterialInstanceNode::RequestCheckout()
	{
		m_checkoutRequestNotifier.Notify( GetDesc() );
	}

	void MaterialInstanceNode::SetState( NodeState state )
	{
		m_state = state;
		m_tree->SelectNodes( this );
	}

	tools::MaterialLayerDescPtr MaterialInstanceNode::GetDesc()
	{
		return m_dataProvider->GetLayerDescByName( GetName() );
	}

	MaterialBaseNode::MaterialBaseNode( MaterialLayersTree* tree, const String& name, const String& path )
		: m_tree( tree )
		, m_name( name )
		, m_path( path )
	{
		m_dataProvider = m_tree->GetDataProvider();
	}

	MaterialBaseNode::~MaterialBaseNode()
	{
	}

	void MaterialBaseNode::AddChildNode( const red::SharedPtr< utils::SimpleNode >& node )
	{
		m_tree->AddNode( node, *this );
	}

}