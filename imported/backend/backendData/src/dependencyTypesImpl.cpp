/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyTypesImpl.h"
#include "dataSystemContext.h"
#include "dataPlatform.h"
#include "dependencyCollector.h"
#include "dependencyOutputCollector.h"
#include "dependencyCache.h"

#include "dependencyReader.h"
#include "dependencyWriter.h"

#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"

namespace dependency
{

//------------------------------------------------------------------------------

DependencyList CloneDependencyList( const DependencyList& list )
{
	DependencyList result;
	result.Reserve( list.Size() );
	for ( const DependencyPtr& dep : list )
	{
		result.PushBack( dep->Clone() );
	}
	return result;
}

DependencyList FilterDependencyList( const DependencyList& list, DependencyFilterFunction filter )
{
	DependencyList result;
	result.Reserve( list.Size() );
	for ( const DependencyPtr& dep : list )
	{
		if ( filter( dep ) )
		{
			result.PushBack( dep );
		}
	}
	return result;
}

//------------------------------------------------------------------------------

ResourceInfo::ResourceInfo()
	: m_platform( data::TargetPlatform::PC )
	, m_outputDepot( data::DepotLocation::Cooked )
{
}

ResourceInfo::~ResourceInfo()
{
}

//------------------------------------------------------------------------------

namespace
{

red::Map< CName, const rtti::ClassType* > CreateDependencyClasses()
{
	red::DynArray< const rtti::ClassType* > klasses;
	GetRttiSystem().EnumClasses( ClassID< Dependency >(), klasses );

	red::Map< CName, const rtti::ClassType* > result;
	result.Reserve( klasses.Size() );

	for ( const rtti::ClassType* klass : klasses )
	{
		result.Insert( klass->GetName(), klass );
	}

	return result;
}

}

bool ResourceInfo::Load( Reader& reader )
{
	static red::Map< CName, const rtti::ClassType* > s_dependencyClasses{ CreateDependencyClasses() };

	reader.ReadPath( "path", m_resource.path );
	reader.ReadTimestamp( "timestamp", m_resource.timestamp );
	reader.ReadHash( "hash", m_resource.hash );

	{
		auto toolScope = reader.StartScope( "tool" );
		auto& toolReader = *toolScope;
		toolReader.ReadName( "name", m_tool.name );
		toolReader.ReadNumber( "version", m_tool.version );
		toolReader.ReadNumber( "file version", m_tool.fileVersion );
	}

	reader.ReadEnum< data::DepotLocation >( "output location", m_outputDepot );
	reader.ReadEnum< data::TargetPlatform >( "target platform", m_platform );

	reader.ReadHash( "prerequisite hash", m_prerequisiteHash );

	{
		auto depsScope = reader.StartArray( "dependencies" );
		Uint32 depsCount = depsScope->Size();
		for ( Uint32 i = 0; i < depsCount; ++i )
		{
			auto depScope = depsScope->GetScope( i );
			auto& depReader = *depScope;

			CName className;
			depReader.ReadName( "kind", className );

			auto depClass = s_dependencyClasses.FindPtr( className );
			if ( depClass )
			{
				auto dep = red::UniquePtr< Dependency >( (*depClass)->CreateObject< Dependency >() );
				dep->Load( depReader );
				m_dependencies.PushBack( std::move( dep ) );
			}
			else
			{
				RED_LOG_ERROR( "Could not read dependency of kind: %hs", className.AsChar() );
				return false;
			}
		}
		std::sort( m_dependencies.Begin(), m_dependencies.End() );
	}

	{
		auto outputsScope = reader.StartArray( "outputs" );
		Uint32 outputsCount = outputsScope->Size();
		for ( Uint32 i = 0; i < outputsCount; ++i )
		{
			auto outputScope = outputsScope->GetScope( i );
			auto& outputReader = *outputScope;

			auto& output = m_outputs.EmplaceBack();
			outputReader.ReadPath( "path", output.path );
			outputReader.ReadTimestamp( "timestamp", output.timestamp );
			outputReader.ReadHash( "hash", output.hash );
			outputReader.ReadEnum< data::DepotLocation >( "depot", output.depot );
		}
		MakeOutputsUnique();
	}

	return true;
}

bool ResourceInfo::Save( Writer& writer ) const
{
	writer.WritePath( "path", m_resource.path );
	writer.WriteTimestamp( "timestamp", m_resource.timestamp );
	writer.WriteHash( "hash", m_resource.hash );

	{
		auto toolScope = writer.StartScope( "tool" );
		auto& toolWriter = *toolScope;
		toolWriter.WriteName( "name", m_tool.name );
		toolWriter.WriteNumber( "version", m_tool.version );
		toolWriter.WriteNumber( "file version", m_tool.fileVersion );
	}

	writer.WriteEnum< data::DepotLocation >( "output location", m_outputDepot );
	writer.WriteEnum< data::TargetPlatform >( "target platform", m_platform );

	writer.WriteHash( "prerequisite hash", m_prerequisiteHash );

	{
		auto depsScope = writer.StartArray( "dependencies", m_dependencies.Size() );
		auto& depsWriter = *depsScope;
		for ( const auto& dep : m_dependencies )
		{
			auto depScope = depsWriter.StartScope( {} );
			auto& depWriter = *depScope;
			depWriter.WriteName( "kind", dep->GetClass()->GetName() );
			dep->Save( depWriter );
		}
	}

	{
		auto outputScope = writer.StartArray( "outputs", m_outputs.Size() );
		auto& outputWriter = *outputScope;
		for ( const auto& outInfo : m_outputs )
		{
			auto outScope = outputWriter.StartScope( {} );
			auto& outWriter = *outScope;
			outWriter.WritePath( "path", outInfo.path );
			outWriter.WriteTimestamp( "timestamp", outInfo.timestamp );
			outWriter.WriteHash( "hash", outInfo.hash );
			outWriter.WriteEnum< data::DepotLocation >( "depot", outInfo.depot );
		}
	}

	return true;
}

bool ResourceInfo::DumpDebugInfo( Writer& writer ) const
{
	// Save default information as in a normal save, this would be getting output to JSON or some other format
	Save( writer );

	{
		auto debugScope = writer.StartArray( "debug_info", m_dependencies.Size() );
		auto& debugWriter = *debugScope;
		for ( const auto& dep : m_dependencies )
		{
			auto depScope = debugWriter.StartScope( {} );
			auto& depWriter = *depScope;
			dep->DumpDebugInfo( depWriter );
		}
	}

	return true;
}

bool ResourceInfo::Update( data::SystemContext& context )
{
	auto& cache = context.GetDepsCache( GetOutputDepot() );
	return cache.Lookup( m_resource );
}

void ResourceInfo::SetPath( const res::ResourcePath& path )
{
	m_resource.path = path;
}

void ResourceInfo::SetTool( CName name, Uint32 version )
{
	m_tool.name = name;
	m_tool.version = version;
	m_tool.fileVersion = VER_CURRENT;
}

void ResourceInfo::SetTool( const ToolInfo& tool )
{
	m_tool = tool;
}

void ResourceInfo::SetOutputDepot( data::DepotLocation location )
{
	m_outputDepot = location;
}

void ResourceInfo::SetPlatform( data::TargetPlatform platform )
{
	m_platform = platform;
}

void ResourceInfo::SetDependencies( const Collector& collector )
{
	m_dependencies = collector.Internal_GetDependencyList();
	std::sort( m_dependencies.Begin(), m_dependencies.End() );
}

void ResourceInfo::CopyDependencies( const ResourceInfo& info )
{
	m_dependencies = CloneDependencyList( info.GetDependencies() );
	std::sort( m_dependencies.Begin(), m_dependencies.End() );
}

void ResourceInfo::SetOutputList( const OutputCollector& collector )
{
	m_outputs = collector.GetOutputList();
	MakeOutputsUnique();
}

void ResourceInfo::CopyOutputList( const ResourceInfo& info )
{
	m_outputs = info.m_outputs;
	std::sort( m_outputs.Begin(), m_outputs.End() );
}

OutputFileInfo ResourceInfo::GetOutputResourceInfo() const
{
	OutputFileInfo result;
	result.path = m_resource.path;
	result.hash = m_resource.hash;
	result.timestamp = m_resource.timestamp;
	result.depot = m_outputDepot;
	return result;
}

void ResourceInfo::ComputePrerequisitesHash()
{
#pragma pack( push, 1 )
	struct Header
	{
		Uint64 pathHash;
		red::CNameHash toolNameHash;
		Uint32 toolVersion;
		Uint32 fileVersion;
		data::TargetPlatform platform;
	};
	struct Entry
	{
		Uint64 pathHash;
		dependency::Hash contentHash;
	};
#pragma pack( pop )

	const Uint32 bufferSize = sizeof( Header ) + m_dependencies.Size() * sizeof( Entry );
	red::Blob buffer{ bufferSize };

	Header& header = buffer.As< Header >();
	header.pathHash = m_resource.path.GetHash();
	header.toolNameHash = m_tool.name.GetHash();
	header.toolVersion = m_tool.version;
	header.fileVersion = m_tool.fileVersion;
	header.platform = m_platform;

	if ( m_dependencies.Size() > 0 )
	{
		red::ArraySpan< Entry > entries = red::MakeArraySpan( buffer.Pointer< Entry >( sizeof( Header ) ), m_dependencies.Size() );
		for ( Uint32 i = 0; i < m_dependencies.Size(); ++i )
		{
			const auto& dep = m_dependencies[ i ];
			Entry& entry = entries[ i ];
			entry.pathHash = dep->GetPathHash();
			entry.contentHash = dep->GetHash();
		}
	}

	m_prerequisiteHash = dependency::Hash::Compute( buffer.View() );
}

void ResourceInfo::MakeOutputsUnique()
{
	std::sort( m_outputs.Begin(), m_outputs.End() );
	auto iter = std::unique( m_outputs.Begin(), m_outputs.End(), []( const OutputFileInfo& left, const OutputFileInfo& right ) -> bool
	{
		return left.path == right.path && left.depot == right.depot;
	} );
	m_outputs.Remove( iter, m_outputs.End() );
}

} // dependency
