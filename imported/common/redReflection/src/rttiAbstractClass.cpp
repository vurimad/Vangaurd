/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "rttiAbstractClass.h"

namespace rtti
{

	AbstractClassType::AbstractClassType( const CName name, Uint32 size, Uint32 flags )
		: ClassType( name, size, flags | CF_Abstract | CF_Native )
	{}

	void AbstractClassType::OnConstruct( void* ) const
	{
		RED_HALT( "Cannot instantiate abstact classes !" );
	}

	void AbstractClassType::OnDestruct( void* ) const
	{
		RED_HALT( "Cannot destroy abstact classes !" );
	}

	Bool AbstractClassType::Compare( const void*, const void*, Uint32 ) const
	{
		RED_HALT( "Cannot operate on abstact classes!" );
		return false;
	}

	void AbstractClassType::Copy( void*, const void* ) const
	{
		RED_HALT( "Cannot operate on abstact classes!" );
	}

	void* AbstractClassType::AllocateClassBuffer() const
	{
		RED_HALT( "Cannot operate on abstact classes!" );
		return nullptr;
	}
}