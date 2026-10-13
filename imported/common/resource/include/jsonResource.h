/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/resource.h"

class RED_RESOURCE_API JsonResource final : public CResource
{
	RTTI_DECLARE_TYPE( JsonResource );
	IMPLEMENT_RESOURCE_INTERFACE( "json", "json", "JsonResource" );
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	JsonResource();
	~JsonResource();

	template< typename T = ISerializable >
	THandle<T> GetRoot() const;

	void SetRoot( THandle<ISerializable> newRoot );

private:
	THandle<ISerializable> m_root;
};

template<typename T>
inline THandle<T> JsonResource::GetRoot() const
{
	if( m_root )
	{
		return Cast< T >( m_root );
	}

	return nullptr;
}
