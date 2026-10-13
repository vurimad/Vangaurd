/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceDependency.h"
#include "dataSystemContext.h"
#include "dataTypesImpl.h"
#include "dependencyCache.h"
#include "dependencyReader.h"
#include "dependencyWriter.h"

dependency::DependencyUniquePtr MakeSourceDependency( const res::ResourcePath& path, dependency::Requirement require )
{
	RED_FATAL_ASSERT( path.IsValid(), "Must provide a valid resource path for a resource dependency" );
	return red::CreateUniquePtr< dependency::ResourceDependency >( path, data::DepotLocation::Source, require );
}

dependency::DependencyUniquePtr MakeSourceDependency_NoRescan( const res::ResourcePath& path, dependency::Requirement require )
{
	RED_FATAL_ASSERT( path.IsValid(), "Must provide a valid resource path for a resource dependency" );
	auto dependency = red::CreateUniquePtr< dependency::ResourceDependency >( path, data::DepotLocation::Source, require );
	dependency->SetRescan( false );
	return dependency;
}

dependency::DependencyUniquePtr MakeBuiltDependency( const res::ResourcePath& path, data::DepotLocation depot, dependency::Requirement require )
{
	RED_FATAL_ASSERT( path.IsValid(), "Must provide a valid resource path for a resource dependency" );
	auto dependency = red::CreateUniquePtr< dependency::ResourceDependency >( path, depot, require );
	dependency->MarkAsGenerated();
	return dependency;
}

dependency::DependencyUniquePtr MakeBuiltChildDependency( const res::ResourcePath& path, data::DepotLocation depot, dependency::Requirement require )
{
	RED_FATAL_ASSERT( path.IsValid(), "Must provide a valid resource path for a resource dependency" );
	auto dependency = red::CreateUniquePtr< dependency::ResourceDependency >( path, depot, require );
	dependency->MarkAsGenerated();
	dependency->MarkAsChild();
	return dependency;
}

//-----------------------------------------------------------------------------

RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceDependency, dependency )
RTTI_PARENT_TYPE( Dependency )
RTTI_END_TYPE()

namespace dependency
{

ResourceDependency::ResourceDependency()
	: Dependency()
{
}

ResourceDependency::ResourceDependency( const res::ResourcePath& path, data::DepotLocation depot, Requirement requireLevel )
	: Dependency( depot, requireLevel )
	, m_path( path )
{
}

ResourceDependency::~ResourceDependency()
{
}

ResourceDependency::ResourceDependency( const ResourceDependency& ) = default;
ResourceDependency::ResourceDependency( ResourceDependency&& ) = default;
ResourceDependency& ResourceDependency::operator= ( const ResourceDependency& ) = default;
ResourceDependency& ResourceDependency::operator= ( ResourceDependency&& ) = default;

DependencyUniquePtr ResourceDependency::Clone() const
{
	return red::CreateUniquePtr< ResourceDependency >( *this );
}

bool ResourceDependency::Load( Reader& reader )
{
	reader.ReadPath( "path", m_path );

	Dependency::Load( reader );

	reader.ReadBool( "generated", m_generated );
	reader.ReadBool( "child", m_child );
	reader.ReadBool( "rescan", m_rescan );

	return true;
}

bool ResourceDependency::Save( Writer& writer ) const
{
	writer.WritePath( "path", m_path );

	Dependency::Save( writer );

	writer.WriteBool( "generated", m_generated );
	writer.WriteBool( "child", m_child );
	writer.WriteBool( "rescan", m_rescan );

	return true;
}

void ResourceDependency::Update( data::SystemContext& context )
{
	auto& cache = context.GetDepsCache( GetDepotLocation() );

	FileInfo self{ m_path, m_timestamp, m_hash };
	const bool result = cache.Lookup( self );
	m_path = self.path;
	m_timestamp = self.timestamp;
	m_hash = self.hash;

	SetValid( result );
}

bool ResourceDependency::DumpDebugInfo( Writer& writer ) const
{
	return true;
}

red::StringView ResourceDependency::GetPathString() const
{
	return m_path.ToStringView();
}

Uint64 ResourceDependency::GetPathHash() const
{
	return m_path.GetHash();
}

res::ResourcePath ResourceDependency::GetResourcePath() const
{
	return m_path;
}

void ResourceDependency::MarkAsGenerated()
{
	m_generated = true;
	m_rescan = false;
}

void ResourceDependency::MarkAsChild()
{
	m_child = true;
}

void ResourceDependency::SetRescan( bool rescan )
{
	m_rescan = rescan;
}


} // dependency
