/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "materialController.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"

#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../../common/particles/include/particleSystem.h"
#include "../../../common/redFileSystem/include/fileSys.h"

class ReimportMaterialsCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ReimportMaterialsCommandlet );

public:
	ReimportMaterialsCommandlet();

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual Bool Execute( const red::CommandLine& options ) override;

private:
	Bool ParseParams( const red::CommandLine& options );

	commandlet::EngineInstance m_engineInstance;

	res::ResourceEditorDepot* m_editorDepot;
};


RTTI_BEGIN_TYPE( ReimportMaterialsCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()


ReimportMaterialsCommandlet::ReimportMaterialsCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "reimport-materials" ) )
	, m_editorDepot( commandlet::GetEditorDepot() )
{}

const char* ReimportMaterialsCommandlet::GetOneLiner() const
{
	return "Reimport all the materials in the depot";
}

void ReimportMaterialsCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  reimport-materials" );
}

Bool ReimportMaterialsCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && InitializeMaterialLibrary();
}

void ReimportMaterialsCommandlet::TeardownEnvironment()
{
	ShutdownMaterialLibrary();
	m_engineInstance.Shutdown();
}

Bool ReimportMaterialsCommandlet::ParseParams( const red::CommandLine& options )
{
	return true;
}

Bool ReimportMaterialsCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseParams( options ) )
	{
		return false;
	}

	const red::DynArray< String > patterns{ "*.mt", "*.remt" };
	const auto allResources = commandlet::ScanResourceDepotDirectories( *m_editorDepot, "", patterns );

	Bool hasErrors = false;

	for ( Uint32 i = 0; i < allResources.Size(); ++i )
	{
		const res::ResourcePath& path = allResources[ i ];
		RED_LOG_INFO( "Processing: %hs (%i/%i)", path.ToDebugString(), i + 1, allResources.Size() );

		const THandle< CResource > resource = GResourceLoader->IssueLoadingRequest( path )->WaitUntilLoaded();
		if ( !resource )
		{
			RED_LOG_ERROR( "Failed to load '%hs'", path.ToDebugString() );
			hasErrors = true;
			continue;
		}

		const THandle< CMaterialTemplate > material = Cast< CMaterialTemplate >( resource );
		if ( !material )
		{
			RED_LOG_ERROR( "Resource '%hs' is not a material", path.ToDebugString() );
			hasErrors = true;
			continue;
		}

		if ( !tools::MaterialController::UpdateMaterialMeta( material ) )
		{
			RED_LOG_ERROR( "Failed to update material meta: '%hs'", path.ToDebugString() );
			hasErrors = true;
			continue;
		}

		material->UnmarkModified();

		res::ResourceSavingOptions options;
		options.m_depot = m_editorDepot;
		if ( !res::SaveResource( material, options ) )
		{
			hasErrors = true;
			continue;
		}
	}

	return !hasErrors;
}

//////////////////////////////////////////////////////////////////////////

class Material_RemapFrostedGlassCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( Material_RemapFrostedGlassCommandlet );

public:
	Material_RemapFrostedGlassCommandlet();

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual Bool Execute(const red::CommandLine& options) override;

private:
	Bool ParseParams( const red::CommandLine& options );
	Bool ProcessMaterialInstance( const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial );

	Bool HandleMaterialInstance( const THandle< CMaterialInstance >& resource, const THandle< IMaterial >& baseMaterial );
	Bool HandleResource( const res::ResourcePath& resourcePath, const THandle< IMaterial >& baseMaterial );

	void RemapScalar( const red::HashMap< CName, Float >& scalarParams, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial );
	void RemapColor( const red::HashMap< CName, Color >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial );
	void RemapVector( const red::HashMap< CName, Vector4 >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial );
	void RemapTexture( const red::HashMap< CName, TResRef< ITexture > >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial );
	
	resdb::Database m_database;
	commandlet::EngineInstance m_engineInstance;

	res::ResourcePath m_glassMaterialPath;
	res::ResourcePath m_frostedGlassMaterialPath;

	THandle< CMaterialTemplate > m_glassMaterial;
	THandle< CMaterialTemplate > m_frostedGlassMaterial;

	Bool m_hasErrors = false;

	red::HashSet< res::ResourcePath > m_processedResources;

	res::ResourceEditorDepot* m_editorDepot;
};


RTTI_BEGIN_TYPE( Material_RemapFrostedGlassCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()


Material_RemapFrostedGlassCommandlet::Material_RemapFrostedGlassCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "material_remapfrostedglass" ) )
	, m_editorDepot(commandlet::GetEditorDepot())
{}

