/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "rttiClass.h"

namespace rtti
{
	template< class Type >
	TAbstractClassType< Type >::TAbstractClassType( const CName name, Uint32 size, Uint32 flags )
		: AbstractClassType( name, size, flags | CF_Abstract | CF_Native )
	{}

	template< class Type >
	const red::memory::Pool& TAbstractClassType< Type >::GetInnerTypeMemoryPool() const
	{
		typedef typename red::memory::PoolResolver< Type, red::PoolRTTI >::PoolType PoolType;
		return PoolType::GetInstance();
	}

}