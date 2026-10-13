/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialLibrary.h"
#include "materialMetaDefinitionLoader.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"
#include "../../../common/app/include/appPath.h"
#include "../../../common/redReflection/include/gatheredResource.h"

using red::DynArray;
using red::String;
using red::StringView;

RTTI_BEGIN_ENUM(ERenderStageMaterialTemplate);
{
	RTTI_ENUM_OPTION( EMST_GBuffer );
	RTTI_ENUM_OPTION( EMST_Forward );
	RTTI_ENUM_OPTION( EMST_Transparent );
	RTTI_ENUM_OPTION( EMST_Refractive );
	RTTI_ENUM_OPTION( EMST_Unlit );
	RTTI_ENUM_OPTION( EMST_Fullscreen );
	RTTI_ENUM_OPTION( EMST_UiEffect );
}
RTTI_END_ENUM();

namespace priv
{

	const AnsiChar*	MapTemplateToScaffold( ERenderStageMaterialTemplate type )
	{
		static_assert( EMST_MAX == 7, "Add new ones" );

		switch( type )
		{
		case EMST_GBuffer:			return "gbuffer_template";
		case EMST_Forward:			return "forward_template";
		case EMST_Transparent:		return "transparent_template";
		case EMST_Refractive:		return "refractive_template";
		case EMST_Unlit:			return "unlit_template";
		case EMST_Fullscreen:		return "fullscreen_template";
		case EMST_UiEffect:			return "ui_effect_template";
		}

		RED_FATAL( "Unknown template" );
		return "";
	}

	const AnsiChar*	MapTypeToExtension( EMaterialFilePathType type )
	{
		static_assert( EMFPT_MAX == 2, "Add new ones" );

		switch( type )
		{
		case EMFPT_FxMeta:	return ".fxmeta";
		case EMFPT_Fx:		return ".fx";
		}

		RED_FATAL( "Unknown template" );
		return "";
	}
}

static CMaterialLibrary* GMaterialLibrary = nullptr;

static const String sourceTemplateDirName = "sourceTemplate";

CMaterialLibrary::CMaterialLibrary()
	: m_isMultilayeredInitialized( false )
{
}

void FindFXMetaFiles( const red::AbsolutePath& absolutePath, DynArray< red::AbsolutePath >& metaFilePaths )
{
	GFileManager->FindFiles( absolutePath, "*.fxmeta", metaFilePaths, false );
	
	DynArray< red::AbsolutePath > subdirectories;
	GFileManager->FindDirectories( absolutePath, subdirectories );
	for ( const red::AbsolutePath& directory : subdirectories )
	{
		DynArray< StringView > names = red::utils::SplitPath( directory );
		// skip files in source templates directory
		if ( names.Back() == sourceTemplateDirName )
		{
			continue;
		}
		GFileManager->FindFiles( directory, "*.fxmeta", metaFilePaths, true );
	}
}