const char* Material_RemapFrostedGlassCommandlet::GetOneLiner() const
{
	return "Reimport all the materials in the depot";
}

void Material_RemapFrostedGlassCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG("  checklocalmaterialinstances");
}

Bool Material_RemapFrostedGlassCommandlet::InitializeEnvironment()
{
	commandlet::BackendDepotHolder depotHolder;

	red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath(resdb::Database::GetDefaultFilename());
	m_database.Initialise(*depotHolder.GetDepot(), resdbCachePath);

	m_frostedGlassMaterialPath = res::ResourcePath::Build( "base\\materials\\frosted_glass.mt" );
	res::IssueLoadingRequestParameter loadingRequestParameter;
	loadingRequestParameter.path = m_frostedGlassMaterialPath;
	loadingRequestParameter.skipBuffers = true;
	loadingRequestParameter.skipPostLoad = true;
	loadingRequestParameter.postLoadFlags.skipUploadRenderData = true;

	m_frostedGlassMaterial = Cast< CMaterialTemplate >( GResourceLoader->IssueLoadingRequest( loadingRequestParameter )->WaitUntilLoaded() );
	if ( !m_frostedGlassMaterial )
	{
		RED_LOG_ERROR( "FrostedGlass Remapper: Failed to load '%hs'", m_frostedGlassMaterialPath.ToDebugString() );
		return false;
	}

	m_glassMaterialPath = res::ResourcePath::Build("base\\materials\\glass.mt");
	loadingRequestParameter.path = m_glassMaterialPath;

	m_glassMaterial = Cast< CMaterialTemplate >( GResourceLoader->IssueLoadingRequest( loadingRequestParameter )->WaitUntilLoaded() );
	if ( !m_glassMaterial )
	{
		RED_LOG_ERROR( "FrostedGlass Remapper: Failed to load '%hs'", m_glassMaterialPath.ToDebugString() );
		return false;
	}

	return m_engineInstance.Initialise() && InitializeMaterialLibrary();
}

void Material_RemapFrostedGlassCommandlet::TeardownEnvironment()
{
	ShutdownMaterialLibrary();
	m_engineInstance.Shutdown();
}

Bool Material_RemapFrostedGlassCommandlet::ParseParams( const red::CommandLine& options )
{
	return true;
}

Float Material_RemapTo01( Float val, Float rangeMin, Float rangeMax )
{
	return math::Clamp( ( ( val - rangeMin ) / ( rangeMax - rangeMin ) ), 0.f, 1.f );
}

template < typename T >
Bool GetParam( const THandle< IMaterial >& material, const THandle< IMaterial >& baseTemplate, const CName paramName, T& outValue )
{
	IMaterial* pMaterial = material.Get();
	while ( pMaterial && pMaterial->IsA< CMaterialInstance >() )
	{
		if ( pMaterial->ReadParameter_NoBase( paramName, outValue ) )
		{
			return true;
		}
		pMaterial = pMaterial->GetBaseMaterial();
	}

	// did not find anything in overriden instances, take it from the base
	return baseTemplate->ReadParameter( paramName, outValue );
}

