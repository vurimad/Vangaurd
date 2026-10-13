/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCacheServer.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( DepsServer, data, prv );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( FileServer, data, prv );
RTTI_END_TYPE();

namespace data
{
namespace prv
{

DepsServer::~DepsServer() = default;

DepsServerPtr DepsServer::Create( CName name )
{
	const rtti::ClassType* serverClass = GetRttiSystem().FindClass( name );
	if ( serverClass )
	{
		return red::UniquePtr< DepsServer >( serverClass->CreateObject< DepsServer >() );
	}
	return nullptr;
}

FileServer::~FileServer() = default;

FileServerPtr FileServer::Create( CName name )
{
	const rtti::ClassType* serverClass = GetRttiSystem().FindClass( name );
	if ( serverClass )
	{
		return red::UniquePtr< FileServer >( serverClass->CreateObject< FileServer >() );
	}
	return nullptr;
}

} // prv
} // data
