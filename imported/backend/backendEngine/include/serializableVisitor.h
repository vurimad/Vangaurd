// Copyright (c) 2018 CD Projekt Red. All Rights Reserved.

#pragma once

#include "../../../common/redContainers/include/hashSet.h"

class ISerializable;

namespace rtti
{
    class Property;
    class ClassType;
    class IBaseArrayType;
	class IBasePointerType;
}

namespace tools
{
    class BACKEND_ENGINE_API ConstSerializableVisitor
	{
	public:
		using FuncClass = std::function<void( const ISerializable&, const rtti::AccessPath& )>;
		using FuncProp = std::function<void( const void*, const rtti::Property*, const rtti::AccessPath& )>;

		ConstSerializableVisitor( FuncClass visitClassClb, FuncProp visitPropertyClb );

		void Visit( const ISerializable& obj, const rtti::AccessPath& path = {} );

	private:
		void VisitClass( const void* owner, const rtti::ClassType* classType, const rtti::AccessPath& path );
		void VisitProperty( const void* owner, const rtti::Property* prop, const rtti::AccessPath& path );
		void VisitArray( const void* owner, const rtti::IBaseArrayType* arrayType, const rtti::AccessPath& path );
		void VisitHandle( const void* handle, const rtti::IBasePointerType* pointerType, const rtti::AccessPath& path );

		FuncClass OnVisitClassStoredInHandle;
		FuncProp OnVisitProperty;

		red::HashSet<const ISerializable*> m_visitedObjects;
	};

	class BACKEND_ENGINE_API SerializableVisitor
	{
	public:
		using FuncClass = std::function<void( ISerializable&, const rtti::AccessPath& )>;
		using FuncProp = std::function<void( void*, const rtti::Property*, const rtti::AccessPath& )>;

		SerializableVisitor( FuncClass visitClassClb, FuncProp visitPropertyClb );

		void Visit( ISerializable& obj, const rtti::AccessPath& path = {} );

	private:
		ConstSerializableVisitor m_visitor;
	};
}