Bool CMaterialLibrary::Init( const EMaterialLibraryPathType pathType, const red::AbsolutePath& absolutePath, Bool initMultilayered /*=true*/, Bool cacheSources /*=false*/ )
{
	RED_ASSERT( m_sourceDirectories[ pathType ].Empty(), "Trying to init MaterialLibrary with path that already is initialized!" );
	m_sourceDirectories[ pathType ] = absolutePath;

	{
		// handle fxmeta include files
		DynArray< red::AbsolutePath > metaIncludeFilePaths;
		GFileManager->FindFiles( absolutePath, "*.fxmetainclude", metaIncludeFilePaths, true );
		for ( const red::AbsolutePath& metaIncludeFilePath : metaIncludeFilePaths )
		{
			const String metaIncludeFileName = red::utils::ExtractFileStem( metaIncludeFilePath );
			RED_ASSERT( !m_metaIncludePaths.KeyExist( metaIncludeFileName ), "Duplicate 'fxmetainclude' filenames" );
			m_metaIncludePaths.Insert( metaIncludeFileName, metaIncludeFilePath );
		}
	}
	
	// initialize template sources - they are in the engine sources folder
	if ( pathType == EMLPT_Engine )
	{
		auto stageClass = GetRttiSystem().FindEnum( GetTypeName<ERenderStageMaterialTemplate>() );

		for ( auto stage : stageClass->GetValues() )
		{
			const AnsiChar*	stageTemplate = priv::MapTemplateToScaffold( ERenderStageMaterialTemplate( stage ) );

			for ( Uint32 i = EMFPT_First; i < EMFPT_MAX; ++i )
			{
				const String filePath = String::Printf("%hs\\%hs%hs", sourceTemplateDirName.AsChar(), stageTemplate, priv::MapTypeToExtension( EMaterialFilePathType( i ) ) );

				m_templatePaths[stage].m_templatePath[ i ] = absolutePath.AddFilePath( StringView( filePath ) );
			}
		}
	}

	DynArray< red::AbsolutePath > metaFilePaths;
	FindFXMetaFiles( absolutePath, metaFilePaths );

	for ( const red::AbsolutePath& metaFilePath : metaFilePaths )
	{
		if ( !GenerateEntry( pathType, metaFilePath, false, cacheSources ) )
		{
			RED_DATA_ERROR( red::LoggerCategory_Rendering, false, "Failed to generate entry for fxmeta '%hs'", metaFilePath.AsChar() );
		}
	}

	if ( initMultilayered )
	{
		RED_ASSERT( !m_isMultilayeredInitialized, "Multilayered already initialized!" );
		if ( !m_isMultilayeredInitialized )
		{
			m_isMultilayeredInitialized = InitializeMatlib(m_materialLayerLibrary);			
		}
	}
	return true;
}

res::GatheredResourceReference g_matlibResource( RED_CONST_RESOURCEPATH( "base\\materials\\matlib\\matlib.matlib" ), GRF_Default ) ;

Bool CMaterialLibrary::InitializeMatlib(THandle< CMaterialLayerLibrary >& matlib)
{
	RED_SCOPE_LOCK( m_mutex );

	// TODO: once packages are sorted out (per game), we need to get rid of this hardcoded path and do it smart way
	auto token = GResourceLoader->IssueLoadingRequest( g_matlibResource.GetPath() );
	token->WaitUntilLoaded();
	if (token->IsLoaded())
	{
		matlib = Cast< CMaterialLayerLibrary >(token->GetResource());
		RED_ASSERT(matlib, "Material layer library couldn't get loaded. Get r6 from p4!");
		return true;
	}
	return false;
}

Bool CMaterialLibrary::GetShader( const String& sourcePath, String& code, Bool cache /*=false*/ ) const
{
	for ( Uint32 i = 0; i < EMLPT_MAX; ++i )
	{
		RED_ASSERT( !m_sourceDirectories[ i ].Empty(), "Material library's source directories were not initialized properly!" );
		red::AbsolutePath filePath = m_sourceDirectories[ i ].AddFilePath( sourcePath );
		
		if ( GetCachedShaderCode( filePath, code ) )
		{
			return true;
		}
		if ( GFileManager->FileExist( filePath ) )
		{
			Bool loadResult = red::LoadFileToString( filePath, code );
			if ( cache )
			{
				CacheShaderCode( filePath, code );
			}
			return loadResult;
		}
	}
	return false;
}

Bool CMaterialLibrary::GetMeta( const CName name, String& meta ) const
{
	red::AbsolutePath metaPath;
	if ( !m_metaPaths.Find( name, metaPath ) )
	{
		return false;
	}
	return red::LoadFileToString( metaPath, meta );
}

Bool CMaterialLibrary::GetMetaInclude( const String& name, String& metaIncludeContent ) const
{
	red::AbsolutePath metaIncludePath;
	if ( !m_metaIncludePaths.Find( name, metaIncludePath ) )
	{
		return false;
	}
	return red::LoadFileToString( metaIncludePath, metaIncludeContent );
}

Bool CMaterialLibrary::GetShaderSourceAbsolutePath( const CName name, const CName stagePassName, EMaterialShaderTarget target, red::AbsolutePath& path ) const
{
	if ( !m_shaderSourcePaths.KeyExist( name ) )
	{
		return false;
	}
	const ShaderSourceEntryCollection& entries = m_shaderSourcePaths[ name ];
	
	if ( !entries.KeyExist( stagePassName ) )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "ShaderSourceEntry: fxmeta for '%hs' does not contain stage pass '%hs'! Check your .fx and .fxmeta files", name.AsChar(), stagePassName.AsChar() );
		return false;
	}
	const ShaderSourceEntry& entry = entries[ stagePassName ];

	if ( !entry.m_isValid )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "ShaderSourceEntry '%hs' ('%hs') is invalid! Check your .fx and .fxmeta files", name.AsChar(), stagePassName.AsChar() );
		return false;
	}

	path = entry.m_sourcePath[ target ];
	if ( path.Empty() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Shader source file for '%hs' ('%hs') has empty path!", name.AsChar(), stagePassName.AsChar() );
	}
	return !path.Empty();
}

