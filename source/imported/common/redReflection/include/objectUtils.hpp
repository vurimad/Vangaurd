/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

template < class T > 
RED_INLINE T* FindParent( const ISerializable* object )
{
	ISerializable* parent = object->GetParent();
	while ( parent != nullptr )
	{
		if ( parent->IsA< T >() )
		{
			return static_cast< T* >( parent );
		}
		parent = parent->GetParent();
	}
	return nullptr;
}

} // red