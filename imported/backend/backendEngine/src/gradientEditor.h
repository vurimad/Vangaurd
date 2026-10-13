/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/resourceMaterial/include/gradient.h"

namespace tools
{
	class GradientEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( GradientEditor );
		DECLARE_INTEROP_DERIVED( GradientEditor, IResourceEditor );

	public:
		GradientEditor();
		virtual ~GradientEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;
		virtual void OnPreClose() override final;

	private:
		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CGradient > m_gradientResource;

		SerializableValueNotifier m_serializableNotifier;
	};
}
