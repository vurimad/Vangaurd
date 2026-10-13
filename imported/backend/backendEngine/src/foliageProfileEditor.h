/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/resourceMaterial/include/foliageProfile.h"

namespace tools
{
	class FoliageProfileEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( FoliageProfileEditor );
		DECLARE_INTEROP_DERIVED( FoliageProfileEditor, IResourceEditor );

	public:
		FoliageProfileEditor();
		virtual ~FoliageProfileEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;
		virtual void OnPreClose() override final;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CFoliageProfile > m_foliageProfileResource;

		SerializableValueNotifier m_serializableNotifier;
	};
}
