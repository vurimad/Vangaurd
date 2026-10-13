/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialTemplate.h"

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
#include "materialCompilationManager.h"
#endif
#include "materialParameter.h"
#include "bitmapTexture.h"

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
#include "../../redCore/include/processRunner.h"
#endif

#include "../../redFileSystem/include/fileSkipableBlock.h"
#include "../../redReflection/include/variant.h"

#include "../../renderData/include/renderMaterialHelpers.h"
#include "../../renderData/include/renderPublicEnums.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
#include "../../shaderCache/include/shaderCacheManager.h"
#include "../../shaderCache/include/shaderCacheCompilationUtils.h"
#endif
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
#include "../../renderData/include/renderSettingsVars.h"
#endif

#include "../../redJobs2/include/jobBuilder.h"

using red::DynArray;

// #pragma optimize("",off)

//////////////////////////////////////////////////////////////////////////

namespace Version
{
	enum Enum
	{
		BASE,
		EMISSIVE_RESAVE,
		EMISSIVE_RESAVE2,
		DECAL_MATERIALS_EMISSIVE_RESAVE,
		DISMEMBERMENT_OFF_BY_DEFAULT,

		NEWEST = DISMEMBERMENT_OFF_BY_DEFAULT
	};
};

RTTI_BEGIN_TYPE( CMaterialTemplate );
{
	RTTI_PARENT_TYPE( IMaterialDefinition );
	RTTI_PROPERTY( m_name ).readOnly();
	RTTI_PROPERTY( m_parameters ).editable().inlined();
	RTTI_PROPERTY( m_techniques ).readOnly();
	RTTI_PROPERTY( m_samplerStates ).readOnly();
	RTTI_PROPERTY( m_usedParameters ).readOnly();
	RTTI_PROPERTY( m_materialPriority ).readOnly();
	RTTI_PROPERTY( m_materialType ).readOnly();
	RTTI_PROPERTY( m_audioTag ).editable().tooltip( "Should be set only on decal materials that affect the footsteps sounds, keep it empty otherwise" );

	RTTI_PROPERTY( m_resourceVersion );
}
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////

CMaterialTemplate::CMaterialTemplate()
	: m_isValid( true )
	, m_materialPriority( EMP_Normal )
	, m_materialType( ERenderMaterialType::RMT_Standard )
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	, m_fileTimes( rend::PoolRenderData() )
#endif // NO_RUNTIME_MATERIAL_COMPILATION
	, m_isGlass( false )
{
}

void CMaterialTemplate::GetTechniquesFeatures( red::DynArray< TechniqueFeatures >& outTechFeatures ) const
{
	outTechFeatures.Resize( m_techniques.Size() );
	for ( Uint32 tech_i = 0; tech_i < m_techniques.Size(); ++tech_i )
	{
		const auto &src = m_techniques[ tech_i ];
		TechniqueFeatures &dst = outTechFeatures[ tech_i ];
		dst.m_mandatoryFlags = src.m_featureFlagsEnabledMask.InternalGetFlagsMask();
	}
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
red::DateTime CMaterialTemplate::GetTimeStamp( const CName stagePassName, EMaterialShaderTarget target ) const
{
	StagePassFileTimes fileTimes;
	if ( m_fileTimes.Find( stagePassName, fileTimes ) )
	{
		return fileTimes.fileTime[ target ];
	}
	return red::DateTime();	
}

void CMaterialTemplate::SetTimeStamp( const CName stagePassName, EMaterialShaderTarget target, red::DateTime timestamp )
{
	if ( m_fileTimes.KeyExist( stagePassName ) )
	{
		m_fileTimes[ stagePassName ].fileTime[ target ] = timestamp;
	}
}
#endif // NO_RUNTIME_MATERIAL_COMPILATION

void CMaterialTemplate::GetPasses( Uint32 techniqueIndex, EngineStagePassDataCollection& stagePassCollection ) const
{
	if ( techniqueIndex < m_techniques.Size() )
	{
		const auto &technique = m_techniques[techniqueIndex];
		stagePassCollection.Reserve( stagePassCollection.Size() + technique.m_passes.Size() );
		for ( const auto& pass : technique.m_passes )
		{
			stagePassCollection.PushBack( EngineStagePassData() );
			EngineStagePassData& stagePassDesc = stagePassCollection.Back();

			stagePassDesc.m_stageNameRegular	= pass.m_stagePassNameRegular;
			stagePassDesc.m_stageNameDiscarded	= pass.m_stagePassNameDiscarded;
			stagePassDesc.m_enablePixelShader	= pass.m_enablePixelShader;
			stagePassDesc.m_streamsToBind		= technique.m_streamsToBind;
			stagePassDesc.m_orderIndex			= pass.m_orderIndex;

			SOMState& omState					= stagePassDesc.m_omState;
			omState.m_depthStencilModeDesc		= pass.m_depthStencilMode;
			omState.m_rasterizerModeDesc		= pass.m_rasterizerMode;
			omState.m_blendModeDesc				= pass.m_blendMode;
			omState.m_stencilReadMask			= pass.m_stencilReadMask;
			omState.m_stencilWriteMask			= pass.m_stencilWriteMask;
			omState.m_stencilRef				= pass.m_stencilRef;
		}
	}
}

Bool CMaterialTemplate::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "pixelParameters" ) )
	{
		TMaterialParameters oldParams{ job::PoolJobScope() };
		RED_VERIFY( readValue.Get( oldParams ) );
		m_parameters[ MSH_PixelShader ] = oldParams;
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "vertexParameters" ) )
	{
		TMaterialParameters oldParams{ job::PoolJobScope() };
		RED_VERIFY( readValue.Get( oldParams ) );
		m_parameters[ MSH_VertexShader ] = oldParams;
		return true;
	}

	return TBaseClass::OnPropertyMissing( propertyName, readValue );
}

