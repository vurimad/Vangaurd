/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializableEditorView.h"
#include "rttiValueHolder.h"
#include "rttiPathParser.h"
#include "rttiPointerTypes.h"
#include "rttiAccessPath.h"
#include "rttiSystem.h"
#include "rttiArrayTypesImpl.h"
#include "rttiProperty.h"
#include "../../redContainers/include/fundamentalStringConversion.h"
#include "../../redConfig/include/configVar.h"

namespace Config
{
	TConfigVar< Bool > cvDebugEditableView( "Backend", "DebugEditableView", false );
}

namespace tools
{

	CName ISerializableEditorView::st_PropNameViewClass = RED_NAME_CONSTEXPR( "ViewClass" );
	CName ISerializableEditorView::st_PropNameObjectClass = RED_NAME_CONSTEXPR( "ObjectClass" );
	CName ISerializableEditorView::st_PropNameObjectID = RED_NAME_CONSTEXPR( "ObjectID" );

	ISerializableEditorView::ISerializableEditorView( const EditableViewPtr& parentView, const Bool isReadOnly, const rtti::ClassType* viewClass, const THandle<ISerializable>& userContext)
		: IEditableView( viewClass )
		, m_isReadOnly( isReadOnly )
		, m_parentView( parentView )
		, m_userContext( userContext )
	{
	}

	SerializableHandle ISerializableEditorView::GetUserContext() const
	{
		return m_userContext;
	}

	EditableViewPtr ISerializableEditorView::GetParentView() const
	{
		return m_parentView;
	}

	SerializableID ISerializableEditorView::GetSerializableID() const
	{
		const THandle< ISerializable > object = GetObject();
		if ( object )
		{
			return object->GetID();
		}
		return SerializableID();
	}

	Bool ISerializableEditorView::IsReadOnly() const
	{
		return m_isReadOnly;
	}

	Bool ISerializableEditorView::IsReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const
	{
		const THandle< ISerializable > object = GetObject();

		if ( !object )
		{
			ctx.ReportError( GetViewClass(), "Object is gone" );
			return false;
		}

		// custom property		
		{
			rtti::PathParser parser( path );

			CName propertyName;
			if ( parser.EatName( propertyName ) )
			{
				// use the dynamic property interface
				rtti::EditableProperties container;
				object->GetCustomEditableProperties( container );
				
				const red::DynArray< rtti::ClassEditablePropertyInfo >& properties = container.Get();
				auto it = std::find_if( properties.Begin(), properties.End(), [propertyName]( const rtti::ClassEditablePropertyInfo& info )
				{
					return info.m_name == propertyName;
				} );
				
				if ( it != properties.End() )
				{
					outReadOnly = it->m_isReadOnly;
					return true;
				}
			}
		}

		return GetViewClass()->IsPropertyReadOnly( ctx, path, outReadOnly );
	}

	Bool ISerializableEditorView::IsInstanceEditable() const
	{
		return m_parentView ? m_parentView->IsInstanceEditable() : false;
	}

	Bool ISerializableEditorView::GetValue(IRTTIContext& ctx, const rtti::AccessPath& path, red::SharedPtr< rtti::ValueHolder >& outValue) const
	{
		const THandle< ISerializable > object = GetObject();

		// make sure object exists
		if ( !object )
		{
			ctx.ReportError( GetViewClass(), "Object is gone" );
			return false;
		}

		// custom property		
		{
			rtti::PathParser parser( path );

			CName propertyName;
			if ( parser.EatName( propertyName ) )
			{
				// try to interpret as custom value
				if ( GetCustomValue( ctx, propertyName, parser.GetUneatenPath(), outValue ) )
					return true;

				// if there's no path left try to interpret as debug value
				if ( !parser )
				{
					if ( GetDebugValue( ctx, propertyName, outValue ) )
						return true;
				}
			}
		}

		// process static properties (they generate errors on invalid names)
		return GetViewClass()->ReadValue( ctx, object.Get(), path, outValue );

	}

	void ISerializableEditorView::OnValueSet( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) const
	{
		const THandle< ISerializable > object = GetObject();

		// notify object that the value was changed
		object->OnPropertyPostChange( path, oldValue, newValue );

		if ( GetParentView() )
		{
			GetParentView()->OnChildViewChanged();
		}
	}