Bool CMaterialLibrary::GetMetaFileAbsolutePath( const CName name, red::AbsolutePath& path ) const
{
	return m_metaPaths.Find( name, path );
}

namespace red
{
	Bool GetSanitizedDateString( String& timeString )
	{
		CDateTime currentTime;
		red::Clock::GetInstance().GetLocalTime( currentTime );

		if ( !ToString( timeString, currentTime ) )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Error converting date to string." );
			return false;
		}

		timeString.ReplaceAll( "\\", "." );
		timeString.ReplaceAll( "/", "." );
		timeString.ReplaceAll( " ", "_" );
		timeString.ReplaceAll( ":", "." );

		return true;
	}

	Bool PreservePotentiallyExistingFile( const CName name, const StringView& extension, const red::AbsolutePath& sourceDirectory, red::AbsolutePath& outFilePath )
	{
		String tempName = String::Printf( "%hs.%hs", name.AsChar(), extension.Data() );
		outFilePath = sourceDirectory.AddFilePath( StringView( tempName ) );
		if ( GFileManager->FileExist( outFilePath ) )
		{
			// fx file exists, rename it to "{filename}_{date}.fx"		
			String timeString;
			if ( !GetSanitizedDateString( timeString ) )
			{
				return false;
			}

			String newFileName = String::Printf( "%hs_%hs", name.AsChar(), timeString.AsChar() );
			String newFileNameWithExtension = newFileName + "." + extension.ToString();
			red::AbsolutePath newFilePath = sourceDirectory.AddFilePath( newFileNameWithExtension );
			while ( GFileManager->FileExist( newFilePath ) )
			{
				newFileName += "_1";
				newFileNameWithExtension = newFileName + "." + extension.ToString();
				newFilePath = sourceDirectory.AddFilePath( StringView( newFileNameWithExtension ) );
			}
		
			if ( !GFileManager->CopyFileW( outFilePath, newFilePath, true ) )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Error preserving old file (copying from '%hs' to '%hs').", outFilePath.AsChar(), newFilePath.AsChar() );
				return false;
			}
		}
		return true;
	}

	Bool Cleanup( red::AbsolutePath createdFiles[ EMFPT_MAX ] )
	{
		Bool result = true;
		for ( Uint32 i = EMFPT_First; i < EMFPT_MAX; ++i )
		{
			if ( createdFiles[ i ].Empty() )
			{
				continue;
			}
			red::AbsolutePath& path = createdFiles[ i ];
			if ( !GFileManager->DeleteFileW( path ) )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Could not delete file '%hs'", path.AsChar() );
				result = false;
			}
		}
		return result;
	}
}

Bool CMaterialLibrary::CreateShaderMetadataAndCodeFiles( const EMaterialLibraryPathType pathType, const CName name, const ERenderStageMaterialTemplate stage, red::AbsolutePath createdFiles[ EMFPT_MAX ] )
{
	RED_ASSERT( !m_sourceDirectories[ pathType ].Empty(), "Material library's source directories were not initialized properly!" );
	
	for ( Uint32 i = EMFPT_First; i < EMFPT_MAX; ++i )
	{
		if ( !CreateMaterialFile( name, pathType, stage, (EMaterialFilePathType)i, createdFiles ) )
		{
			RED_VERIFY( red::Cleanup( createdFiles ) );
			return false;
		}
	}

	if ( !GenerateEntry( pathType, createdFiles[ EMFPT_FxMeta ] ) )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Could not generate material library entry for '%hs'.", createdFiles[ EMFPT_FxMeta ].AsChar() );
		RED_VERIFY( red::Cleanup( createdFiles ) );
		return false;
	}
	return true;
}

