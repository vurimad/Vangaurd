/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "helperObject.h"
#include "helperObjectFactory.h"


namespace tools
{

	HelperObjectFactory::Creator::Creator( const rtti::ClassType* parentObjectType, const FactoryFunc& factoryFunc, Uint8 priority )
		: m_parentObjectType( parentObjectType )
		, m_factoryFunction( factoryFunc )
		, m_priority( priority )
	{
		RED_FATAL_ASSERT( parentObjectType );
		RED_FATAL_ASSERT( factoryFunc );
	}

	red::SortedArray< HelperObjectFactory::Creator >& HelperObjectFactory::GetCreators()
	{
		static red::SortedArray< Creator > creators{ red::PoolBackend() };
		return creators;
	}

	const Bool HelperObjectFactory::RegisterCreator( const Creator& creator )
	{
		GetCreators().Insert( creator );
		return true;
	}

	const Bool HelperObjectFactory::IsTypeSupported( const rtti::ClassType* objectClass )
	{
		for ( const Creator& creator : GetCreators() )
		{
			if ( creator.SupportsType( objectClass ) )
			{
				return true;
			}
		}

		return false;
	}

	HelperObjectPtr HelperObjectFactory::ConstructHelperObject( const EditableProxyPtr& proxy )
	{
		if ( const THandle< ISerializable >& object = proxy->GetSerializable() )
		{
			for ( const Creator& creator : GetCreators() )
			{
				if ( object->IsA( creator.m_parentObjectType ) )
				{
					return creator.Construct( proxy );
				}
			}
		}

		return nullptr;
	}

} // tools
