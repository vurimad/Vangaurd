/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/resourceEditor.h"

class CTextureArray;

namespace interop
{
	class TextureArrayResourceEditor;
}

namespace tools
{
	/// Editor for mesh files
	class TextureArrayResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( TextureArrayResourceEditor );
		DECLARE_INTEROP_DERIVED( TextureArrayResourceEditor, IResourceEditor );

	public:
		TextureArrayResourceEditor();
		virtual ~TextureArrayResourceEditor();

		/// IResourceEditor interface
		virtual Bool Initialize( const THandle< CResource >& resource ) override;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override;
		virtual const THandle< CResource > GetResource() const override;

	private:
		Bool Compile();

		THandle< CTextureArray > m_textureArray;
		interop::InteropNotifier m_bitmapsCountChanged;
	};

} // tools



