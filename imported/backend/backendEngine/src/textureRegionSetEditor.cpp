/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureRegionSetEditor.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureRegionSetEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();


namespace tools
{
	TextureRegionSetEditor::TextureRegionSetEditor()
	{}

	TextureRegionSetEditor::~TextureRegionSetEditor()
	{}

	Bool TextureRegionSetEditor::Initialize( const THandle< CResource >& resource )
	{
		m_textureRegionSetResource = Cast< CTextureRegionSet >( resource );
		if ( !m_textureRegionSetResource )
		{
			return false;
		}

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void TextureRegionSetEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const bool TextureRegionSetEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CTextureRegionSet >();
	}
	
	const THandle< CResource > TextureRegionSetEditor::GetResource() const
	{
		return m_textureRegionSetResource;
	}

	void TextureRegionSetEditor::SetupInterop( TextureRegionSetEditor* editor, ::interop::utils::Object& op )
	{
	}

	void TextureRegionSetEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
	}
}

