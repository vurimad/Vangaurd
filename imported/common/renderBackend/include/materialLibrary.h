/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderBackendApi.h"

#include "../../../common/renderData/include/materialStructures.h"
#include "../../../common/resourceMaterial/include/materialMetaDefinition.h"
#include "../../../common/resource/include/xmlDocument.h"

class CMaterialLayerLibrary;

enum EMaterialLibraryPathType
{
	EMLPT_Engine,
	EMLPT_Game,

	EMLPT_MAX,
};

enum ERenderStageMaterialTemplate
{
	EMST_GBuffer = 0,
	EMST_Forward,
	EMST_Transparent,
	EMST_Refractive,
	EMST_Unlit,
	EMST_Fullscreen,
	EMST_UiEffect,

	EMST_MAX
};

RTTI_DECLARE_ENUM(ERenderStageMaterialTemplate);

enum EMaterialFilePathType
{
	EMFPT_First = 0,
	EMFPT_Fx = EMFPT_First,
	EMFPT_FxMeta,

	EMFPT_MAX
};

class EMaterialLibraryPathTypeHashFunc
{
public:
	static RED_FORCE_INLINE Uint32 GetHash( const EMaterialLibraryPathType key )
	{
		return ( Uint32 ) key;
	}
};

class RENDERBACKEND_API CMaterialLibrary
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	CMaterialLibrary();

	Bool Init( const EMaterialLibraryPathType pathType, const red::AbsolutePath& paths, Bool initMultilayered = true, Bool cacheSources = false );
	Bool InitializeMatlib( THandle< CMaterialLayerLibrary >& matlib );
	Bool GetShader( const red::String& name, red::String& code, Bool cache = false ) const;
	Bool GetMeta( const CName name, red::String& meta ) const;
	Bool GetMetaInclude( const String& name, String& metaIncludeContent ) const;
	Bool GetShaderSourceAbsolutePath( const CName name, const CName stagePassName, EMaterialShaderTarget target, red::AbsolutePath& path ) const;
	Bool GetMetaFileAbsolutePath( const CName name, red::AbsolutePath& path ) const;

	Bool CreateShaderMetadataAndCodeFiles( const EMaterialLibraryPathType pathType, const CName name, const ERenderStageMaterialTemplate stage, red::AbsolutePath createdFiles[ EMFPT_MAX ] );

	Bool UpdateTechniquesForMaterial( const CName name, const red::DynArray< MaterialTechnique >& techniques );

	RED_INLINE THandle< CMaterialLayerLibrary > GetMatlib() const { return m_materialLayerLibrary; }

private:
	Bool GeneratePath( const String& filePath, const EMaterialLibraryPathType pathType, red::AbsolutePath& generatedPath ) const;
	Bool GenerateEntry( const EMaterialLibraryPathType pathType, const red::AbsolutePath& path, Bool forceUpdate = false, Bool cacheSources = false );
	Bool CreateMaterialFile( const CName name, const EMaterialLibraryPathType libPathType, const ERenderStageMaterialTemplate stage, const EMaterialFilePathType pathType, red::AbsolutePath createdFiles[ EMFPT_MAX ] ) const;
	
	Bool GetCachedShaderCode( const red::AbsolutePath& filePath, String& code ) const;
	void CacheShaderCode( const red::AbsolutePath& filePath, const String& code ) const; // 'const' is just to allow to call it from another const function

private:
	red::AbsolutePath m_sourceDirectories[ EMLPT_MAX ];

	typedef red::HashMap< CName, red::AbsolutePath > EntryMap;
	EntryMap m_metaPaths;

	struct ShaderSourceEntry
	{
		ShaderSourceEntry() : m_isValid( true ) {}

		red::AbsolutePath	m_sourcePath[ MSH_MAX ];
		Bool			m_isValid;
	};
	typedef red::HashMap< CName, ShaderSourceEntry > ShaderSourceEntryCollection;
	typedef red::HashMap< CName, ShaderSourceEntryCollection > ShaderSourceEntryCollectionMap;
	ShaderSourceEntryCollectionMap m_shaderSourcePaths;

	red::HashMap< red::String, red::AbsolutePath > m_metaIncludePaths;

	THandle< CMaterialLayerLibrary > m_materialLayerLibrary;
	red::Mutex m_mutex;
	
	Bool m_isMultilayeredInitialized;

	struct SRenderStageMaterialTemplate
	{
		red::AbsolutePath	m_templatePath[ EMFPT_MAX ];
	} m_templatePaths[ EMST_MAX ];

	mutable red::HashMap< Uint32, String >	m_cachedSources;
};

RENDERBACKEND_API CMaterialLibrary* GetMaterialLibrary();

RENDERBACKEND_API Bool InitializeMaterialLibrary();
RENDERBACKEND_API void ShutdownMaterialLibrary();
