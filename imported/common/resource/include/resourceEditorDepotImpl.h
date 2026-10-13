/*
 * Copyright (c) 2007-2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "fileSystemResourceDepot.h"
#include "resourceDepotMappingConfig.h"

namespace res
{

struct DepotInitInfo
{
	// Absolute path to the engine root depot directory (loose files)
	red::AbsolutePath m_engineDepotRoot;

	// Absolute path to the game root depot directory (loose files)
	red::AbsolutePath m_gameDepotRoot;

	// (Optional) Absolute path to a mapping file to use instead of the default
	red::AbsolutePath m_customDepotMapping;
};

// Depot implementation for the editor based resource system
class RED_RESOURCE_API ResourceEditorDepot : public prv::FileSystemResourceDepot
{
public:
	explicit ResourceEditorDepot( const DepotInitInfo& initInfo );
	explicit ResourceEditorDepot( const red::DynArray< prv::DepotMapping >& mappings );
	virtual ~ResourceEditorDepot() override;

	ResourceEditorDepot( const ResourceEditorDepot& ) = delete;
	ResourceEditorDepot& operator=( const ResourceEditorDepot& ) = delete;

	virtual void FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const override;
	virtual void FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const override;

	virtual red::AbsolutePath TranslateResourcePath( const res::ResourcePath resourcePath ) const override;
	virtual red::AbsolutePath ResolveWritableResourcePath( const res::ResourcePath resourcePath ) const override;
	virtual res::ResourcePath TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const override;

	Uint32 GetResourceLevel( const res::ResourcePath& resourcePath ) const;

private:
	red::AbsolutePath ComputeAbsolutePath( const red::StringView& path, const prv::DepotMapping& mapping ) const;
	res::ResourcePath ComputeResourcePath( const red::StringView& path, const prv::DepotMapping& mapping ) const;

	virtual red::AbsolutePath TranslateExistingResourcePath( const red::StringView& path ) const override;
	virtual red::AbsolutePath TranslateWritableResourcePath( const red::StringView& path ) const override;

	// Array of mappings that we will search through to find the files for reading and writing.
	// The mappings will be matched in reverse order since we want latter entries to override existing ones. 
	// This is the same for reading and writing, only for iterating does the order not matter.
	// It is expected that overriding existing resource system paths will be rare and for knowledgable 
	// technical users. Usually paths will be mounted onto unique mount points.
	red::DynArray< prv::DepotMapping > m_mappings;
};

RED_RESOURCE_API ResourceEditorDepot* GetEditorDepot();
RED_RESOURCE_API void SetEditorDepot( ResourceEditorDepot* depot );

} // res
