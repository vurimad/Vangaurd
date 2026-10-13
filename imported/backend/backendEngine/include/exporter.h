/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "backendMemory.h"

#include "../../../common/redFileSystem/include/fileFormat.h"

// Base resource exporter interface
class BACKEND_ENGINE_API IExporter
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	RTTI_DECLARE_POLYMORPHIC_TYPE( IExporter );

public:
	// Export options
	struct ExportOptions
	{
		THandle<CResource> m_resource;		// Resource that should be exported
		red::String m_saveFilePath;			// Path to file we should Export the resource to
		CFileFormat m_saveFileFormat;		// Save file format
		Uint32 m_lodToUse;					// Defines mesh's lod to be exported

		ExportOptions()
			: m_resource( nullptr )
			, m_lodToUse( 0 )
		{};
	};

	// Export resource
	virtual Bool DoExport( const ExportOptions& options ) = 0;

	// Check support
	Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
	Bool SupportsFormat( const red::String& fileFormat ) const;

	static IExporter* FindExporter( const rtti::ClassType* resourceClass, const red::String& fileFormat );
	static void EnumExportFormats( const rtti::ClassType* resourceClass, red::DynArray< CFileFormat >& formats );
	static void EnumExportClasses( red::DynArray< const rtti::ClassType* >& exportClasses );

protected:
	const rtti::ClassType*					m_resourceClass;		// Resource class this Exporter supports
	red::DynArray< CFileFormat >			m_formats;				// Supported file formats
};

