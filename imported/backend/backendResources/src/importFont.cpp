/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "importFont.h"
#include "../../../common/renderData/include/font.h"
#include "../../../common/redFileSystem/include/fileSys.h"

RTTI_BEGIN_TYPE( FontImporter );
	RTTI_PARENT_TYPE( IImporter );
RTTI_END_TYPE();

FontImporter::FontImporter()
{
	// Supported class
	m_resourceClass = ClassID< rend::Font >();

	// Supported formats
	m_formats.PushBack( CFileFormat( "ttf", "TrueType font" ) );
	m_formats.PushBack( CFileFormat( "otf", "OpenType font" ) );
	m_formats.PushBack( CFileFormat( "cff", "Compact Font Format" ) );
}

ImportResult FontImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	// Get the target object
	THandle< rend::Font > font = CreateHandle< rend::Font >();
	if( !font )
	{
		return ImportResult::Error( "Unable to import font into something that is not a font resource" );
	}

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();

	red::DynArray< Uint8 > buffer;
	if ( !LoadFileToBuffer( sourceFilePath, buffer ) )
	{
		return ImportResult::Error( "Couldn't load font from %hs", sourceFilePath.AsChar() );
	}

	if ( !font->SetFontBuffer( std::move( buffer ) ) )
	{
		return ImportResult::Error( "Unable to initialize font. Check logs for more information" );
	}

	return ImportResult::Success( font );
}