Bool CMaterialTemplate::OnPropertyPreChange(const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue)
{
	//texture group validation
	if ( inOutValue->IsSingle() )
	{
		if ( const CMaterialParameter* param = FindParameter( RED_NAME_NOREG( path.ToString() ) ) )
		{
			// only textures need to be validated and only if there is a texture group specified for the parameter
			if ( param->GetType() == IMaterialDataProviderDesc::PT_Texture
				&& static_cast< const CMaterialParameterTexture* >( param )->GetTextureGroup() != GpuWrapApi::TEXG_Unknown )
			{
				String resourcePath = inOutValue->GetSingle()->ToString();

				res::LoadingOptions loadingOptions;
				loadingOptions.skipPostLoad = true;
				THandle< CResource > res = GResourceLoader->IssueLoadingRequest( res::Convert( res::ResourcePath::Build( resourcePath ), loadingOptions ) )->WaitUntilLoaded();
				THandle< CBitmapTexture > tex = Cast< CBitmapTexture >( res );
				GpuWrapApi::eTextureGroup textureGroupData = tex->GetTextureSetup().m_group;
				if ( static_cast< const CMaterialParameterTexture* >( param )->GetTextureGroup() != textureGroupData )
				{
					//TODO moradin some kind of feedback to the user?
					return false;
				}
			}
		}
	}

	return TBaseClass::OnPropertyPreChange( path, inOutValue );
}

void CMaterialTemplate::OnPreSave( const PreSaveContext& context )
{
	m_resourceVersion = Version::NEWEST;

	TBaseClass::OnPreSave( context );
}

namespace Local
{
	Bool CanHaveDismemberment( const CName name )
	{
		red::DynArray< CName > dismAllowed { rend::PoolMesh() };
		dismAllowed.Reserve( 30 );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "metal_base" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "metal_base_garment" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "metal_base_glitter" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "multilayered" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "device_diode" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "cloth_mov" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "cloth_f_mov" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "cloth_mov_multilayered" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "eye" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "eye_gradient" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "eye_shadow" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "glass" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "frosted_glass" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "hair" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_double_diffuse" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_emissive" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_emissive_subsurface" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_gradientmap_recolor" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_gradientmap_recolor_2" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_gradientmap_recolor_emissive" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_multitinted" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_particles" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "mesh_decal_revealed" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "skin" ) );

		// suggestions
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "loot_drop_highlight" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "debug_coloring" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "shadows_debug" ) );
		dismAllowed.PushBack( RED_NAME_CONSTEXPR( "multilayered_debug" ) );

		for ( const CName dismAllowedName : dismAllowed )
		{
			if ( dismAllowedName == name )
			{
				return true;
			}
		}
		return false;
	}
}

