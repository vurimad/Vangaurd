/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceEditor.h"

namespace world { class EnvironmentAreaParameters; }

namespace tools
{
	class CurveSettingsInterop;

	class EnvironmentParametersEditor : public IResourceEditor
	{
		RTTI_DECLARE_TYPE(EnvironmentParametersEditor);
		DECLARE_INTEROP_DERIVED( EnvironmentParametersEditor, IResourceEditor );

	public:

		EnvironmentParametersEditor();
		virtual ~EnvironmentParametersEditor();

		virtual Bool Initialize( const THandle< CResource >& resource ) override final;
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const override final;
		virtual const THandle< CResource > GetResource() const override final;

	private:

		THandle< world::EnvironmentAreaParameters > m_environmentResource;

		CurveSettingsInterop* m_curveSettings;
		interop::ObjectPtr m_curveSettingsObjectPtr;
	};
}
