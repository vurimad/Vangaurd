/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "importCurveSet.h"
#include "../../../backend/backendEngine/include/resourceImportMetadata.h"
#include "../../../backend/backendEngine/include/resourceMetadata.h"
#include "../../../common/engine/include/curveSetResource.h"
#include "../../../common/redFileSystem/include/fileSys.h"

RTTI_BEGIN_TYPE( CurveSetImporter )
	RTTI_PARENT_TYPE( IImporter )
RTTI_END_TYPE()


CurveSetImporter::CurveSetImporter()
{
	// Supported class
	m_resourceClass = ClassID< CurveSet >();

	// Supported formats
	m_formats.PushBack( CFileFormat( "csv", "Comma Separated Value table" ) );
}

ImportResult CurveSetImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	THandle< CurveSet > curveSetResource = Cast< CurveSet >( inputResource );
	if ( !curveSetResource )
	{
		return ImportResult::Error( "Unable to import curve set from CSV." );
	}

	String dataString { red::PoolBackend() };
	red::LoadFileToString( options->GetSourcePath(), dataString );

	C2dArray dataArray;
	dataArray.ParseData( std::move( dataString ), "," );

	if ( dataArray.GetNumberOfColumns() % 2 != 0 )
	{
		return ImportResult::Error( "There must be even amount of columns in source CSV file!" );
	}

	Uint32 columns = dataArray.GetNumberOfColumns();
	red::DynArray<String> headers = dataArray.GetHeaders();

	red::DynArray<CurveSetEntry> curves;

	for ( Uint32 i = 0; i < columns/2; ++i )
	{
		const Uint32 columnIndex = i * 2;

		CurveSetEntry entry;

		String curveName = String( headers[ columnIndex ] + "//" + headers[ columnIndex + 1 ], red::PoolBackend());
		entry.m_name = RED_NAME( curveName );

		TSingleChannelCurveBuilder<Float> builder( entry.m_curve );
		builder.Resize( dataArray.GetNumberOfRows() );

 		for ( Uint32 row = 0; row < dataArray.GetNumberOfRows(); ++row )
		{
			Float x;
			if ( !FromString( dataArray.GetValueRef( columnIndex, row ), x ) )
			{
				return ImportResult::Error( "Data contains non-float values" );
			}
			
			Float y;
			if ( !FromString( dataArray.GetValueRef( columnIndex + 1, row ), y ) )
			{
				return ImportResult::Error( "Data contains non-float values" );
			}

			builder.SetKeyData( row, x, y );
		}

		curves.PushBack( entry );
	}

	curveSetResource->SetCurves( curves );

	THandle<res::ResourceImportMetadata> importMetadata( RED_NEW( res::ResourceImportMetadata ) );
	importMetadata->SetAssetPath( options->GetSourcePath() );
	res::ResourceMetadata::SetImportData( curveSetResource, importMetadata );

	return ImportResult::Success( curveSetResource );
}