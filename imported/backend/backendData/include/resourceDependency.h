/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyTypes.h"

// Create a dependency on an uncooked resource in the source depot
// This resource will cause the dependencies to be rescanned if it is changed
BACKEND_DATA_API dependency::DependencyUniquePtr MakeSourceDependency( const res::ResourcePath& path, dependency::Requirement require = dependency::Requirement::Normal );

// Create a dependency on an uncooked resource in the source depot
// This resource will not cause the dependencies to be rescanned if it is changed
BACKEND_DATA_API dependency::DependencyUniquePtr MakeSourceDependency_NoRescan( const res::ResourcePath& path, dependency::Requirement require = dependency::Requirement::Normal );

// Create a dependency on a built/generated resource in either depot
BACKEND_DATA_API dependency::DependencyUniquePtr MakeBuiltDependency( const res::ResourcePath& path, data::DepotLocation depot = data::DepotLocation::Source, dependency::Requirement require = dependency::Requirement::Normal );

// Create a dependency on a build/generated resource with a parent/child relationship
// This means that this resource owns the lifetime of the dependency, and if it is no longer required the child resource will be cleaned up by the system
// It is intended to be used in root -> branch -> leaf dependency relationships for generators
BACKEND_DATA_API dependency::DependencyUniquePtr MakeBuiltChildDependency( const res::ResourcePath& path, data::DepotLocation depot = data::DepotLocation::Source, dependency::Requirement require = dependency::Requirement::Normal );

namespace dependency
{

//-----------------------------------------------------------------------------

 // Dependency on a single resource that may be from either the source or cooked depot
 // This dependency can also be generated itself and can cause a dependency resacan to happen if changed
class BACKEND_DATA_API ResourceDependency : public Dependency
{
	RTTI_DECLARE_TYPE( ResourceDependency );
public:
	ResourceDependency();
	ResourceDependency( const res::ResourcePath& path, data::DepotLocation depot, Requirement requireLevel );
	virtual ~ResourceDependency();

	ResourceDependency( const ResourceDependency& );
	ResourceDependency( ResourceDependency&& );
	ResourceDependency& operator= ( const ResourceDependency& );
	ResourceDependency& operator= ( ResourceDependency&& );

	virtual DependencyUniquePtr Clone() const override;

	virtual bool Load( Reader& reader ) override;
	virtual bool Save( Writer& writer ) const override;

	virtual void Update( data::SystemContext& context ) override;

	virtual bool DumpDebugInfo( Writer& writer ) const override;

	virtual red::StringView GetPathString() const override;
	virtual Uint64 GetPathHash() const override;

	virtual res::ResourcePath GetResourcePath() const override;

	using Dependency::SetTimestampHash;
	void MarkAsGenerated();
	void MarkAsChild();
	void SetRescan( bool rescan );

private:
	res::ResourcePath m_path;
};

//-----------------------------------------------------------------------------

} // dependency
