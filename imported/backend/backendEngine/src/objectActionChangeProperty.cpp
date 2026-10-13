/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectHandler.h"
#include "objectActionChangeProperty.h"
#include "editableProxy.h"
#include "../../../common/core/include/message.h"
#include "../../../common/redReflection/include/editableView.h"

namespace tools
{
	Action_ChangeProperty::Action_ChangeProperty( ObjectHandler& objectHandler, const EditorObjectIDPath& objectPath, const rtti::AccessPath& propertyPath, const rtti::ValuePtr& newValue )
		: m_objectHandler( objectHandler )
		, m_objectPath( objectPath )
		, m_propertyPath( propertyPath )
		, m_newValue( newValue )
	{
		const EditableProxyPtr proxy = m_objectHandler.ResolveProxy( m_objectPath );
		if ( !proxy )
		{
			RED_REPORT_ERROR( RED_LOCATION( objectPath ), "% Failed to change % % of object %. Make sure the resource is open for editing.", RED_NAME_CONSTEXPR( "Action" ), RED_NAME_CONSTEXPR( "Property" ), propertyPath, objectPath );

			return;
		}

		const EditableViewPtr editableView = proxy->CreateView( proxy->GetViewClass() );
		if ( !editableView->GetValue( IRTTIContext::GetDefault(), m_propertyPath, m_oldValue ) )
		{
			RED_LOG_ERROR( "Action_ChangeProperty: Failed to read old property value" );
			m_oldValue.Reset();
		}
	}

	String Action_ChangeProperty::GetDescription() const
	{
		return "Change object property " + m_objectPath.ToString();
	}

	Bool Action_ChangeProperty::Do()
	{
		return WriteValue( m_newValue );
	}

	Bool Action_ChangeProperty::Undo()
	{
		return WriteValue( m_oldValue );
	}

	Bool Action_ChangeProperty::WriteValue( const rtti::ValuePtr& value ) const
	{
		if ( !value )
			return false;

		IObjectResolver::ResolvingParams params;
		const EditableProxyPtr proxy = m_objectHandler.ResolveProxy( m_objectPath );
		if ( !proxy )
		{
			RED_LOG_ERROR( "Action_ChangeProperty: Failed to resolve editable proxy" );
			return false;
		}

		// TODO mark as modified

		const EditableViewPtr editableView = proxy->CreateView( proxy->GetViewClass() );
		rtti::ValuePtr oldValue;
		editableView->GetValue( IRTTIContext::GetDefault(), m_propertyPath, oldValue );
		if ( !editableView->SetValue( IRTTIContext::GetDefault(), m_propertyPath, oldValue, value, true ) )
		{
			RED_LOG_ERROR( "Action_ChangeProperty: Failed to write value" );
			return false;
		}

		return true;
	}

	Bool Action_ChangeProperty::IsModification() const
	{
		return true;
	}

	void Action_ChangeProperty::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		if ( !m_objectPath.IsEmpty() )
		{
			const EditorObjectID& id = m_objectPath.GetFinalChildID();
			outResourcePaths.Insert( id.GetResourcePath() );
		}
	}

} // tools