	Bool ISerializableEditorView::SetValue( IRTTIContext& ctx,  const rtti::AccessPath& path, const red::SharedPtr< rtti::ValueHolder >& oldValue, const red::SharedPtr< rtti::ValueHolder >& newValue, bool clone ) const
	{
		const THandle< ISerializable > object = GetObject();

		// make sure object exists
		if ( !object )
		{
			ctx.ReportError( GetViewClass(), "Object is gone" );
			return false;
		}

		// no value to set
		if ( !newValue )
		{
			ctx.ReportError( GetViewClass(), "No value to set" );
			return false;
		}

		// read only
		if ( m_isReadOnly )
		{
			ctx.ReportError( GetViewClass(), "Object is read only" );
			return false;
		}

		// make sure object wants the change
		red::SharedPtr< const rtti::ValueHolder > copy( newValue ); // NOTE: this is a copy of the REFERENCE not the value 
		if ( !object->OnPropertyPreChange( path, copy ) ) // if we want to change the value we need to modify the shared pointer, not the object inside
		{
			ctx.ReportError( GetViewClass(), "Object prevented change" );
			return false;
		}

		// write custom properties
		{
			rtti::PathParser parser( path );

			CName propertyName;
			if ( parser.EatName( propertyName ) )
			{
				// try to interpret as custom value
				if ( SetCustomValue( ctx, propertyName, parser.GetUneatenPath(), copy ) )
				{
					OnValueSet( path, oldValue, copy );
					return true;
				}
			}
		}

		// write new value
		if ( !GetViewClass()->WriteValue( ctx, object.Get(), path, *copy, clone ) )
		{
			ctx.ReportError( GetViewClass(), "Writing new value failed" );
			return false;
		}

		OnValueSet( path, oldValue, copy );
		return true;
	}