void CMaterialTemplate::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	m_isGlass = m_name == RED_NAME_CONSTEXPR_NOREG( "glass" );
	m_isSpeedTreeBillboard = m_name == RED_NAME_CONSTEXPR_NOREG( "speedtree_3d_v8_billboard" );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	m_fileTimes.Clear();
	for ( const auto& technique : m_techniques )
	{
		for ( const auto& pass : technique.m_passes )
		{
			StagePassFileTimes fileTimes;
			for ( Uint32 i = MSH_First; i < MSH_MAX; ++i )
			{
				fileTimes.fileTime[ i ] = red::DateTime();
			}
			m_fileTimes.Insert( pass.m_stagePassNameRegular, fileTimes );
		}
	}
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	if ( m_resourceVersion < Version::EMISSIVE_RESAVE2 )
	{
		// swap parameters when new ones need to be resaved
		for( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			for( Int32 i = m_parameters[target].Size() - 1; i >= 0; --i )
			{
				THandle< CMaterialParameter > param = m_parameters[target][i];
				if( param.Get() )
				{
					if( param->GetName() == RED_NAME_CONSTEXPR_NOREG( "EmissiveBrightness" ) && param->GetType() == IMaterialDataProviderDesc::PT_Scalar )
					{
						param->SetName( RED_NAME_CONSTEXPR( "EmissiveEV" ) );
						param->SetMin( 0.0f );
						param->SetMax( 10.0f );

						const rtti::Property* prop = param->GetProperty();

						Float* propData = (Float*)prop->GetOffsetPtr( param.Get() );

						*propData = Clamp( std::log2( *propData ) * (10 / 16.0f), 0.0f, 10.0f );
					}
					else
					{
						// Put some new material names here if needed
					}
				}
			}

			for( Int32 i = m_usedParameters[target].Size() - 1; i >= 0; --i )
			{
				MaterialUsedParameter& usedParam = m_usedParameters[target][i];
				if( usedParam.m_name == RED_NAME_CONSTEXPR_NOREG( "EmissiveBrightness" ) )
				{
					usedParam.m_name = RED_NAME_CONSTEXPR( "EmissiveEV" );
				}
			}
		}
	}
	if ( m_resourceVersion < Version::DECAL_MATERIALS_EMISSIVE_RESAVE )
	{
		// swap parameters when new ones need to be resaved
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			for ( Int32 i = m_parameters[target].Size() - 1; i >= 0; --i )
			{
				THandle< CMaterialParameter > param = m_parameters[target][i];
				if (param.Get())
				{
					if ( param->GetName() == RED_NAME_CONSTEXPR_NOREG( "EmissiveIntensity" ) && param->GetType() == IMaterialDataProviderDesc::PT_Scalar )
					{
						param->SetName( RED_NAME_CONSTEXPR( "EmissiveEV" ) );

						const rtti::Property* prop = param->GetProperty();

						Float* propData = (Float*)prop->GetOffsetPtr(param.Get());

						*propData = Clamp(std::log2(*propData) * (10.f / 16.0f), 0.0f, 10.0f);
					}
					else
					{
						// Put some new material names here if needed
					}
				}
			}
			for( Int32 i = m_usedParameters[ target ].Size() - 1; i >= 0; --i )
			{
				MaterialUsedParameter& usedParam = m_usedParameters[ target ][ i ];
				if( usedParam.m_name == RED_NAME_CONSTEXPR_NOREG( "EmissiveIntensity" ) )
				{
					usedParam.m_name = RED_NAME_CONSTEXPR( "EmissiveEV" );
				}
			}
		}
	}
	if ( m_resourceVersion < Version::DISMEMBERMENT_OFF_BY_DEFAULT )
	{
		m_canHaveDismemberment = Local::CanHaveDismemberment( m_name );
	}


