/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "resourceOptions.h"
#include "backendMemory.h"
#include "../../../common/redFileSystem/include/fileFormat.h"

class BACKEND_ENGINE_API ResourceImportOptions : public ResourceCommandOptions
{
	RTTI_DECLARE_TYPE( ResourceImportOptions );
	RED_BASE_CLASS( ResourceCommandOptions );

public:
	ResourceImportOptions();
	virtual ~ResourceImportOptions();

	void SetSourcePath( const red::AbsolutePath& path );
	void SetResourcePath( const res::ResourcePath& path );

	red::AbsolutePath GetSourcePath() const;
	res::ResourcePath GetResourcePath() const;

	Bool m_skipThumbnail;

	// When the import is performed without calling out the UI (i.e. generating asset in external application and importing through the interop) we don't want to reuse the options from previous import
	Bool m_skipSuggestedOptions;  
private:
	// helper function for keeping backward compatibility with custom ResourceImportMetadata implementations
	// - used for extracting saved metadata and moving it to import options
	virtual void OnResourcePathSet();
	virtual void OnSourcePathSet();

	String m_resourcePath;
	String m_sourcePath;
};

typedef ResourceCommandResult ImportResult;

/// Base resource importer interface
class BACKEND_ENGINE_API IImporter
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	RTTI_DECLARE_POLYMORPHIC_TYPE( IImporter );

public:
	// Import resource
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle< ResourceImportOptions >& options ) = 0;

	virtual THandle< ResourceImportOptions > CreateOptions( const red::AbsolutePath& sourcePath );

	// Check support
	Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
	Bool SupportsFormat( const red::String& fileFormat ) const;

	red::String GetResourceExtension() const;
	const red::DynArray< CFileFormat >& GetSourceFileFormats() const;

	static IImporter* FindImporter( const rtti::ClassType* resourceClass, const red::String& fileFormat );
	static void EnumImportFormats( const rtti::ClassType* resourceClass, red::DynArray< CFileFormat >& formats );
	static void EnumImportClasses( red::DynArray< const rtti::ClassType* >& importClasses );

protected:
	const rtti::ClassType* m_resourceClass; // Resource class this importer supports
	red::DynArray< CFileFormat > m_formats; // Supported file formats
};
