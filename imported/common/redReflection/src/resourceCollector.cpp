/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceCollector.h"

#include "../../redFileSystem/include/nullFile.h"
#include "../../redContainers/include/algorithms.h"

ResourceCollector::ResourceCollector()
{
}

ResourceCollector::~ResourceCollector()
{
}

void ResourceCollector::Run( const THandle< ISerializable >& serializable )
{
	CNullFileWriter file;
	file.m_mapper = this;
	file.m_flags |= FF_Mapper;
	file.m_flags |= FF_ResourceCollector;
	serializable->OnSerialize( file );
}

void ResourceCollector::GetCollectedPaths( red::DynArray< String >& outPaths )
{
	FinalisePathList();
	outPaths.Reserve( outPaths.Size() + m_collectedPaths.Size() );
	for ( const auto& path : m_collectedPaths )
	{
		outPaths.PushBack( path.ToString() );
	}
}

void ResourceCollector::GetCollectedPaths( red::DynArray< res::ResourcePath >& outPaths )
{
	FinalisePathList();
	outPaths.PushBack( m_collectedPaths );
}

void ResourceCollector::MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex )
{
	if ( objectRef && !m_visitedObjects.Exist( objectRef.Get() ) )
	{
		m_visitedObjects.Insert( objectRef.Get() );

		CNullFileWriter file;
		file.m_mapper = this;
		file.m_flags |= FF_Mapper;
		file.m_flags |= FF_ResourceCollector;
		objectRef->OnSerialize( file );
	}
}

void ResourceCollector::MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex )
{
	CollectPath( path );
}

void ResourceCollector::MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex )
{
	CollectPath( path );
}

void ResourceCollector::CollectPath( const res::ResourcePath& path )
{
	if ( path.IsValid() )
	{
		m_collectedPaths.PushBack( path );
	}
}

void ResourceCollector::FinalisePathList()
{
	red::alg::RemoveDuplicates( m_collectedPaths );
}