#if !defined( VANGUARD_ASSET_HOST )
	if ( !context.flags.skipUploadRenderData && GRender && !GRender->IsDeviceLost() )
	{
		// If we're doing render data, create the material render resource now, and provide a job builder to sync with
		// any required async work. This allows the renderer to do some async caching, and include that caching as part
		// of the material resource loading.

		job::Builder postloadBuilder { job::Priority::Latent };

		RED_SCOPE_LOCK( m_createRenderResourceLock );

		// Create rendering resource for material
		m_renderResource = GRender->UploadMaterial( GetMaterialDefinition(), this, &postloadBuilder );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		if ( m_renderResource )
		{
			red::ScopedLock< red::Mutex > mutex( st_accessMutex );
			// Register in the list
			if ( !st_allMaterials.Exist( this ) )
			{
				RED_VERIFY( st_allMaterials.Insert( this ).IsSuccessful(), "Material %hs was not added to global list, expect problems", GetFriendlyName().AsChar() );
			}
		}
#endif

		m_postloadCounter.Reset( postloadBuilder.ExtractWaitCounter() );
	}
#endif
}


const job::Counter* CMaterialTemplate::HACK_GetPostLoadWaitCounter() const
{
	return &m_postloadCounter;
}


Bool CMaterialTemplate::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) )
	{
		String stringName = readValue.Get< String >();
		m_name = RED_NAME( stringName );
		return true;
	}

	return IMaterialDefinition::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// START OF HELPERS

static Bool ParameterExists( TMaterialParameters& parameters, const CName& paramName, IMaterialDataProviderDesc::EParameterType paramType, THandle< CMaterialParameter >*& outParam )
{
	for ( auto& param : parameters )
	{
		if ( param->GetName() == paramName && param->GetType() == paramType )
		{
			outParam = &param;
			return true;
		}
	}
	return false;
}

static Bool UsedParameterExists( TMaterialUsedParameterArray& usedParameters, const CName& usedParamName, MaterialUsedParameter*& outUsedParameter )
{
	for ( auto& usedParam : usedParameters )
	{
		if ( usedParam.m_name == usedParamName )
		{
			outUsedParameter = &usedParam;
			return true;
		}
	}
	return false;
}

static Uint32 CreateRenderingParams( const TMaterialParameters& parameters, IMaterialDataProvider::TParameterDefinitionArray& parameterDefinitions )
{
	// Clear list of parameters
	parameterDefinitions.Clear();

	Uint32 paramBlockSize = 0;
	for ( auto parameter : parameters )
	{
		// Map parameter
		const Uint32 paramOffset = paramBlockSize;
		paramBlockSize += parameter->GetDataSize();
		parameterDefinitions.PushBack( IMaterialDataProvider::ParameterDefinition( parameter->GetType(), parameter->GetName(), (Uint16)paramOffset ) );
	}
	return paramBlockSize;
}

