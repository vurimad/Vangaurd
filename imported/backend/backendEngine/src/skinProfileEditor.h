/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/resourceMaterial/include/skinProfile.h"

namespace tools
{
	class SkinProfileEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( SkinProfileEditor );
		DECLARE_INTEROP_DERIVED( SkinProfileEditor, IResourceEditor );

	public:
		SkinProfileEditor();
		virtual ~SkinProfileEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;
		virtual void OnPreClose() override final;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CSkinProfile > m_skinProfileResource;

		SerializableValueNotifier m_serializableNotifier;
	};
}