void Material_RemapFrostedGlassCommandlet::RemapScalar( const red::HashMap< CName, Float >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial )
{
	Float scalarValue;
	RED_VERIFY( GetParam( baseMaterial, m_glassMaterial, paramName, scalarValue ) );

	if ( paramName == RED_NAME_CONSTEXPR_NOREG( "GlassRoughnessBias" ) ) // GlassRoughnessBias = clamp( RoughnessBase, -1.f, 1.f );
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR_NOREG( "RoughnessBase" ) ) )
		{
			Float unmapped = params[ RED_NAME_CONSTEXPR( "RoughnessBase" ) ];
			const Float f =  Clamp( unmapped, -1.f, 1.f );
			if ( f != scalarValue )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' RoughnessBase[%1.3f] -> GlassRoughnessBias[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, f, false );
			}
		}
	}
	else if ( paramName == RED_NAME_CONSTEXPR_NOREG( "MaskRoughnessBias" ) ) // MaskRoughnessBias = lerp(-1.f, 0.f, RoughnessAttenuation);
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR_NOREG( "RoughnessAttenuation" ) ) )
		{
			const Float unmapped = params[ RED_NAME_CONSTEXPR( "RoughnessAttenuation" ) ];
			const Float f = math::Lerp( unmapped, -1.f, 0.f );
			if ( f != scalarValue )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' RoughnessAttenuation[%1.3f] -> MaskRoughnessBias[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false );
			}
		}
	}
	else if ( paramName == RED_NAME_CONSTEXPR_NOREG( "BlurRadius" ) ) // BlurRadius = RemapTo01(BlurRadius, 0.f, 5.f);
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR_NOREG( "BlurRadius" ) ) )
		{
			const Float unmapped = params[ RED_NAME_CONSTEXPR( "BlurRadius" ) ];
			const Float f = Material_RemapTo01( unmapped, 0.f, 0.5f );
			if ( f != scalarValue )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' BlurRadius[%1.3f] -> BlurRadius[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, f, false );
			}
		}
	}
	else if ( paramName == RED_NAME_CONSTEXPR_NOREG( "BlurByRoughness" ) ) // BlurByRoughness = RemapTo01(BlurRoughness, 0.f, 4.f);
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR_NOREG( "BlurRoughness" ) ) )
		{
			const Float unmapped = params[ RED_NAME_CONSTEXPR( "BlurRoughness" ) ];
			const Float f = Material_RemapTo01( unmapped, 0.f, 4.f );
			if ( f != scalarValue )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' BlurRoughness[%1.3f] -> BlurByRoughness[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, f, false );
			}
		}
	}
	else if ( paramName == RED_NAME_CONSTEXPR_NOREG( "OpacityBackFace" ) ) // OpacityBackFace = RenderBackFaces;
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR_NOREG( "RenderBackFaces" ) ) )
		{
			const Float unmapped = params[ RED_NAME_CONSTEXPR( "RenderBackFaces" ) ];
			const Float f = unmapped;
			if ( f != scalarValue )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' RenderBackFaces[%1.3f] -> OpacityBackFace[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, f, false );
			}
		}
	}
	else if ( paramName == RED_NAME_CONSTEXPR_NOREG( "TintFromVertexPaint" ) ) // TintFromVertexPaint = 0.f;
	{
		if ( scalarValue != 0.f )
		{
			Float unmapped = 0.f;
			Float f = 0.f;
			RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' TintFromVertexPaint[%1.3f] -> TintFromVertexPaint[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue );
			matInstance->WriteParameter_NoDefinitionCheck( paramName, 0.f, false );
		}
	}
	else if (paramName == RED_NAME_CONSTEXPR_NOREG("MaskOpacity")) // MaskOpacity = saturate( MaskOpacity + SurfaceOpacity );
	{
		Float oldMaskOpacity = 0.f;
		Float oldSurfaceOpacity = 0.f;
		Float f = 0.f;
		if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("MaskOpacity")))
		{
			oldMaskOpacity = params[RED_NAME_CONSTEXPR("MaskOpacity")];
			f += oldMaskOpacity;
		}
		if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("SurfaceOpacity")))
		{
			oldSurfaceOpacity = params[RED_NAME_CONSTEXPR("SurfaceOpacity")];
			f += oldSurfaceOpacity;
		}

		// saturate
		f = math::Clamp(f, 0.f, 1.f);
		if (f != scalarValue)
		{
			RED_LOG_INFO("FrostedGlass Remapper:     '%hs' (MaskOpacity[%1.3f] + SurfaceOpacity[%1.3f]) -> MaskOpacity[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), oldMaskOpacity, oldSurfaceOpacity, f, scalarValue);
			matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
		}
	}
	else if (paramName == RED_NAME_CONSTEXPR_NOREG("Opacity")) // Opacity = 1.f;
	{
		if (scalarValue != 1.f)
		{
			Float unmapped = 1.f;
			Float f = 1.f;
			RED_LOG_INFO("FrostedGlass Remapper:     '%hs' Opacity[%1.3f] -> Opacity[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
			matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
		}
	}
	else
	{
		if ( params.KeyExist( paramName ) )
		{
			const Float unmapped = params[ paramName ];
			const Float f = params[ paramName ];
			if ( scalarValue != f )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' %hs[%1.3f] -> %hs[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), unmapped, paramName.AsChar(), f, scalarValue );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, f, false );
			}
		}
	}
}