// returns true if any parameter was casted on different (suitable) type.
static Bool PerformPotentialParametersCast(IMaterialDefinition* parent, Int32 target, TMaterialParameters& parameters, const MaterialMetaDefinition& metaDefinition)
{
	if ( parameters.Empty() )
	{
		return false;
	}
	
	Bool wasModified = false;
#ifndef RED_CONFIGURATION_FINAL
	for ( Int32 i = parameters.Size() - 1; i >= 0; --i )
	{
		const THandle< CMaterialParameter > param = parameters[ i ];
		if ( param.Get() )
		{
			const auto paramType = param->GetType();
			const auto paramName = param->GetName().AsChar();
			IMaterialDataProviderDesc::EParameterType mismatchedType;
			if ( metaDefinition.ContainsParameterWithMatchingNameButNotType(paramName, paramType, target, mismatchedType ) )
			{
				// Meta definition contains PT_Vector, while editor holds PT_Color.
				if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Vector && paramType == IMaterialDataProviderDesc::EParameterType::PT_Color)
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 1.0f, param->GetTooltip() ) )
					{
						const auto paramColor = ( static_cast< CMaterialParameterColor* > ( param.Get() ) )->GetData();
						const Vector4 scaledVector( static_cast< Float >( paramColor.R ) / 255.0f, static_cast< Float >( paramColor.G ) / 255.0f, static_cast< Float >( paramColor.B ) / 255.0f, static_cast< Float >( paramColor.A ) / 255.0f );
						static_cast< CMaterialParameterVector* > ( newParam.Get() )->m_vector = scaledVector;
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
				// Meta definition contains PT_Color, while editor holds PT_Vector.
				else if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Color && paramType == IMaterialDataProviderDesc::EParameterType::PT_Vector )
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 255.0f, param->GetTooltip() ) )
					{
						const auto paramVector = ( static_cast< CMaterialParameterVector* > ( param.Get() ) )->m_vector * 255.0f;
						const Color scaledColor( static_cast< unsigned char >( Clamp( paramVector.X, 0.0f, 255.0f ) ), 
							                     static_cast< unsigned char >( Clamp( paramVector.Y, 0.0f, 255.0f ) ), 
							                     static_cast< unsigned char >( Clamp( paramVector.Z, 0.0f, 255.0f ) ), 
							                     static_cast< unsigned char >( Clamp( paramVector.X, 0.0f, 255.0f ) ) );
						static_cast< CMaterialParameterColor* > ( newParam.Get() )->SetData( scaledColor );
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
				// Meta definition contains PT_Scalar, while editor holds PT_Color.
				else if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Scalar && paramType == IMaterialDataProviderDesc::EParameterType::PT_Color )
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 255.0f, param->GetTooltip() ) )
					{
						const auto paramColor = ( static_cast< CMaterialParameterColor* > ( param.Get() ) )->GetData();
						static_cast< CMaterialParameterScalar* > ( newParam.Get() )->SetData( static_cast<Float>( paramColor.R ) / 255.0f );
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
				// Meta definition contains PT_Color, while editor holds PT_Scalar.
				else if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Color && paramType == IMaterialDataProviderDesc::EParameterType::PT_Scalar )
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 255.0f, param->GetTooltip() ) )
					{
						const auto paramClampedScalarAsByte = static_cast< unsigned char > ( Clamp( static_cast< CMaterialParameterScalar* > ( param.Get() )->GetData(), 0.0f, 255.0f ) );
						const Color colorFromScalar( paramClampedScalarAsByte, paramClampedScalarAsByte, paramClampedScalarAsByte, 1 );
						static_cast< CMaterialParameterColor* > ( newParam.Get() )->SetData( colorFromScalar );
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
				// Meta definition contains PT_Scalar, while editor holds PT_Vector.
				else if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Scalar && paramType == IMaterialDataProviderDesc::EParameterType::PT_Vector )
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 255.0f, param->GetTooltip() ) )
					{
						const auto paramVector = ( static_cast< CMaterialParameterVector* > ( param.Get() ) )->m_vector;
						static_cast< CMaterialParameterScalar* > ( newParam.Get() )->SetData( static_cast< Float >( paramVector.X ) );
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
				// Meta definition contains PT_Vector, while editor holds PT_Scalar.
				else if ( mismatchedType == IMaterialDataProviderDesc::EParameterType::PT_Vector && paramType == IMaterialDataProviderDesc::EParameterType::PT_Scalar )
				{
					if ( auto newParam = CMaterialParameter::Create( parent, param->GetName(), mismatchedType, param->GetRegister(), 0.0f, 255.0f, param->GetTooltip() ) )
					{
						const auto paramScalarClamped = Clamp( static_cast< CMaterialParameterScalar* > ( param.Get() )->GetData(), 0.0f, 1.0f );
						const Vector4 vectorFromScalar( paramScalarClamped, paramScalarClamped, paramScalarClamped, paramScalarClamped );
						static_cast< CMaterialParameterVector* > ( newParam.Get() )->m_vector = vectorFromScalar;
						parameters.InsertAt( i, newParam );
						wasModified = true;
					}
				}
			}
		}
	}
#endif // RED_CONFIGURATION_FINAL
	return wasModified;
}

