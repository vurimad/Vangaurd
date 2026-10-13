/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "importer.h"

#include "../../../common/redReflection/include/resource.h"

using red::DynArray;
using red::String;

RTTI_BEGIN_ABSTRACT_TYPE( IImporter );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( ResourceImportOptions );
RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_PROPERTY_CATEGORY( "Paths" );
	RTTI_PROPERTY( m_resourcePath ).readOnly();
	RTTI_PROPERTY( m_sourcePath ).readOnly();
RTTI_END_TYPE();


ResourceImportOptions::ResourceImportOptions()
	: m_skipThumbnail( false )
	, m_skipSuggestedOptions( false )
{}

ResourceImportOptions::~ResourceImportOptions()
{}

void ResourceImportOptions::SetSourcePath( const red::AbsolutePath& path )
{
	m_sourcePath = path.AsString();
	OnSourcePathSet();
}

void ResourceImportOptions::SetResourcePath( const res::ResourcePath& path )
{
	m_resourcePath = path.ToString();
	OnResourcePathSet();
}

red::AbsolutePath ResourceImportOptions::GetSourcePath() const
{
	if( !m_sourcePath.Empty() )
	{
		return red::AbsolutePath::CreateFilePath( m_sourcePath );
	}
	else
	{
		return red::AbsolutePath();
	}
}

res::ResourcePath ResourceImportOptions::GetResourcePath() const
{
	return res::ResourcePath::Build( m_resourcePath );
}

void ResourceImportOptions::OnResourcePathSet()
{
}

void ResourceImportOptions::OnSourcePathSet()
{
}

//////////////////////////////////////////////////////////////////////////////////////////

THandle<ResourceImportOptions> IImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	return CreateHandle<ResourceImportOptions>();
}

Bool IImporter::SupportsResource( const rtti::ClassType* resourceClass ) const
{
	return m_resourceClass == resourceClass;
}

Bool IImporter::SupportsFormat( const String& fileFormat ) const
{
	for ( const auto& format : m_formats )
	{
		if ( format.GetExtension().EqualsNC( fileFormat ) )
		{
			return true;
		}
	}

	// Not supported
	return false;
}

red::String IImporter::GetResourceExtension() const
{
	RED_FATAL_ASSERT( m_resourceClass != nullptr );
	auto defaultResource = m_resourceClass->GetDefaultObject<CResource>();
	RED_FATAL_ASSERT( defaultResource != nullptr );
	return defaultResource->GetExtension();
}

const red::DynArray< CFileFormat >& IImporter::GetSourceFileFormats() const
{
	return m_formats;
}

IImporter* IImporter::FindImporter( const rtti::ClassType* resourceClass, const String& fileFormat )
{
	// Request importer classes
	DynArray< const rtti::ClassType* > importerClasses;
	GetRttiSystem().EnumClasses( ClassID<IImporter>(), importerClasses );

	while ( resourceClass )
	{
		for ( const rtti::ClassType* importerClass : importerClasses )
		{
			IImporter* importer = importerClass->GetDefaultObject< IImporter >();
			if ( importer->SupportsResource( resourceClass ) && importer->SupportsFormat( fileFormat ) )
			{
				return importer;
			}
		}

		// If we didn't find a importer, check resourceClass' base class to see if we have an importer for that.
		//
		// This fixes problems where an importer is given for class A, but actually creates some base class B or C
		// depending on the contents of the file. Without this check, we are unable to reimport such an object, since
		// no importer exists specifically for B or C.
		resourceClass = resourceClass->GetBaseClass();
	}

	// Format not supported
	return nullptr;
}

void IImporter::EnumImportFormats( const rtti::ClassType* resourceClass, DynArray< CFileFormat >& formats )
{
	// Request importer classes
	DynArray< const rtti::ClassType* > importerClasses;
	GetRttiSystem().EnumClasses( ClassID<IImporter>(), importerClasses );

	for ( const rtti::ClassType* importerClass : importerClasses )
	{
		IImporter* importer = importerClass->GetDefaultObject< IImporter >();
		if ( importer->SupportsResource( resourceClass ) )
		{
			formats.PushBack( importer->m_formats );
		}
	}
}

void IImporter::EnumImportClasses( DynArray< const rtti::ClassType* >& importClasses )
{
	// Request importer classes
	DynArray< const rtti::ClassType* > importerClasses;
	GetRttiSystem().EnumClasses( ClassID<IImporter>(), importerClasses );

	for ( const rtti::ClassType* importerClass : importerClasses )
	{
		IImporter* importer = importerClass->GetDefaultObject< IImporter >();

		if( importer->m_resourceClass != nullptr )
		{
			DynArray< const rtti::ClassType* > rawResourceClasses;
			GetRttiSystem().EnumClasses( importer->m_resourceClass, rawResourceClasses );

			// only output non-abstract resource classes - note it may be more than one
			for ( const auto* resourceClass : rawResourceClasses )
			{
				RED_FATAL_ASSERT( resourceClass->IsA<CResource>(), "Error: Class [%hs] isn't a resource", resourceClass->GetName().AsChar() );
				red::alg::PushBackUnique( importClasses, resourceClass );
			}
		}
		else
		{
			RED_LOG_ERROR( "Error: resource class for importer [%hs] is a nullptr", importerClass->GetName().AsChar() );
		}
	}
}
