/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "jsonResource.h"
#include "jsonFormatResourceLoader.h"

RTTI_BEGIN_TYPE( JsonResource );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_root ).readOnly();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////

JsonResource::JsonResource()
{
}

JsonResource::~JsonResource()
{
}

void JsonResource::SetRoot( THandle<ISerializable> newRoot )
{
	m_root = newRoot;
}

class JsonResourceLoader : public json::JsonFormatResourceLoader< JsonResource >
{
public:
	JsonResourceLoader() = default;
};

RED_RESOURCE_API void RegisterJsonResourceLoader()
{
	serialization::ILoader::RegisterCustomFactory< JsonResourceLoader >( "json" );
}