void Material_RemapFrostedGlassCommandlet::RemapColor( const red::HashMap< CName, Color >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial )
{
	Color colorValue;
	RED_VERIFY( GetParam( baseMaterial, m_glassMaterial, paramName, colorValue ) );
	if ( paramName == RED_NAME_CONSTEXPR_NOREG( "TintColor" ) )
	{
		if ( params.KeyExist(RED_NAME_CONSTEXPR_NOREG( "ColorMultiplier" ) ) )
		{
			const Color& c = params[ RED_NAME_CONSTEXPR( "ColorMultiplier" ) ];
			if ( colorValue != c )
			{
				String s;
				ToString( s, c );
				String originalS;
				ToString( originalS, colorValue );
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar() );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, c, false );
			}
		}
	}
	else
	{
		if ( params.KeyExist( paramName ) )
		{
			const Color& c = params[ paramName ];
			if ( colorValue != c )
			{
				String s;
				ToString(s, c);
				String originalS;
				ToString(originalS, colorValue);
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar() );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, c, false );
			}
		}
	}
}

void Material_RemapFrostedGlassCommandlet::RemapVector( const red::HashMap< CName, Vector4 >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial )
{
	Vector4 vectorValue;
	RED_VERIFY( GetParam( baseMaterial, m_glassMaterial, paramName, vectorValue ) );
	if ( params.KeyExist( paramName ) )
	{
		const Vector4& v = params[ paramName ];
		if ( vectorValue != v )
		{
			String s;
			ToString(s, v);
			String originalS;
			ToString( originalS, vectorValue );
			RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar() );
			matInstance->WriteParameter_NoDefinitionCheck( paramName, v, false );
		}
	}
}

void Material_RemapFrostedGlassCommandlet::RemapTexture( const red::HashMap< CName, TResRef< ITexture > >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial )
{
	TResRef< ITexture > textureValue;
	RED_VERIFY( GetParam( baseMaterial, m_glassMaterial, paramName, textureValue ) );
	if ( paramName == RED_NAME_CONSTEXPR_NOREG( "MaskTexture" ) )
	{
		if ( params.KeyExist( RED_NAME_CONSTEXPR( "Diffuse" ) ) )
		{
			const TResRef< ITexture >& t = params[ RED_NAME_CONSTEXPR("Diffuse" ) ];
			if ( textureValue.GetPath() != t.GetPath() )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' Diffuse[%hs] -> MaskTexture[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), t.GetPath().ToDebugString(), t.GetPath().ToDebugString(), textureValue.GetPath().ToDebugString() );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, t, false );
			}
		}
	}
	else
	{
		if ( params.KeyExist( paramName ) )
		{
			const TResRef< ITexture >& t = params[ paramName ];
			if ( textureValue.GetPath() != t.GetPath() )
			{
				RED_LOG_INFO( "FrostedGlass Remapper:     '%hs' Diffuse[%hs] -> MaskTexture[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), t.GetPath().ToDebugString(), t.GetPath().ToDebugString(), textureValue.GetPath().ToDebugString() );
				matInstance->WriteParameter_NoDefinitionCheck( paramName, t, false );
			}
		}
	}
}

Bool Material_RemapFrostedGlassCommandlet::ProcessMaterialInstance( const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial )
{
	RED_LOG_INFO( "FrostedGlass Remapper: Processing: %hs with base %hs", matInstance->GetPath().ToDebugString(), matInstance->GetBaseMaterialPath().ToDebugString() );

	red::HashMap< CName, Float > scalarParams;
	red::HashMap< CName, Vector4 > vectorParams;
	red::HashMap< CName, Color > colorParams;
	red::HashMap< CName, TResRef< ITexture > > texParams;

	const auto& fg_parameterDefinitions = m_frostedGlassMaterial->GetParameterDefinitions( MSH_PixelShader );
	Float scalarValue;
	TResRef< ITexture > textureValue;
	Color colorValue;
	Vector4 vectorValue;
	for ( const IMaterialDataProvider::ParameterDefinition& paramDef : fg_parameterDefinitions )
	{
		switch ( paramDef.m_type )
		{
			case IMaterialDataProviderDesc::PT_Scalar:	if ( GetParam( matInstance, m_frostedGlassMaterial, paramDef.m_name, scalarValue ) )	scalarParams.Insert( paramDef.m_name, scalarValue );	break;
			case IMaterialDataProviderDesc::PT_Texture:	if ( GetParam( matInstance, m_frostedGlassMaterial, paramDef.m_name, textureValue ) )	texParams.Insert( paramDef.m_name, textureValue );		break;
			case IMaterialDataProviderDesc::PT_Color:	if ( GetParam( matInstance, m_frostedGlassMaterial, paramDef.m_name, colorValue ) )		colorParams.Insert( paramDef.m_name, colorValue );		break;
			case IMaterialDataProviderDesc::PT_Vector:	if ( GetParam( matInstance, m_frostedGlassMaterial, paramDef.m_name, vectorValue ) )	vectorParams.Insert( paramDef.m_name, vectorValue );	break;
		}
	}

	matInstance->ClearInstanceParameters();
	
	const auto& parameterDefinitions = m_glassMaterial->GetParameterDefinitions( MSH_PixelShader );
	for ( const IMaterialDataProvider::ParameterDefinition& paramDef : parameterDefinitions )
	{
		switch ( paramDef.m_type )
		{
		case IMaterialDataProviderDesc::PT_Scalar:	RemapScalar( scalarParams, paramDef.m_name, matInstance, baseMaterial );	break;
		case IMaterialDataProviderDesc::PT_Texture:	RemapTexture( texParams, paramDef.m_name, matInstance, baseMaterial );		break;
		case IMaterialDataProviderDesc::PT_Color:	RemapColor( colorParams, paramDef.m_name, matInstance, baseMaterial );		break;
		case IMaterialDataProviderDesc::PT_Vector:	RemapVector( vectorParams, paramDef.m_name, matInstance, baseMaterial );	break;
		}
	}

	return true;
}

