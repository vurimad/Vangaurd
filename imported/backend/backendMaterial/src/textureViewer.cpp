/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureViewer.h"

#include "bitmapTextureCompilationSource.h"

#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "bitmapTextureCooker.h"
#include "../../../common/resourceMaterial/include/dynamicTexture.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureViewerResourceEditor, tools );
RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	static constexpr char c_textureSetupPropertyName[] = "setup";
	static constexpr char c_textureSourceDataPropertyName[] = "sourceData";

	TextureViewerResourceEditor::TextureViewerResourceEditor()
		: m_controller( nullptr )
	{
	}

	TextureViewerResourceEditor::~TextureViewerResourceEditor()
	{
	}

	Bool TextureViewerResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		m_resource = Cast< ITexture >( resource );
		m_sharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData>( *this );
		m_controller = red::CreateSharedPtr<TextureController>( this );
		m_controller->SetTexture( m_resource );
		return true;
	}

	void TextureViewerResourceEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const Bool TextureViewerResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< CBitmapTexture >() ) || resourceClass->IsA( ClassID< DynamicTexture >() );
	}

	const THandle< CResource > TextureViewerResourceEditor::GetResource() const
	{
		return Cast< CResource >( (m_controller->GetTexture()) );
	}

	void TextureViewerResourceEditor::SetupInterop( TextureViewerResourceEditor* editor, ::interop::utils::Object& op )
	{
		op.RegisterObject( "textureView", editor->m_controller->CreateInterop() );
	}

	void TextureViewerResourceEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		m_texturePreviewPanel = red::CreateSharedPtr<TexturePreviewPanel>( renderingWindowPtr, m_sharedData );
		m_texturePreviewPanel->SetController( m_controller );
		outPanel = m_texturePreviewPanel;
	}

	void TextureViewerResourceEditor::OnNotifyPropertyChange(const SerializableID& id, const rtti::AccessPath& path)
	{
		if ( id == m_resource->GetID() )
		{
			rtti::PathParser parser( path );
			CName propertyName;
			parser.EatName( propertyName );

			if( propertyName == RED_NAME_CONSTEXPR_NOREG( c_textureSetupPropertyName ) ||
				propertyName == RED_NAME_CONSTEXPR_NOREG( c_textureSourceDataPropertyName ) )
			{
				if( CookTexture() )
				{
					m_resource->CreateRenderResource();
				}
			}
		}
	}

	bool TextureViewerResourceEditor::Save( red::DynArray< res::ResourcePath >& successfullySaved, red::DynArray< res::ResourcePath >& failedToSave )
	{
		return IResourceEditor::Save( successfullySaved, failedToSave );
	}

	bool TextureViewerResourceEditor::CookTexture()
	{
		if( THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( m_resource ) )
		{
			tools::BitmapTextureCookerSetup cookerSetup( *texture );
			cookerSetup.cookPC = true;
			cookerSetup.cookDURANGO = true;
			cookerSetup.cookORBIS = true;
			cookerSetup.cookWINDOWS_SERVER = true;
			tools::BitmapTextureCooker cooker( cookerSetup );
			return cooker.DoCook();
		}
		return false;
	}

} // namespace tools