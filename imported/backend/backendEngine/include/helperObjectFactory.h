/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editableProxy.h"
#include "helperObject.h"

namespace tools
{
	using HelperObjectPtr = red::UniquePtr< tools::IHelperObject >;

	// Factory of helper objects
	class BACKEND_ENGINE_API HelperObjectFactory
	{
	public:
		class BACKEND_ENGINE_API Creator
		{
		public:
			using FactoryFunc = std::function< HelperObjectPtr ( const EditableProxyPtr& /* proxy */ )>;

			Creator( const rtti::ClassType* parentObjectType, const FactoryFunc& factoryFunc, Uint8 priority = 10u );

			// check if this helper object creator supports given objects type
			const Bool SupportsType( const rtti::ClassType* objectType ) const { return objectType->IsA( m_parentObjectType ); }

			// construct helper object for given parent object (referenced by proxy so we have more context)
			HelperObjectPtr Construct( const EditableProxyPtr& proxy ) const { return m_factoryFunction( proxy ); }

			const Uint8 GetPriority() const { return m_priority; }

		private:
			const rtti::ClassType* m_parentObjectType;
			Uint8 m_priority;
			FactoryFunc m_factoryFunction;

			friend class HelperObjectFactory;
		};

		// register a new helper object creator
		// this should be called once on application startup
		static const Bool RegisterCreator( const Creator& creator );

		// is helper object supported for given parent object?
		static const Bool IsTypeSupported( const rtti::ClassType* objectClass );

		// construct helper object for given parent object
		static HelperObjectPtr ConstructHelperObject( const EditableProxyPtr& proxy );

	private:
		static red::SortedArray< Creator >& GetCreators();
	};


	RED_INLINE static Bool operator<( const HelperObjectFactory::Creator& a, const HelperObjectFactory::Creator& b )
	{
		return a.GetPriority() < b.GetPriority();
	}
} // tools