// returns true if any parameter was removed
Bool RemoveNonExistingParameters( Int32 target, TMaterialParameters& parameters, TMaterialUsedParameterArray& usedParameters, const MaterialMetaDefinition& metaDefinition )
{
	if ( parameters.Empty() )
	{
		return false;
	}

	Bool wasModified = false;
	for ( Int32 i = parameters.Size() - 1; i >= 0; --i )
	{
		THandle< CMaterialParameter > param = parameters[ i ];
		if ( param.Get() )
		{
			if ( !metaDefinition.ContainsParameter( param->GetName().AsChar(), param->GetType(), target ) )
			{
				parameters.RemoveAt( i );
				wasModified = true;
			}
		}
		else
		{
			parameters.RemoveAt( i );
			wasModified = true;
		}
	}

	for ( Int32 i = usedParameters.Size() - 1; i >= 0; --i )
	{
		MaterialUsedParameter& usedParam = usedParameters[ i ];
		if ( !metaDefinition.ContainsUsedParameter( usedParam.m_name.AsChar(), target ) )
		{
			usedParameters.RemoveAt( i );
			wasModified = true;
		}
	}

	return wasModified;
}

// END OF HELPERS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Bool CMaterialTemplate::UpdateParametersList( const MaterialMetaDefinition& materialMetaDefinition )
{
	Bool wasModified = false;

#ifndef RED_CONFIGURATION_FINAL
	// Check type casting.
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		if ( PerformPotentialParametersCast( this, target, m_parameters[target], materialMetaDefinition ) )
		{
			wasModified = true;
		}
	}

	// remove null or non-existing parameters
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		if ( RemoveNonExistingParameters( target, m_parameters[ target ], m_usedParameters[ target ], materialMetaDefinition ) )
		{
			wasModified = true;
		}
	}

	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		for ( Uint32 i = 0; i < materialMetaDefinition.m_parameters[ target ].Size(); ++i )
		{
			const MaterialMetaDefinition::Param& parameter = materialMetaDefinition.m_parameters[ target ][ i ];

			CName paramName = RED_NAME( parameter.m_name );

			// Map parameter
			THandle< CMaterialParameter >* existingParam = nullptr;
			if ( !ParameterExists( m_parameters[ target ], paramName, parameter.m_type, existingParam ) )
			{
				if ( auto newParam = CMaterialParameter::Create( this, paramName, parameter.m_type, parameter.m_register, parameter.m_min, parameter.m_max, parameter.m_tooltip ) )
				{
					newParam->SetHidden( parameter.m_hidden );
					m_parameters[ target ].InsertAt( i, newParam );
					wasModified = true;
				}
			}
			else
			{
				RED_FATAL_ASSERT( existingParam, "MaterialParameter found but null returned." );
				if ( (*existingParam)->GetRegister() != parameter.m_register )
				{
					wasModified = true;
				}
				if ( (*existingParam)->CanHaveMinMax() &&
						( (*existingParam)->GetMin() != parameter.m_min || (*existingParam)->GetMax() != parameter.m_max )
					)
				{
					wasModified = true;
				}
				if ( (*existingParam)->GetTooltip() != parameter.m_tooltip )
				{
					wasModified = true;
				}
				if ( (*existingParam)->GetCustomEditorName() != parameter.m_customEditorName )
				{
					wasModified = true;
				}
				if ( (*existingParam)->IsHidden() != parameter.m_hidden )
				{
					wasModified = true;
				}
				if ((*existingParam)->GetType() == IMaterialDataProviderDesc::PT_Texture &&
					static_cast<CMaterialParameterTexture*>((existingParam->Get()))->GetTextureGroup() != parameter.m_textureGroup)
				{
					wasModified = true;
					static_cast<CMaterialParameterTexture*>((existingParam->Get()))->SetTextureGroup(parameter.m_textureGroup);
				}

				(*existingParam)->SetRegister( parameter.m_register );
				(*existingParam)->SetMin( parameter.m_min );
				(*existingParam)->SetMax( parameter.m_max );
				(*existingParam)->SetTooltip( parameter.m_tooltip );
				(*existingParam)->SetHidden( parameter.m_hidden );
				(*existingParam)->SetCustomEditorName( parameter.m_customEditorName );
			}

			MaterialUsedParameter* existingUsedParam = nullptr;
			if ( !UsedParameterExists( m_usedParameters[ target ], paramName, existingUsedParam ) )
			{
				m_usedParameters[ target ].PushBack( MaterialUsedParameter( RED_NAME( parameter.m_name ), parameter.m_register ) );
				wasModified = true;
			}
			else
			{
				RED_FATAL_ASSERT( existingUsedParam, "UsedParameter found but null returned." );
				if ( existingUsedParam->m_register != parameter.m_register )
				{
					wasModified = true;
					existingUsedParam->m_register = parameter.m_register;
				}
			}
		}

		if ( m_samplerStates[ target ] != materialMetaDefinition.m_samplerStates[ target ] )
		{
			m_samplerStates[ target ] = materialMetaDefinition.m_samplerStates[ target ];
			wasModified = true;
		}
	}

	{
		// make sure noone will use the parameters information while those are updated
		red::ScopedLock< red::SpinLock > lock( m_parametersAccessLock );
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			m_paramBlockSize[ target ] = CreateRenderingParams( m_parameters[ target ], m_parameterDefinitions[ target ] );
		}
	}

