/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTableOfContent.h"
#include "rttiSystem.h"

namespace red
{
	PackageTableOfContent::PackageTableOfContent()
		: m_typeDictionary( red::PoolEngine() )
	{}

	PackageTableOfContent::~PackageTableOfContent()
	{}

	const rtti::IType * PackageTableOfContent::AcquireType( CName name )
	{
		auto iter = m_typeDictionary.Find( name );
		if( iter != m_typeDictionary.End() )
		{
			return iter.Value();
		}

		rtti::ITypeSystem& rttiSystem = GetRttiSystem();
		const rtti::IType * type = rttiSystem.FindType( name );
		m_typeDictionary.Insert( name, type );

		return type;	
	}
}