	Bool ISerializableEditorView::ResetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue ) const
	{
		rtti::PathParser parser( path );
		const rtti::IType* propType = GetViewClass();

		CName propertyName;
		Uint32 depth = 0;
		while ( parser.EatName( propertyName ) )
		{
			if ( propType->GetType() == RT_Handle || propType->GetType() == RT_Pointer )
			{
				const rtti::IBasePointerType* pointerType = static_cast<const rtti::IBasePointerType*>( propType );
				propType = pointerType->GetPointedType();
			}

			if ( propType->GetType() != RT_Class )
			{
				return false;
			}

			const rtti::ClassType* classType = static_cast<const rtti::ClassType*>( propType );

			rtti::EditableProperties container;
			// TODO try to acquire property data in each loop so that we can get custom editable properties in case of serializables
			// Submitting now as a part of P0 fix, will revisit tomorrow
			if ( depth++ == 0 )
			{
				GetRootProperties( container );
			}
			else
			{
				classType->GetEditableProperties( container );
			}

			const red::DynArray< rtti::ClassEditablePropertyInfo >& props = container.Get();
			const auto propIt = std::find_if( props.Begin(), props.End(), [ propertyName ]( const rtti::ClassEditablePropertyInfo& info ) { return info.m_name == propertyName; } );
			if ( propIt != props.End() )
			{
				propType = propIt->m_type;
			}
			else if ( const rtti::Property* prop = classType->FindProperty( propertyName ) )
			{
				propType = prop->GetType();
			}
			else
			{
				return false;
			}
		}

		red::SharedPtr< rtti::ValueHolder > defaultValue;
		THandle< ISerializable > defaultObject = GetDefaultObject();

		if ( propType->GetType() == RT_Array )
		{
			const rtti::ArrayType* arrayType = static_cast<const rtti::ArrayType*>( propType );
			const Uint32 arraySize = arrayType->GetArraySize( defaultObject.Get() );

			rtti::AccessPath arrayPath;
			Int32 index;
			if ( parser.EatIndex( index ) )
			{
				propType = arrayType->GetInnerType();
				arrayPath = rtti::AccessPath( propertyName.AsChar() )[0];
			}
			else
			{
				arrayPath = rtti::AccessPath( propertyName.AsChar() );
			}

			if ( arraySize > 0 )
			{
				GetViewClass()->ReadValue( IRTTIContext::GetDefault(), defaultObject.Get(), arrayPath, defaultValue );
			}
		}
		
		if ( !defaultValue && defaultObject )
		{
			red::SharedPtr< IEditableView > defaultObjectView = defaultObject->CreateView();
			defaultObjectView->GetValue( IRTTIContext::GetDefault(), path, defaultValue );
		}

		// don't 'else' with the previous statement, it can still be null
		if ( !defaultValue )
		{
			defaultValue = rtti::ValueHolder::CreateEmpty();
		}

		if ( propType->GetType() != RT_Handle && propType->GetType() != RT_Pointer )
		{
			return SetValue( ctx, path, oldValue, defaultValue );
		}

		const rtti::IBasePointerType* pointerType = static_cast<const rtti::IBasePointerType*>( propType );

		THandle< ISerializable > defaultPointedObject;
		pointerType->WriteValue( IRTTIContext::GetDefault(), &defaultPointedObject, rtti::AccessPath(), *defaultValue, false );

		THandle< ISerializable > newPropertyValue;
		if ( defaultPointedObject )
		{
			newPropertyValue.Reset( defaultPointedObject->GetClass()->CreateObject<ISerializable>() );
		}

		pointerType->ReadValue( IRTTIContext::GetDefault(), &newPropertyValue, rtti::AccessPath(), defaultValue );
		return SetValue( ctx, path, oldValue, defaultValue );	
	}

	Bool ISerializableEditorView::IsDefault( IRTTIContext& ctx, const rtti::AccessPath& path, Uint8& out ) const
	{
		THandle< ISerializable > defaultObject = GetDefaultObject();
		if ( !defaultObject )
			return false;

		rtti::ValuePtr currentValue;
		if ( !GetValue( ctx, path, currentValue ) )
			return false;

		rtti::ValuePtr defaultValue;
		red::SharedPtr< IEditableView > defaultObjectView = defaultObject->CreateView();

		if ( !defaultObjectView->GetValue( IRTTIContext::GetDummy(), path, defaultValue ) )
			return false;

		out = currentValue->ToString() == defaultValue->ToString() ? 1 : 2;
		return true;
	}

	void ISerializableEditorView::GetRootProperties( rtti::EditableProperties& outRootProperties ) const
	{
		const THandle< ISerializable > object = GetObject();

#ifndef NO_EDITOR_PROPERTY_SUPPORT
		if ( IsInstanceEditable() )
			GetViewClass()->GetInstanceEditableProperties( outRootProperties );
		else
			GetViewClass()->GetEditableProperties( outRootProperties );
#endif //! NO_EDITOR_PROPERTY_SUPPORT

		// Verify property names, otherwise will cause another fatal assert later and harder to debug why
		for ( const auto& it : outRootProperties.Get() )
		{
			if ( !it.m_name )
			{
				if (IsInstanceEditable())
				{
					RED_FATAL("%hs GetInstanceEditableProperties() returned an empty property name!", GetViewClass()->GetName().AsChar());
				}
				else
				{
					RED_FATAL("%hs GetEditableProperties() returned an empty property name!", GetViewClass()->GetName().AsChar());
				}
			}
		}

		// get dynamic properties
		if ( object )
			object->GetCustomEditableProperties( outRootProperties );

		// Verify property names, otherwise will cause another fatal assert later and harder to debug why
		for ( const auto& it : outRootProperties.Get() )
		{
			RED_FATAL_ASSERT( it.m_name, "%hs GetCustomEditableProperties() returned an empty property name!", GetViewClass()->GetName().AsChar());
		}

		// add special debug properties
		if ( Config::cvDebugEditableView.Get() )
		{
			// view class name
			{
				rtti::ClassEditablePropertyInfo info;
				info.m_name = st_PropNameViewClass;
				info.m_isReadOnly = true;
				info.m_type = GetTypeObject< CName >();
				info.m_category = RED_NAME_CONSTEXPR( "Debug" );
				outRootProperties.Add( info );
			}

			// object class name
			{
				rtti::ClassEditablePropertyInfo info;
				info.m_name = st_PropNameObjectClass;
				info.m_isReadOnly = true;
				info.m_type = GetTypeObject< CName >();
				info.m_category = RED_NAME_CONSTEXPR( "Debug" );
				outRootProperties.Add( info );
			}

			// object ID
			{
				rtti::ClassEditablePropertyInfo info;
				info.m_name = st_PropNameObjectID;
				info.m_isReadOnly = true;
				info.m_type = GetTypeObject< Uint32 >();
				info.m_category = RED_NAME_CONSTEXPR( "Debug" );
				outRootProperties.Add( info );
			}
		}

		outRootProperties.RemoveIf( []( const rtti::ClassEditablePropertyInfo& info ) { return !info.m_isBrowsable; } );
	}

	const Bool ISerializableEditorView::GetCustomValue( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		const THandle< ISerializable > object = GetObject();

		// use the dynamic property interface
		if ( object )
		{
			if ( object->ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue ) )
			{
				RED_FATAL_ASSERT( outValue, "ReadCustomEditableProperty failed to set outValue to non-nullptr on success-path. This will crash later or cause other issues.");
				return true;
			}
		}

		return false;
	}

	const Bool ISerializableEditorView::SetCustomValue( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) const
	{
		const THandle< ISerializable > object = GetObject();

		// use the dynamic property interface
		if ( object )
		{
			if ( object->WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue ) )
				return true;
		}

		return false;
	}

	const Bool ISerializableEditorView::GetDebugValue( IRTTIContext& ctx, const CName propertyName, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		const THandle< ISerializable > object = GetObject();

		if ( propertyName == st_PropNameViewClass )
		{
			outValue = rtti::ValueHolder::CreateSingle( GetViewClass()->GetName().AsChar() );
			return true;
		}
		else if ( propertyName == st_PropNameObjectClass )
		{
			if ( !object )
				return false;

			outValue = rtti::ValueHolder::CreateSingle( object->GetClass()->GetName().AsChar() );
			return true;
		}
		else if ( propertyName == st_PropNameObjectID )
		{
			if ( !object )
				return false;

			outValue = rtti::ValueHolder::CreateSingle( ::ToStringDirect( object->GetID().Get() ).AsChar() );
			return true;
		}
		else
		{
			return false;
		}
	}

	void ISerializableEditorView::OnChildViewChanged() const
	{
		if ( GetParentView() )
		{
			GetParentView()->OnChildViewChanged();
		}
	}

	THandle< ISerializable > ISerializableEditorView::GetDefaultObject() const
	{
		if( !m_defaultObject )
		{
			m_defaultObject = GetViewClass()->CreateHandle< ISerializable >();
			if ( m_defaultObject )
			{
				m_defaultObject->Editor_OnInitialize();
			}
		}

		return m_defaultObject;
	}

} // tools
