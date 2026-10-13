/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "resourceEditor.h"

class CurveSet;

namespace tools
{
	class CurveSetResourceEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( CurveSetResourceEditor );
		DECLARE_INTEROP_DERIVED( CurveSetResourceEditor, IResourceEditor );
		RED_BASE_CLASS( IResourceEditor );

	public:
		CurveSetResourceEditor();

		virtual ~CurveSetResourceEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override;

		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;

		virtual const THandle< CResource > GetResource() const override final;

		virtual Bool OnResourcePreSave( const THandle< CResource >& resource ) override;

	private:

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		THandle< CurveSet > m_curveSet;
		SerializableValueNotifier m_serializableNotifier;
	};

} // tools