Bool CMaterialLibrary::GeneratePath( const String& filePath, const EMaterialLibraryPathType pathType, red::AbsolutePath& generatedPath ) const
{
	const red::AbsolutePath& sourceDirectory = m_sourceDirectories[ pathType ];
	generatedPath = sourceDirectory.AddFilePath( filePath );
	if ( !GFileManager->FileExist( generatedPath ) )
	{
		generatedPath = m_sourceDirectories[ EMLPT_Engine ].AddFilePath( filePath );
		if ( !GFileManager->FileExist( generatedPath ) )
		{
			generatedPath = m_sourceDirectories[ EMLPT_Game ].AddFilePath( filePath );
			if ( !GFileManager->FileExist( generatedPath ) )
			{
				return false;
			}
		}
	}
	return true;
}

Bool CMaterialLibrary::GenerateEntry( const EMaterialLibraryPathType pathType, const red::AbsolutePath& metaFilePath, Bool forceUpdate /*=false*/, Bool cacheSources /*=false*/ )
{
	// R6_ON_CONSOLES : FileExist isn't supported on consoles. Not sure if this is the correct thing, or maybe
	// MaterialLibrary shouldn't be generating entries at game runtime, but just need consoles running :)
#ifndef RED_PLATFORM_CONSOLE
	if( !GFileManager->FileExist( metaFilePath ) )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Materials: Meta file for shader [%hs] does not exits!!!", metaFilePath.AsChar() );
		return false;
	}
#endif

	const CName metaFileName = RED_NAME( red::utils::GetFileStem( metaFilePath ) );
	if ( m_metaPaths.KeyExist( metaFileName ) )
	{
		if ( forceUpdate )
		{
			// overwrite the value in the map
			m_metaPaths[ metaFileName ] = metaFilePath;
		}
		else if ( m_metaPaths[ metaFileName ] != metaFilePath )
		{
			return false;
		}
	}
	else
	{
		m_metaPaths.Insert( metaFileName, metaFilePath );
	}

	// prepare absolutePaths for shader source files
	CMaterialMetaDefinitionLoader::Logger log( metaFileName );
	CMaterialMetaDefinitionLoader loader( false, this, log );
	MaterialMetaDefinition metaDefinition( metaFileName );
	if ( loader.Load( metaFileName, metaDefinition ) != CMaterialMetaDefinitionLoader::LoaderStatus::Ok )
	{
		return false;
	}

	ShaderSourceEntryCollection sourceEntries;

	for ( const auto& technique : metaDefinition.m_techniques )
	{
		for ( const auto& pass : technique.m_passes )
		{
			ShaderSourceEntry shaderSourceEntry;
			for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
			{
				const String& shaderSourcePath = pass.m_shaderSourcePath[ target ];

				red::AbsolutePath path;
				shaderSourceEntry.m_isValid = GeneratePath( shaderSourcePath, pathType, path );
				if ( !shaderSourceEntry.m_isValid )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Materials: Shader source file '%hs' does not exist!", path.AsChar() );
				}
				shaderSourceEntry.m_sourcePath[ target ] = path;

				if ( cacheSources )
				{
					if ( shaderSourceEntry.m_isValid )
					{
						String code;
						RED_VERIFY( red::LoadFileToString( path, code ), "Error loading source file '%hs' for material '%hs'.", path.AsChar(), metaFilePath.AsChar() );
						m_cachedSources.Insert( red::CalculateAnsiHash32( path.AsChar() ), code );
					}
					else
					{
						RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "MaterialLibrary: Error when parsing '%hs': engine is unable to cache shader source file '%hs' - file does not exist!", metaFilePath.AsChar(), path.AsChar() );
					}
				}
			}
			sourceEntries.Set( pass.m_stagePassNameRegular, shaderSourceEntry );
		}
	}
	m_shaderSourcePaths.Set( metaFileName, sourceEntries );
	
	return true;
};