#endif // RED_CONFIGURATION_FINAL

	return wasModified;
}

Bool CMaterialTemplate::UpdateTechniques( const MaterialMetaDefinition& materialMetaDefinition )
{
	if ( m_techniques != materialMetaDefinition.m_techniques )
	{
		m_techniques.Clear();
		m_techniques.PushBack( materialMetaDefinition.m_techniques );
		return true;
	}
	return false;
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
Bool CMaterialTemplate::ShouldRecompile( const Bool includesMatch ) const
{
	if ( GetName().Empty() )
	{
		return false;
	}

	if ( GetPath().IsEmpty() )
	{
		return false;
	}

	if ( !m_isValid )
	{
		return true;
	}

	if ( !includesMatch )
	{
		return true;
	}

	red::DateTime cachedModificationTime = GShaderCache->GetMaterialModificationTime( GetHash() );
	red::DateTime dt;
	{
		// find newest TimeStamp
		for ( const auto& fileTime : m_fileTimes )
		{
			for ( Uint32 i = MSH_First; i < MSH_MAX; ++i )
			{
				if ( fileTime.Value().fileTime[i] > dt )
				{
					dt = fileTime.Value().fileTime[ i ];
				}
			}
		}
	}
	if ( cachedModificationTime < dt )
	{
		return true;
	}

	return false;
}

Bool CMaterialTemplate::Recompile( const Bool includesMatch, const Bool onlyHighPriority )
{
#ifndef RED_PLATFORM_WINPC
	return true;
#else
	if ( !ShouldRecompile( includesMatch ) )
	{
		return true;
	}

	const Bool isAsyncCompilationEnabled = GRender->IsAsyncMaterialCompilationEnabled();
	if ( isAsyncCompilationEnabled )
	{
		GetMaterialCompilationManager().Register( HandleFromPtr( this ), onlyHighPriority );
		return true;
	}
	else
	{
		return RecompileSynchronously( includesMatch );
	}
#endif // RED_PLATFORM_WINPC
}

Bool CMaterialTemplate::RecompileSynchronously( const Bool includesMatch )
{
#ifndef RED_PLATFORM_WINPC
	return true;
#else
	// compile material synchonously
	const red::AbsolutePath& shaderCachePath = GShaderCache->GetPath();

	red::AbsolutePath rootPath = GFileManager->GetEngineRoot().AddDirPath( ".." );
		
	const red::AbsolutePath& engineRootPath = GFileManager->GetEngineRoot();
	const red::AbsolutePath& gameRootPath = GFileManager->GetGameRoot();

	const Bool useLocalCompiler = Config::cvShaderCompilerUseLocalCompiler.Get();

	red::AbsolutePath appPath;
	if ( !GetShaderCompilerToolPath( useLocalCompiler, rootPath, engineRootPath, SHADER_COMPILER_FILE_NAME, appPath ) )
	{
		return false;
	}

	String forceShaderCodeUnrollString;
	RED_VERIFY( ToString( forceShaderCodeUnrollString, !includesMatch ) );

	const Utf16String arguments = Utf16String( String::Printf( "-platform=pc -breakOnError -root=\"%hs\" -engineRoot=\"%hs\" -gameRoot=\"%hs\" -forceShaderCodeUnroll=%hs -material=\"%hs\"", rootPath.AsChar(), engineRootPath.AsChar(), gameRootPath.AsChar(), forceShaderCodeUnrollString.AsChar(), GetPath().ToStringView().Data() ).AsChar() );
	const Uint32 timeout = 1000 * 60 * 5; // 5 min

	// Start task
	CProcessRunner runner;
	String compilerOutput;

	runner.Run( appPath, arguments, red::utils::GetCurrentWorkingDirectory() );
	runner.WaitForFinish( timeout );
	runner.LogOutput( CProcessRunner::ELT_String, String::EMPTY(), &compilerOutput );
	red::DynArray< String > compilerOutputLines{ job::PoolJobScope() };
	compilerOutput.Slice( compilerOutputLines, "\r\n" );
	for ( const auto& line : compilerOutputLines )
	{
		if ( line.BeginsWith( "error:" ) )
		{
			RED_LOG_ERROR( line.AsChar() );
		}
		else
		{
			RED_LOG( line.AsChar() );
		}
	}
	runner.Terminate();

	m_isValid = runner.GetExitCode() == EShaderCompilerStatus::SHADER_COMPILER_STATUS_SUCCESS;
	if ( m_isValid )
	{
		red::AbsolutePath pathCopy = shaderCachePath;
		GShaderCache->Reload( pathCopy );
	
		while ( !GShaderCache->VerifyLoaded() )
		{
			RED_LOG( "Shader cache reloading after recompilation.." );
			red::SleepOnCurrentThread( 500 );
		}
	}
	
	ForceRecompilation();

	return m_isValid;
#endif // RED_PLATFORM_WINPC
}

#endif	//NO_RUNTIME_MATERIAL_COMPILATION

Bool CMaterialTemplate::WriteParameterRaw( const CName& name, const rtti::IType* dataType, const void* data, Bool /*recreateResource*/ /*= true*/ )
{
	Bool result = false;

	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		// Find parameter
		for ( auto param : m_parameters[ target ] )
		{
			if ( param && name == param->GetName() )
			{
				const rtti::Property* prop = param->GetProperty();
				// #TODO: Allow some limited conversion? If prop has something like Vector4, then could write a Float to
				// it. Would need to be handled on a per-type basis.
				if ( prop && prop->GetType() == dataType )
				{
					void* propData = prop->GetOffsetPtr( param.Get() );
					prop->GetType()->Copy( propData, data );
					result = true;
				}
			}
		}
	}

	// Not found
	return result;
}

