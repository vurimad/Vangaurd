/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "resourceOptions.h"
#include "backendMemory.h"

#include "../../../common/redReflection/include/rttiClassDeclarationMacros.h"
#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redReflection/include/handle.h"

/// Resource creation options
class BACKEND_ENGINE_API FactoryOptions : public ResourceCommandOptions
{
	RTTI_DECLARE_TYPE( FactoryOptions );
	RED_BASE_CLASS(ResourceCommandOptions);

public:
	FactoryOptions() {}

	RED_INLINE FactoryOptions( const String& path )
		: m_path( path )
	{};

	String m_path;

	virtual Bool NeedOptions() const;
};

/// Base resource factory interface
class BACKEND_ENGINE_API IFactory
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	RTTI_DECLARE_POLYMORPHIC_TYPE( IFactory );

public:
	virtual ~IFactory() {}

	// Create resource
	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) = 0;
	
	virtual THandle<FactoryOptions> CreateOptions( const res::ResourcePath& path ) const;

	// Check support
	Bool SupportsResource( const rtti::ClassType* resourceClass ) const;

public:
	static IFactory* FindFactory( const rtti::ClassType* resourceClass );
	static void EnumFactoryClasses( red::DynArray< const rtti::ClassType* >& factoryClasses );

protected:
	const rtti::ClassType*		m_resourceClass;		// Resource class this creator supports
};

