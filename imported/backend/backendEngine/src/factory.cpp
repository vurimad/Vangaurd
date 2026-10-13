/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "factory.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/rttiSystem.h"
#include "../../../common/redReflection/include/rttiClassBuilder.h"

using red::DynArray;

RTTI_BEGIN_ABSTRACT_TYPE( IFactory );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( FactoryOptions );
RTTI_PARENT_TYPE(ISerializable);
	RTTI_PROPERTY( m_path );
RTTI_END_TYPE();

Bool FactoryOptions::NeedOptions() const
{
	return false;
}

THandle<FactoryOptions> IFactory::CreateOptions( const res::ResourcePath& path ) const
{
	return CreateHandle< FactoryOptions >( path.ToString() );
}

Bool IFactory::SupportsResource( const rtti::ClassType* resourceClass ) const
{
	return m_resourceClass == resourceClass;
}

IFactory* IFactory::FindFactory( const rtti::ClassType* resourceClass )
{
	// Request all factory classes
	DynArray< const rtti::ClassType* > factoryClasses;
	GetRttiSystem().EnumClasses( ClassID<IFactory>(), factoryClasses );

	// Linear search :P
	for ( Uint32 i=0; i<factoryClasses.Size(); i++ )
	{
		IFactory* factory = factoryClasses[i]->GetDefaultObject< IFactory >();
		if ( factory->SupportsResource( resourceClass ) )
		{
			return factory;
		}
	}

	// Format not supported
	return NULL;
}

void IFactory::EnumFactoryClasses( DynArray< const rtti::ClassType* >& factoryClasses )
{
	// Request factory classes
	DynArray< const rtti::ClassType* > rawFactoryClasses;
	GetRttiSystem().EnumClasses( ClassID<IFactory>(), rawFactoryClasses );

	// Extract supported resources
	for ( const auto* factoryClass : rawFactoryClasses )
	{
		IFactory* factory = factoryClass->GetDefaultObject< IFactory >();
		if ( factory->m_resourceClass && factory->m_resourceClass != ClassID< CResource >() )
		{
			DynArray< const rtti::ClassType* > rawResourceClasses;
			GetRttiSystem().EnumClasses( factory->m_resourceClass, rawResourceClasses );

			// only output non-abstract resource classes - note it may be more than one
			for ( const auto* resourceClass : rawResourceClasses )
				factoryClasses.PushBack( resourceClass );
		}
	}
}