Bool CMaterialTemplate::WriteParameterRaw_NoDefinitionCheck(const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource /*= true*/)
{
	return WriteParameterRaw(name, dataType, data, recreateResource );
}

Bool CMaterialTemplate::ReadParameterRaw( const CName& name, const rtti::IType* dataType, void* data ) const
{
	Bool result = false;

	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		// Find parameter
		for ( auto param : m_parameters[ target ] )
		{
			if ( param && name == param->GetName() )
			{
				const rtti::Property* prop = param->GetProperty();
				// #TODO: Allow some limited conversion? If prop has something like Float, then could read a Vector4
				// from it. Would need to be handled on a per-type basis.
				if ( prop && prop->GetType() == dataType )
				{
					const void* propData = prop->GetOffsetPtr( param.Get() );
					prop->GetType()->Copy( data, propData );
					result = true;
				}
			}
		}
	}

	// Not found
	return result;
}

Bool CMaterialTemplate::ReadParameterRaw_NoBase(const CName& name, const rtti::IType* dataType, void* data) const
{
	return ReadParameterRaw(name, dataType, data);
}

const CMaterialParameter* CMaterialTemplate::FindParameter( const CName& name ) const
{
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		for ( auto param : m_parameters[ target ] )
		{
			if ( param && name == param->GetName() )
			{
				return param.Get();
			}
		}
	}

	// Not found
	return nullptr;
}

Bool CMaterialTemplate::ValidateParameters( red::StringBuilder< red::String >& errorMessageBuilder ) const
{
	Bool result = true;
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		for ( const THandle< CMaterialParameter >& param : m_parameters[ target ] )
		{
			if ( param && !param->Validate( errorMessageBuilder ) )
			{
				result = false;
			}
		}
	}
	return result;
}

CName CMaterialTemplate::GetAudioTag() const
{
	// Check for value in this instance
	if ( m_audioTag && m_audioTag != CName::NONE() )
	{
		return m_audioTag;
	}

	// Invalid
	return CName::NONE();
}
