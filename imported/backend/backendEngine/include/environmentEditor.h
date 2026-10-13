/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"
#include "objectCollection.h"
#include "../../common/redReflection/include/resourceMonitor.h"

namespace world { class EnvironmentDefinition; }

namespace tools
{
	class CurveSettingsInterop;

	class EnvironmentEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE( EnvironmentEditor );
		DECLARE_INTEROP_DERIVED(EnvironmentEditor, IResourceEditor);

	public:

		EnvironmentEditor();
		virtual ~EnvironmentEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;

	private:		

		THandle< world::EnvironmentDefinition > m_environmentResource;

		CurveSettingsInterop* m_curveSettings;
		interop::ObjectPtr m_curveSettingsObjectPtr;
	};
}
