/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTypes.h"
#include "dependencyTypes.h"

namespace dependency
{
//------------------------------------------------------------------------------

// Information about the tool used to generate, cook, or otherwise create the resource
// Most of these values are taken from the compiled code
struct ToolInfo
{
	CName name;					// RTTI class name of the tool class
	Uint32 version;				// Tool version as returned by the IGenerator::GetVersion method
	Uint32 fileVersion;			// The value of VER_CURRENT when the generator was compiled
};

//------------------------------------------------------------------------------

class OutputCollector;

struct OutputFileInfo : public FileInfo
{
	data::DepotLocation depot;

	OutputFileInfo();
};

using OutputFileList = red::DynArray< OutputFileInfo >;

BACKEND_DATA_API bool operator==( const OutputFileInfo& left, const OutputFileInfo& right );
BACKEND_DATA_API bool operator!=( const OutputFileInfo& left, const OutputFileInfo& right );
BACKEND_DATA_API bool operator<( const OutputFileInfo& left, const OutputFileInfo& right );

//------------------------------------------------------------------------------

class Collector;

class BACKEND_DATA_API ResourceInfo
{
public:
	static constexpr Uint32 CURRENT_VERSION = 10;

	ResourceInfo();
	~ResourceInfo();

	bool Load( Reader& reader );
	bool Save( Writer& writer ) const;

	bool Update( data::SystemContext& context );

	bool DumpDebugInfo( Writer& writer ) const;

	void SetPath( const res::ResourcePath& path );
	void SetTool( CName name, Uint32 version );
	void SetTool( const ToolInfo& tool );
	void SetOutputDepot( data::DepotLocation location );
	void SetPlatform( data::TargetPlatform platform );

	void SetDependencies( const Collector& collector );
	void CopyDependencies( const ResourceInfo& info );
	DependencyList& GetDependencies();
	const DependencyList& GetDependencies() const;

	const res::ResourcePath& GetPath() const;
	Timestamp GetTimeStamp() const;
	const Hash& GetHash() const;

	const ToolInfo& GetToolInfo() const;

	void SetOutputList( const dependency::OutputCollector& collector );
	void CopyOutputList( const ResourceInfo& info );
	OutputFileList& GetOutputList(); 
	const OutputFileList& GetOutputList() const; 
	OutputFileInfo GetOutputResourceInfo() const;

	data::DepotLocation GetOutputDepot() const;
	data::TargetPlatform GetPlatform() const;

	void ComputePrerequisitesHash();
	void SetPrerequisiteHash( const dependency::Hash& hash );
	const dependency::Hash& GetPrerequisiteHash() const;

private:
	void MakeOutputsUnique();

	FileInfo m_resource;

	ToolInfo m_tool;
	data::DepotLocation m_outputDepot;
	data::TargetPlatform m_platform;

	// Hash of all prerequisites details
	dependency::Hash m_prerequisiteHash;

	// List of dependencies for this resource
	DependencyList m_dependencies;

	// List of additional outputs created with this resource
	OutputFileList m_outputs;
};

//------------------------------------------------------------------------------

DependencyList CloneDependencyList( const DependencyList& list );

using DependencyFilterFunction = std::function< bool ( const DependencyPtr& ) >;
DependencyList FilterDependencyList( const DependencyList& list, DependencyFilterFunction filter );

//-----------------------------------------------------------------------------

RED_INLINE const res::ResourcePath& ResourceInfo::GetPath() const
{
	return m_resource.path;
}

RED_INLINE Timestamp ResourceInfo::GetTimeStamp() const
{
	return m_resource.timestamp;
}

RED_INLINE const Hash& ResourceInfo::GetHash() const
{
	return m_resource.hash;
}

RED_INLINE const ToolInfo& ResourceInfo::GetToolInfo() const
{
	return m_tool;
}

RED_INLINE DependencyList& ResourceInfo::GetDependencies()
{
	return m_dependencies;
}

RED_INLINE const DependencyList& ResourceInfo::GetDependencies() const
{
	return m_dependencies;
}

RED_INLINE OutputFileList& ResourceInfo::GetOutputList()
{
	return m_outputs;
}

RED_INLINE const OutputFileList& ResourceInfo::GetOutputList() const
{
	return m_outputs;
}

RED_INLINE data::TargetPlatform ResourceInfo::GetPlatform() const
{
	return m_platform;
}

RED_INLINE data::DepotLocation ResourceInfo::GetOutputDepot() const
{
	return m_outputDepot;
}

RED_INLINE void ResourceInfo::SetPrerequisiteHash( const dependency::Hash& hash )
{
	m_prerequisiteHash = hash;
}

RED_INLINE const dependency::Hash& ResourceInfo::GetPrerequisiteHash() const
{
	return m_prerequisiteHash;
}



//-----------------------------------------------------------------------------

RED_INLINE OutputFileInfo::OutputFileInfo()
	: FileInfo{ {}, 0ull, {} }
	, depot( data::DepotLocation::Source )
{
}

RED_INLINE bool operator==( const OutputFileInfo& left, const OutputFileInfo& right )
{
	return left.path == right.path &&
		left.timestamp == right.timestamp &&
		left.hash == right.hash && 
		left.depot == right.depot;
}

RED_INLINE bool operator!=( const OutputFileInfo& left, const OutputFileInfo& right )
{
	return !( left == right );
}

RED_INLINE bool operator<( const OutputFileInfo& left, const OutputFileInfo& right )
{
	if ( left.path == right.path )
	{
		return left.depot < right.depot;
	}
	return left.path < right.path;
}

//-----------------------------------------------------------------------------

} // dependency
