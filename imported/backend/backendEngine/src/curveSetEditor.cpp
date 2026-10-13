/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "curveSetEditor.h"
#include "session.h"
#include "resourceBackend.h"
#include "objectCollection.h"
#include "editActionHistory.h"
#include "../../../common/engine/include/curveSetResource.h"
#include "../../../common/redReflection/include/rttiPathParser.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( CurveSetResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();


namespace tools
{
	CurveSetResourceEditor::CurveSetResourceEditor()
	{
	}

	CurveSetResourceEditor::~CurveSetResourceEditor()
	{
	}

	Bool CurveSetResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		m_curveSet = Cast< CurveSet >( resource );
		if ( !m_curveSet )
			return false;
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	const Bool CurveSetResourceEditor::SupportsResourceClass(const rtti::ClassType* resourceClass) const
	{
		return resourceClass->IsA< CurveSet >();
	}

	const THandle< CResource > CurveSetResourceEditor::GetResource() const
	{
		return Cast< CResource >( m_curveSet );
	}

	Bool CurveSetResourceEditor::OnResourcePreSave( const THandle< CResource >& resource )
	{
		auto curveSet = Cast< CurveSet >( resource );
		if ( curveSet )
		{
			const String errorMessage = curveSet->Validate();
			if ( !errorMessage.Empty() )
			{
				RED_REPORT_ERROR( RED_LOCATION( GetPath() ), errorMessage.AsChar() );
				return false;
			}
		}

		return TBaseClass::OnResourcePreSave( resource );
	}

	void CurveSetResourceEditor::SetupInterop( CurveSetResourceEditor* editor, ::interop::utils::Object& op )
	{
		THandle< CurveSet > curveSet = Cast< CurveSet >( editor->GetResource() );
		if ( curveSet == nullptr )
		{
			return;
		}
	}
	
	void CurveSetResourceEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
	}

} // interop