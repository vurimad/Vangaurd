/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "exporter.h"

RTTI_BEGIN_ABSTRACT_TYPE( IExporter );
RTTI_END_TYPE();


Bool IExporter::SupportsResource( const rtti::ClassType* resourceClass ) const
{
	return resourceClass->IsA( m_resourceClass );
}

Bool IExporter::SupportsFormat( const String& fileFormat ) const
{
	// Linear search :P
	for ( Uint32 i=0; i<m_formats.Size(); i++ )
	{
		if ( m_formats[i].GetExtension().EqualsNC( fileFormat ) )
		{
			return true;
		}
	}

	// Not supported
	return false;
}

IExporter* IExporter::FindExporter( const rtti::ClassType* resourceClass, const String& fileFormat )
{
	// Request Exporter classes
	red::DynArray< const rtti::ClassType* > ExporterClasses;
	GetRttiSystem().EnumClasses( ClassID<IExporter>(), ExporterClasses );

	// Linear search :P
	for ( Uint32 i=0; i<ExporterClasses.Size(); i++ )
	{
		IExporter* Exporter = ExporterClasses[i]->GetDefaultObject< IExporter >();
		if ( Exporter->SupportsResource( resourceClass ) )
		{
			if ( Exporter->SupportsFormat( fileFormat ))
			{
				return Exporter;
			}
		}
	}

	// Format not supported
	return NULL;
}

void IExporter::EnumExportClasses( red::DynArray< const rtti::ClassType* >& exportClasses )
{
	// Request Exporter classes
	red::DynArray< const rtti::ClassType* > rawExporterClasses;
	GetRttiSystem().EnumClasses( ClassID<IExporter>(), rawExporterClasses );

	// Linear search :P
	for ( const auto* exporterClass : rawExporterClasses )
	{
		IExporter* exporter = exporterClass->GetDefaultObject< IExporter >();
		red::alg::PushBackUnique( exportClasses, exporter->m_resourceClass );
	}
}

void IExporter::EnumExportFormats( const rtti::ClassType* resourceClass, red::DynArray< CFileFormat >& formats )
{
	// Request Exporter classes
	red::DynArray< const rtti::ClassType* > ExporterClasses;
	GetRttiSystem().EnumClasses( ClassID<IExporter>(), ExporterClasses );

	// Linear search :P
	for ( Uint32 i=0; i<ExporterClasses.Size(); i++ )
	{
		IExporter* Exporter = ExporterClasses[i]->GetDefaultObject< IExporter >();
		if ( Exporter->SupportsResource( resourceClass ) )
		{
			formats.PushBack( Exporter->m_formats );
		}		
	}
}