Bool Material_RemapFrostedGlassCommandlet::HandleMaterialInstance( const THandle< CMaterialInstance >& resource, const THandle< IMaterial >& baseMaterial )
{
	ProcessMaterialInstance( resource, baseMaterial );
	HandleResource( resource->GetPath(), resource );
	resource->SetBaseMaterial( baseMaterial );
	return true;
}

Bool Material_RemapFrostedGlassCommandlet::HandleResource( const res::ResourcePath& resourcePath, const THandle< IMaterial >& baseMaterial )
{
	if ( !resourcePath.IsValid() || resourcePath.IsEmpty() )
	{
		RED_LOG_ERROR( "FrostedGlass remapper: Invalid path: '%hs'", resourcePath.ToDebugString() );
		m_hasErrors = true;
		return false;
	}

	if (m_processedResources.Exist(resourcePath))
	{
		RED_LOG_ERROR( "FrostedGlass remapper: Resource already processed '%hs'", resourcePath.ToDebugString() );
		return true;
	}

	m_processedResources.Insert( resourcePath );

	resdb::Links backwardLinks = m_database.GetBackwardLinks( resourcePath );
	for ( const resdb::Link& link : backwardLinks )
	{
		res::IssueLoadingRequestParameter loadingRequestParameter;
		loadingRequestParameter.path = link.from;
		loadingRequestParameter.skipBuffers = true;
		loadingRequestParameter.skipPostLoad = false;
		loadingRequestParameter.postLoadFlags.skipUploadRenderData = true;

		const THandle< CResource > resource = GResourceLoader->IssueLoadingRequest( loadingRequestParameter )->WaitUntilLoaded();
		if ( !resource )
		{
			RED_LOG_ERROR( "FrostedGlass remapper: Failed to load '%hs'", link.from.ToDebugString() );
			m_hasErrors = true;
			continue;
		}

		resource->MarkModified();
		
		Bool shouldSave = false;

		const red::StringView parentFileExtension = red::paths::GetExtension( link.from.ToStringView() );
		if ( parentFileExtension == CMaterialInstance::GetFileExtension() || parentFileExtension == CMaterialInstance::GetDeprecatedFileExtension() )
		{
			const THandle< CMaterialInstance > matInstance = Cast< CMaterialInstance >( resource );
			if ( !matInstance )
			{
				RED_LOG_ERROR( "FrostedGlass remapper: Failed to load '%hs'", link.from.ToDebugString() );
				m_hasErrors = true;
				return false;
			}
			HandleMaterialInstance( matInstance, baseMaterial );

			shouldSave = true;
		}

		if (shouldSave)
		{
			red::AbsolutePath resourceAbsolutePath = m_editorDepot->TranslateResourcePath(resource->GetPath());
			GFileManager->SetFileReadOnly(resourceAbsolutePath, false);

			res::ResourceSavingOptions options;
			options.m_depot = m_editorDepot;
			if (res::SaveResource(resource, options))
			{
				RED_LOG_INFO("FrostedGlass remapper: Saved '%hs'", resource->GetPath().ToDebugString());
			}
			else
			{
				RED_LOG_ERROR("FrostedGlass remapper: Could not save '%hs'", resource->GetPath().ToDebugString());
				m_hasErrors = true;
			}
		}
	}

	return true;
}

Bool Material_RemapFrostedGlassCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseParams( options ) )
	{
		return false;
	}

	HandleResource( m_frostedGlassMaterialPath, m_glassMaterial );

	return !m_hasErrors;
}

