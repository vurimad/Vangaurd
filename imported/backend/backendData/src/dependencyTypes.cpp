/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyTypes.h"

#include "dependencyReader.h"
#include "dependencyWriter.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( Requirement, dependency );
RTTI_ENUM_OPTION( Normal );
RTTI_ENUM_OPTION( Required );
RTTI_ENUM_OPTION( Optional );
RTTI_END_ENUM();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Dependency, dependency );
// No members for now as we only need RTTI to create the correct object
// We will call a virtual method to load/save them
// Need ToolInfo and ResourceInfo to be RTTI (which means ResourcePath needs to be RTTI)
// before we can use that serialisation for these dependency objects
RTTI_END_TYPE();

namespace dependency
{

Dependency::Dependency()
	: m_timestamp( 0ull )
	, m_depot( data::DepotLocation::Source )
	, m_require( Requirement::Normal )
	, m_generated( false )
	, m_child( false )
	, m_rescan( true )
	, m_valid( false )
{
}

Dependency::Dependency( data::DepotLocation depot, Requirement require )
	: m_timestamp( 0ull )
	, m_depot( depot )
	, m_require( require )
	, m_generated( false )
	, m_child( false )
	, m_rescan( true )
	, m_valid( false )
{
}

Dependency::~Dependency()
{
}

Dependency::Dependency( const Dependency& ) = default;
Dependency::Dependency( Dependency&& ) = default;
Dependency& Dependency::operator= ( const Dependency& ) = default;
Dependency& Dependency::operator= ( Dependency&& ) = default;

bool Dependency::Load( Reader& reader )
{
	reader.ReadTimestamp( "timestamp", m_timestamp );
	reader.ReadHash( "hash", m_hash );

	reader.ReadEnum< data::DepotLocation >( "depot", m_depot );
	reader.ReadEnum< dependency::Requirement >( "require", m_require );

	return true;
}

bool Dependency::Save( Writer& writer ) const
{
	writer.WriteTimestamp( "timestamp", m_timestamp );
	writer.WriteHash( "hash", m_hash );

	writer.WriteEnum< data::DepotLocation >( "depot", m_depot );
	writer.WriteEnum< dependency::Requirement >( "require", m_require );

	return true;
}

void Dependency::SetTimestampHash( Timestamp timestamp, Hash hash )
{
	m_timestamp = timestamp;
	m_hash = hash;
}

void Dependency::SetValid( bool valid )
{
	m_valid = valid;
}

bool Dependency::DumpDebugInfo( Writer& writer ) const
{
	return true;
}

res::ResourcePath Dependency::GetResourcePath() const
{
	return {};
}

} // dependency
