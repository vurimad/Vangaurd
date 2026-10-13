/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableView.h"
#include "rttiPathParser.h"
#include "rttiPointerTypes.h"
#include "rttiArrayTypesImpl.h"
#include "rttiAccessPath.h"
#include "rttiValueHolder.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IResolverUserContext, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

namespace tools
{

	Bool IResolverUserContext::OnPrePaste( const SerializableID& ownerId, const rtti::AccessPath& path, const rtti::ValueHolder& inValue, rtti::ValuePtr& outValue ) const
	{
		return true;
	}

	red::UniquePtr< IResolverUserContext::IPrePropertyChange > IResolverUserContext::CreatePrePropertyChangeAction( const rtti::ClassType* viewClass, SerializableID ownerId, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) const
	{
		return nullptr;
	}


	IEditableView::IEditableView( const rtti::ClassType* classType )
		: m_classType( classType )
	{
		RED_FATAL_ASSERT( m_classType, "Invalid view class" );
	}

	IEditableView::~IEditableView()
	{
	}

	const rtti::ClassType* IEditableView::GetViewClass() const
	{
		return m_classType;
	}

	Bool IEditableView::ResetValue( IRTTIContext& ctx, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue ) const
	{
		rtti::PathParser parser( path );
		const rtti::IType* propType = m_classType;

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
			const auto propIt = std::find_if( props.Begin(), props.End(), [propertyName]( const rtti::ClassEditablePropertyInfo& info ) { return info.m_name == propertyName; } );
			if ( propIt != props.End() )
			{
				propType = propIt->m_type;
			}
			else
			{
				return false;
			}
		}

		red::SharedPtr< rtti::ValueHolder > defaultValue;
		THandle< ISerializable > defaultObject = m_classType->CreateHandle< ISerializable >();

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
				m_classType->ReadValue( IRTTIContext::GetDefault(), defaultObject.Get(), arrayPath, defaultValue );
			}
		}

		if ( !defaultValue )
		{
			red::SharedPtr< IEditableView > defaultObjectView = defaultObject->CreateView();
			defaultObjectView->GetValue( IRTTIContext::GetDefault(), path, defaultValue );
			if ( defaultValue == nullptr )
			{
				defaultValue = rtti::ValueHolder::CreateEmpty();
			}
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

} // tools
