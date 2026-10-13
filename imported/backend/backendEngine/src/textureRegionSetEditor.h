/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/resourceMaterial/include/textureRegionSet.h"

namespace tools
{
	class TextureRegionSetEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( TextureRegionSetEditor );
		DECLARE_INTEROP_DERIVED( TextureRegionSetEditor, IResourceEditor );

	public:
		TextureRegionSetEditor();
		virtual ~TextureRegionSetEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;
		virtual void OnPreClose() override final;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CTextureRegionSet > m_textureRegionSetResource;

		SerializableValueNotifier m_serializableNotifier;
	};
}
