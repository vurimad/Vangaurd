// Copyright (c) 2018 CD Projekt Red. All Rights Reserved.

#include "build.h"

#include "serializableVisitor.h"
#include "../../../common/redReflection/include/serializable.h"
#include "../../../common/redReflection/include/rttiProperty.h"
#include "../../../common/redReflection/include/rttiClass.h"
#include "../../../common/redReflection/include/rttiPointerTypes.h"
#include "../../../common/redReflection/include/rttiArrayTypes.h"

namespace tools
{
	ConstSerializableVisitor::ConstSerializableVisitor( FuncClass visitClassClb, FuncProp visitPropertyClb )
		: OnVisitClassStoredInHandle( visitClassClb )
		, OnVisitProperty( visitPropertyClb )
	{
		if( !OnVisitClassStoredInHandle )
		{
			OnVisitClassStoredInHandle = []( const ISerializable&, const rtti::AccessPath& ) {};
		}

		if( !OnVisitProperty )
		{
			OnVisitProperty = []( const void*, const rtti::Property*, const rtti::AccessPath& ){};
		}
	}

	void ConstSerializableVisitor::Visit( const ISerializable& obj, const rtti::AccessPath& path )
	{
		RED_ASSERT( m_visitedObjects.Empty(), "Cannot call root function before ending object visit." );

		VisitClass( &obj, obj.GetClass(), path );
		m_visitedObjects.Clear();
	}

	void ConstSerializableVisitor::VisitClass( const void* owner, const rtti::ClassType* classType, const rtti::AccessPath& path )
	{
		red::DynArray<const rtti::Property*> props;
		classType->GetProperties( props );

		for( const rtti::Property* prop : props )
		{
			VisitProperty( owner, prop, path[ prop->GetName() ] );
		}
	}

	void ConstSerializableVisitor::VisitProperty( const void* owner, const rtti::Property* prop, const rtti::AccessPath& path )
	{
		const rtti::IType* propType = prop->GetType();
		switch( propType->GetType() )
		{
		case ERTTITypeType::RT_Handle:
		case ERTTITypeType::RT_WeakHandle:
		{
			const auto* pointerType = static_cast< const rtti::IBasePointerType* >( propType );
			const void* handle = prop->GetOffsetPtr( owner );
			VisitHandle( handle, pointerType, path );
			break;
		}
		case ERTTITypeType::RT_Class:
		{
			const rtti::ClassType* propClass = static_cast< const rtti::ClassType* >( propType );
			const void* subOwner = prop->GetOffsetPtr( owner );
			VisitClass( subOwner, propClass, path );
			break;
		}
		case ERTTITypeType::RT_Array:
		case ERTTITypeType::RT_NativeArray:
		case ERTTITypeType::RT_StaticArray:
		{
			const rtti::IBaseArrayType* arrayType = static_cast< const rtti::IBaseArrayType* >( propType );
			const void* subOwner = prop->GetOffsetPtr( owner );
			VisitArray( subOwner, arrayType, path );
			break;
		}
		default:
			break;
		}
		OnVisitProperty( owner, prop, path );
	}

	void ConstSerializableVisitor::VisitArray( const void* owner, const rtti::IBaseArrayType* arrayType, const rtti::AccessPath& path )
	{
		const rtti::IType* innerType = arrayType->ArrayGetInnerType();

		switch( innerType->GetType() )
		{
		case ERTTITypeType::RT_Handle:
		case ERTTITypeType::RT_WeakHandle:
		{
			auto pointerType = static_cast< const rtti::IBasePointerType* >( innerType );
			const Uint32 size = arrayType->ArrayGetArraySize( owner );
			for( Uint32 i = 0; i < size; ++i )
			{
				const void* element = arrayType->ArrayGetArrayElement( owner, i );
				VisitHandle( element, pointerType, path[ i ] );
			}
			break;
		}
		case ERTTITypeType::RT_Class:
		{
			const rtti::ClassType* innerClass = static_cast< const rtti::ClassType* >( innerType );
			const Uint32 size = arrayType->ArrayGetArraySize( owner );
			for( Uint32 i = 0; i < size; ++i )
			{
				const void* element = arrayType->ArrayGetArrayElement( owner, i );
				VisitClass( element, innerClass, path[ i ] );
			}
			break;
		}
		default:
			break;
		}
	}

	void ConstSerializableVisitor::VisitHandle( const void* handle, const rtti::IBasePointerType* pointerType, const rtti::AccessPath& path )
	{
		const void* rawObj = pointerType->GetPointer( handle ).GetPointer();
		auto object = reinterpret_cast< const ISerializable* >( rawObj );
		if( object != nullptr && m_visitedObjects.Exist( object ) == false )
		{
			m_visitedObjects.Insert( object );

			OnVisitClassStoredInHandle( *object, path );
			VisitClass( object, object->GetClass(), path );
		}
	}

	SerializableVisitor::SerializableVisitor( FuncClass visitClassClb, FuncProp visitPropertyClb )
		: m_visitor{ 
			[visitClassClb]( const ISerializable& obj, const rtti::AccessPath& path ) { if( visitClassClb ) { visitClassClb( const_cast< ISerializable& >( obj ), path ); } },
			[visitPropertyClb]( const void* obj, const rtti::Property* prop, const rtti::AccessPath& path ) { if( visitPropertyClb ) { visitPropertyClb( const_cast< void* >( obj ), prop, path ); } },
		}
	{}

	void SerializableVisitor::Visit( ISerializable& obj, const rtti::AccessPath& path )
	{
		m_visitor.Visit( const_cast< const ISerializable& >( obj ), path );
	}
}