Bool CMaterialLibrary::UpdateTechniquesForMaterial( const CName name, const red::DynArray< MaterialTechnique >& techniques )
{
	if ( !m_shaderSourcePaths.KeyExist( name ) )
	{
		return false;
	}
	ShaderSourceEntryCollection& entries = m_shaderSourcePaths[ name ];

	for ( const auto& technique : techniques )
	{
		for ( const auto& pass : technique.m_passes )
		{
			if ( !entries.KeyExist( pass.m_stagePassNameRegular ) )
			{
				// create new empty entry
				ShaderSourceEntry entry;
				entries.Insert( pass.m_stagePassNameRegular, entry );
			}

			ShaderSourceEntry& entry = entries[ pass.m_stagePassNameRegular ];
			for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
			{
				const String& shaderSourcePath = pass.m_shaderSourcePath[ target ];

				red::AbsolutePath path;
				// TODO: revisit when .fx and .fxmeta files are moved outside engine / r6 folder to one directory
				entry.m_isValid = GeneratePath( shaderSourcePath, EMLPT_Engine, path );
				if ( !entry.m_isValid )
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Materials: Shader source file '%hs' does not exist!", path.AsChar() );
					return false;
				}
				entry.m_sourcePath[ target ] = path;
			}
		}
	}
	return true;
}

Bool CMaterialLibrary::CreateMaterialFile( const CName name, const EMaterialLibraryPathType libPathType, const ERenderStageMaterialTemplate stage, const EMaterialFilePathType pathType, red::AbsolutePath createdFiles[ EMFPT_MAX ] ) const
{
	const red::AbsolutePath& sourceDirectory = m_sourceDirectories[ libPathType ];

	const red::AbsolutePath& path = m_templatePaths[ stage ].m_templatePath[ pathType ];
	red::StringView extension = red::utils::GetExtension( path );

	red::AbsolutePath outFilePath;
	if ( !red::PreservePotentiallyExistingFile( name, extension, sourceDirectory, outFilePath ) )
	{
		return false;
	}

	if ( GFileManager->FileExist( outFilePath ) )
	{
		RED_VERIFY( GFileManager->SetFileReadOnly( outFilePath, false ) );
	}

	if ( !GFileManager->CopyFileW( path, outFilePath, true ) )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Error creating fx file: '%hs'.", outFilePath.AsChar() );
		RED_VERIFY( red::Cleanup( createdFiles ) );
		return false;
	}
	createdFiles[ pathType ] = outFilePath;
	return true;
}

Bool CMaterialLibrary::GetCachedShaderCode( const red::AbsolutePath& filePath, String& code ) const
{
	Uint32 filePathHash = red::CalculateAnsiHash32( filePath.AsChar() );
	return m_cachedSources.Find( filePathHash, code );
}

void CMaterialLibrary::CacheShaderCode( const red::AbsolutePath& filePath, const String& code ) const
{
	Uint32 filePathHash = red::CalculateAnsiHash32( filePath.AsChar() );
	if ( m_cachedSources.KeyExist( filePathHash ) )
	{
		RED_ASSERT( m_cachedSources[ filePathHash ] == code );
		return;
	}
	m_cachedSources.Insert( filePathHash, code );
}

CMaterialLibrary* GetMaterialLibrary()
{
	RED_FATAL_ASSERT( GMaterialLibrary != nullptr, "Material library is not initialized" );

	return GMaterialLibrary;
}

bool InitializeMaterialLibrary()
{
	RED_FATAL_ASSERT( GMaterialLibrary == nullptr, "Material library is already initialized" );

	// Create Material Library
	GMaterialLibrary = RED_NEW( CMaterialLibrary )( );
	const red::AbsolutePath engineDepotRoot = app::paths::MakeEngineDepotPath( GFileManager->GetEngineRoot() );
	const red::AbsolutePath materialLibEnginePath = engineDepotRoot.AddDirPath( "materials\\sources\\" );
	if ( !GMaterialLibrary->Init( EMLPT_Engine, materialLibEnginePath, false ) )
	{
		return false;
	}

	const red::AbsolutePath gameDepotRoot = app::paths::MakeGameDepotPath( GFileManager->GetGameRoot() );
	const red::AbsolutePath materialLibGamePath = gameDepotRoot.AddDirPath( "base\\materials\\sources\\" );
	if ( !GMaterialLibrary->Init( EMLPT_Game, materialLibGamePath, false ) )
	{
		return false;
	}

	return true;
}

void ShutdownMaterialLibrary()
{
	if ( GMaterialLibrary != nullptr )
	{
		RED_DELETE( GMaterialLibrary );
		GMaterialLibrary = nullptr;
	}
}
