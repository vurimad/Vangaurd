/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../backend/backendEngine/include/importer.h"
#include "../../../backend/backendEngine/include/factory.h"
#include "../../../common/renderData/include/imageProcessing.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "bitmapTextureCompilationSource.h"

#pragma warning(disable:4996) // deprecated functions: IE_ReadFile uses fopen, sscanf, strcpy

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// Some conversion setups
#define USE_SQRT_QUANTIZE	1
#define M_PI_2			   1.57079632679489661923f   // pi/2

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// Just import both header and source. Don't want to mess with libs
#include "../../../external/ies/src/IESNA.H"
#include "../../../external/ies/src/IESNA.C"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/renderData/include/iesResource.h"
#include "../../../common/resource/include/resourceSaver.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

/// Bitmap texture importer based on DirectXTex functionality
class CIESImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CIESImporter );

public:
	CIESImporter();

public:
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) override;
};

RTTI_BEGIN_TYPE( CIESImporter )
{
	RTTI_PARENT_TYPE( IImporter )
}
RTTI_END_TYPE()


/// Factory shit - why its event nedded ??
class CIESProfileFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CIESProfileFactory );

public:

	CIESProfileFactory()
	{
		m_resourceClass = ClassID< CIESDataResource >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CIESProfileFactory );
{
	RTTI_PARENT_TYPE( IFactory );
}
RTTI_END_TYPE();

//***********************************************************************************************************************//
//***********************************************************************************************************************//

static Float GetIES( const IE_DATA* ieData, Float verAngle, Float horAngle )
{
	RED_FATAL_ASSERT( nullptr != ieData );

	Int32 horIdx = -1;
	Int32 verIdx = -1;

	Float lerpScaleV = 0.0f, lerpScaleH = 0.0f;

	for( Int32 j = 0; j < ieData->photo.num_vert_angles - 2; ++j )
	{
		const Float angle0 = ieData->photo.vert_angles[j];
		const Float angle1 = ieData->photo.vert_angles[j + 1];

		if( angle0 <= verAngle && angle1 >= verAngle )
		{
			verIdx = j;

			const Float angleRange	= angle1 - angle0;
			const Float angleStep	= verAngle - angle0;
			lerpScaleV	= angleStep / angleRange;

			break;
		}

	}

	if( ieData->photo.num_horz_angles > 1 )
	{
		for( Int32 j = 0; j < ieData->photo.num_horz_angles - 1; ++j )
		{
			const Float angle0 = ieData->photo.horz_angles[j];
			const Float angle1 = ieData->photo.horz_angles[j + 1];

			if( angle0 <= horAngle && angle1 >= horAngle )
			{
				horIdx = j;

				const Float angleRange	= angle1 - angle0;
				const Float angleStep	= horAngle - angle0;
				lerpScaleH	= angleStep / angleRange;

				break;
			}

		}
	}
	else
	{
		horIdx = 0;
	}

	// No data found
	if( verIdx == -1 || horIdx == -1 )
	{
		return 0.0f;
	}

	const Float candela00	= ieData->photo.pcandela[horIdx][verIdx + 1];
	const Float candela10	= ieData->photo.pcandela[horIdx][verIdx + 2];

	horIdx = (horIdx + 1) % ieData->photo.num_horz_angles;

	const Float candela01	= ieData->photo.pcandela[horIdx][verIdx + 1];
	const Float candela11	= ieData->photo.pcandela[horIdx][verIdx + 2];

	const Float one_lerpScaleV	=	1.0f - lerpScaleV;
	const Float one_lerpScaleH	=	1.0f - lerpScaleH;

	// simple linear interpolation
	return one_lerpScaleH *	(one_lerpScaleV * candela00 + lerpScaleV * candela10) +
		lerpScaleH     * (one_lerpScaleV * candela01 + lerpScaleV * candela11);

}

//***********************************************************************************************************************//
//***********************************************************************************************************************//

CIESImporter::CIESImporter()
{
	// Supported class
	m_resourceClass = ClassID< CIESDataResource >();

	// Supported formats
	m_formats.PushBack( CFileFormat( "ies", "IES light profile" ) );
}

ImportResult CIESImporter::DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options )
{
	THandle < CIESDataResource > resource = Cast< CIESDataResource >( inputResource );
	if( !resource )
	{
		return ImportResult::Error( "Core: Unable to import IES into something that is not a IES" );
	}

	const red::AbsolutePath sourceFilePath = options->GetSourcePath();
	if( !red::utils::HasExtension( sourceFilePath ) )
	{
		return ImportResult::Error( "Error parsing resource filename." );
	}

	IE_DATA ieData;

	// Read IESNA photometric data file
	if( !IE_ReadFile( const_cast<char*>( sourceFilePath.AsChar() ), &ieData ) )
	{
		return ImportResult::Error( "Error reading IESNA file" );
	}

	const Uint32 height	= 1;
	const Uint32 width	= CIESDataResource::NUM_SAMPLES;

	Float*	data = RED_NEW_ARRAY( Float, width * height, red::PoolBackend );
	

	// Gather all data for LUT
	for( Uint32 x = 0; x < width; ++x )
	{
		// sqrt improoves LUT distribution to more uniform dot() <-> angle
#if USE_SQRT_QUANTIZE
		const Float linearIndex = std::sqrt( (x + 0.5f) / CIESDataResource::NUM_SAMPLES );
#else
		const Float linearIndex = (i + 0.5f) / CIESDataResource::NUM_SAMPLES;
#endif

		const Float radAngle = std::acos( linearIndex ) / M_PI_2;

		const Float angle = radAngle * 90.0f;

		for( Uint32 y = 0; y < height; ++y )
		{
			Float& o	= data[x + y * width];

			const Float horAngle = ((y + 0.5f) * 180.0f) / height;

			o = GetIES( &ieData, angle, horAngle );
		}
	}

	resource->Import( data, width );

	// {
	// 	res::ResourceSavingOptions savingOptions;
	// 	savingOptions.m_targetPath		= options->GetResourcePath();
	// 
	// 	resource->Internal_SetPath( savingOptions.m_targetPath );
	// 
	// 	// Save for editor
	// 	res::SaveResource( resource, savingOptions );
	// }

	RED_DELETE_ARRAY( data, width * height, red::PoolBackend );

	return ImportResult::Success( resource );
}

//***********************************************************************************************************************//
//***********************************************************************************************************************//
