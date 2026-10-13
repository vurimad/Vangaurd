/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "material.h"
#include "materialDefinition.h"
#include "materialTemplate.h"
#include "materialInstance.h"
#include "materialParameter.h"
#include "materialParameterInstance.h"
#include "multilayerSetup.h"
#include "multilayerMask.h"
#include "terrainSetup.h"
#include "../../redReflection/include/rttiPathParser.h"
#include "../../renderData/include/renderUtils.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderCommandHandler.h"
#include "../../renderData/include/renderMaterialSetupParam.h"
#endif

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
#include "../../core/include/codeChunk.h"
#endif // NO_RUNTIME_MATERIAL_COMPILATION
#include "../../resourceMaterial/include/texture.h"
#include "../../resourceMaterial/include/dynamicTexture.h"

using red::DynArray;

RTTI_BEGIN_ABSTRACT_TYPE( IMaterial );
	RTTI_PARENT_TYPE( CResource );
RTTI_END_TYPE();

RTTI_BEGIN_ENUM( EColorChannel );
	RTTI_ENUM_OPTION( COLCHANNEL_Red );
	RTTI_ENUM_OPTION( COLCHANNEL_Green );
	RTTI_ENUM_OPTION( COLCHANNEL_Blue );
	RTTI_ENUM_OPTION( COLCHANNEL_Alpha );
RTTI_END_ENUM()


#ifndef NO_RUNTIME_MATERIAL_COMPILATION
red::HashMap< Uint32, red::DynArray< IMaterial* > >	IMaterial::s_materialsDependencies{ rend::PoolMaterials() };	//!< Dependencies (template -> list of instances)
red::Mutex											IMaterial::s_dependenciesMutex;		//!< Mutex for accessing the structure above
#endif //NO_RUNTIME_MATERIAL_COMPILATION

IMaterial::IMaterial( const THandle< IMaterial >& baseMaterial )
	:	m_baseMaterial( baseMaterial ),
		m_proxyFailed( false ),
		m_renderResource( nullptr )
{
}

IMaterial::~IMaterial()
{
}

void IMaterial::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
#ifndef RED_CONFIGURATION_FINAL
	const CMaterialTemplate* matTemplate = Cast< const CMaterialTemplate >( GetMaterialDefinition() );
	if( matTemplate )
	{
		red::DynArray< rtti::ClassEditablePropertyInfo > existingProperties{ red::PoolRTTI() };

		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			// Scan for artist variables
			for ( auto param : matTemplate->GetParameters( static_cast< EMaterialShaderTarget >( target ) ) )
			{
				if ( param && param->GetName() && param->GetProperty() )
				{
#ifndef RED_CONFIGURATION_FINAL
					if ( param->IsHidden() )
					{
						continue;
					}
#endif // RED_CONFIGURATION_FINAL

					if (std::find_if(existingProperties.Begin(), existingProperties.End(), [&param](const rtti::ClassEditablePropertyInfo& element)
						{ return param->GetName() == element.m_name; }) != existingProperties.End())
					{
						continue;
					}

					rtti::ClassEditablePropertyInfo info;
					info.m_category = RED_NAME_CONSTEXPR( "Material" ); // class is the category name for now
					info.m_name = param->GetName();
					info.m_type = param->GetProperty()->GetType();
#ifndef RED_CONFIGURATION_FINAL
					info.m_customTypeName = RED_NAME( param->GetCustomEditorName() );
					info.m_tooltip = param->GetTooltip();
#endif // RED_CONFIGURATION_FINAL
					if( param->GetType() == IMaterialDataProviderDesc::PT_Scalar )
					{
						const CMaterialParameterScalar* scalarParam = static_cast<CMaterialParameterScalar*>( param.Get() );
						if( scalarParam->GetMin() > -RED_FLT_MAX && scalarParam->GetMax() < RED_FLT_MAX )
						{
							info.m_minValue = scalarParam->GetMin();
							info.m_maxValue = scalarParam->GetMax();
						}
					}
					existingProperties.PushBack(info);
				}
			}
		}
		outRootProperties.Add(existingProperties);
	}
#endif // RED_CONFIGURATION_FINAL
}

Bool IMaterial::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
{
	const CMaterialTemplate* matTemplate = Cast< const CMaterialTemplate >( GetMaterialDefinition() );
	if ( matTemplate )
	{
		if ( const CMaterialParameter* param = matTemplate->FindParameter( propertyName ) )
		{
			const rtti::IType* paramType = param->GetProperty()->GetType();

			red::UniqueBuffer data = red::CreateUniqueBuffer< red::PoolBackend >( paramType->GetSize(), DEFAULT_MATERIAL_PARAMETER_INSTANCE_ALIGNMENT );

			red::Memset( data.Get(), 0, paramType->GetSize() );
			paramType->Construct( data.Get() );

			Bool result = true;
			rtti::PathParser parser( restOfThePath );

			if ( !ReadParameterRaw( propertyName, paramType, data.Get() ) || !paramType->ReadValue( ctx, data.Get(), parser.GetUneatenPath(), outValue ) )
			{
				result = false;
			}

			paramType->Destruct( data.Get() );

			return result;
		}
	}

	return false;
}

Bool IMaterial::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
{
	CMaterialTemplate* matTemplate = Cast< CMaterialTemplate >( GetMaterialDefinition() );
	if ( matTemplate )
	{
		if ( const CMaterialParameter* param = matTemplate->FindParameter( propertyName ) )
		{
			const rtti::IType* paramType = param->GetProperty()->GetType();
			
			red::UniqueBuffer data = red::CreateUniqueBuffer< red::PoolBackend >( paramType->GetSize(), DEFAULT_MATERIAL_PARAMETER_INSTANCE_ALIGNMENT );

			red::Memset( data.Get(), 0, paramType->GetSize() );
			paramType->Construct( data.Get() );

			Bool result = true;
			rtti::PathParser parser( restOfThePath );

			// if we're not changing the whole property itself, but only its member (like Red in Color),
			// we should read current property value before updating its part - otherwise we'll wipe out current values of other members
			if ( restOfThePath.IsEmpty() || ReadParameterRaw( propertyName, paramType, data.Get() ) )
			{
				if ( !paramType->WriteValue( ctx, data.Get(), parser.GetUneatenPath(), *newValue, false ) || !WriteParameterRaw( propertyName, paramType, data.Get() ) )
				{
					result = false;
				}
			}

			paramType->Destruct( data.Get() );
			return result;
		}
	}

	return false;
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION

void IMaterial::ForceRecompilation( Bool createRenderResource /*= true*/ )
{
	if ( this->IsA< IMaterialDefinition >() )
	{
		red::ScopedLock< red::Mutex > lock( IMaterial::s_dependenciesMutex );
		Uint32 definitionID = GetDefinitionID();
		for ( IMaterial* mat : IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ) )
		{
			mat->ForceRecompilation( createRenderResource );
		}
	}
	
	// No render resource no need to recompile anything
	// it will recompile once rende resource gets created.
	if ( !m_renderResource )
	{
		return;
	}

	if ( createRenderResource)
	{
		// Create new rendering resource
		UpdateRenderResource();
	}
	else if ( GRender )
	{
		GRender->ForceMaterialRecompilation( GetRenderResource() );
	}
}

#endif // NO_RUNTIME_MATERIAL_COMPILATION

void IMaterial::InvalidateRenderResource()
{
	RED_SCOPE_LOCK( m_createRenderResourceLock );
	// Release rendering proxy
	m_renderResource = nullptr;
}


TRenderPtr<IRenderResource> IMaterial::GetRenderResource() const
{
	// Not created, create
	if ( !m_proxyFailed && !m_renderResource )
	{
		RED_SCOPE_LOCK(m_createRenderResourceLock);

		if ( !m_proxyFailed && !m_renderResource )
		{
			IMaterial* nonConstMaterial = const_cast<IMaterial*>(this);
			nonConstMaterial->m_renderResource = nonConstMaterial->CreateRenderResource();

			nonConstMaterial->m_proxyFailed = (nonConstMaterial->m_renderResource == nullptr);
			if (nonConstMaterial->m_proxyFailed)
			{
				RED_LOG_WARNING("Engine: Unable to create rendering proxy for material '%hs'", GetFriendlyName().AsChar());
			}
		}
	}

	// Return proxy
	return m_renderResource;
}

void IMaterial::UpdateRenderResource()
{
#if defined( VANGUARD_ASSET_HOST )
	// AssetHost loads/cooks material resources with render-data upload disabled.
	// Keep the authoritative authored/cooked material model without activating a renderer.
	return;
#else
	if ( !GRender || !GRender->GetCommands() || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_createRenderResourceLock );
	if ( m_renderResource )
	{
		const IMaterialDataProvider* dataProvider = GetMaterialDefinition();
		if ( dataProvider == nullptr )
		{
			return;
		}
		SMaterialUpdateData data;
		data.m_totalTechniquesNum	= dataProvider->GetNumTechniques();
		// Remember material friendly name
		data.m_engineMaterialName	= dataProvider->GetDisplayableName();
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		data.m_engineMaterialHash	= dataProvider->GetHash();
#endif // NO_RUNTIME_MATERIAL_COMPILATION
		data.m_materialPriority		= dataProvider->GetPriority();
		data.m_canHaveDismemberment = dataProvider->CanHaveDismemberment();
		data.m_hasDPL = dataProvider->HasDPL();

		data.m_engineStatePassDataCollectionArray.Resize( data.m_totalTechniquesNum, rend::PoolMaterialStagePassData() );
		for ( Uint32 i = 0; i < data.m_totalTechniquesNum; ++i )
		{
			dataProvider->GetPasses( i, data.m_engineStatePassDataCollectionArray[ i ] );
		}

		data.m_supportedVertexFactories = dataProvider->GetSupportedVertexFactories();

#ifndef RED_CONFIGURATION_FINAL
		data.m_isValid = dataProvider->IsValid();
#endif // RED_CONFIGURATION_FINAL

		dataProvider->GetTechniquesFeatures( data.m_techFeatures );

		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			data.m_samplerStateInfos[ target ]		= dataProvider->GetSamplerStates( (EMaterialShaderTarget)target );
			data.m_parameters[ target ]				= dataProvider->GetParameterDefinitions( (EMaterialShaderTarget)target );
			data.m_paramBlockSize[ target ]			= dataProvider->GetParamBlockSize( (EMaterialShaderTarget)target );
			MaterialHelpers::MapUsedParameters( dataProvider->GetUsedParameters( (EMaterialShaderTarget)target ), data.m_parameters[ target ], data.m_usedParameters[ target ] );
		}

		const IMaterialParametersProvider* parametersProvider = this;

		Uint32 totalSize = 0;
		red::FixedArray<Uint32, MSH_MAX> shaderSize;
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			const Uint32 dataSize = parametersProvider->GetCompiledDataBufferSize( EMaterialShaderTarget(target) );
			shaderSize[target] = dataSize;
			data.m_parametersDataShaderOffsets[target] = totalSize;
			totalSize += dataSize;
		}

		if ( totalSize > 0 )
		{
			data.m_parametersData.Resize( totalSize );

			for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
			{
				red::BlobSpan dataSpan = data.m_parametersData.Span( data.m_parametersDataShaderOffsets[target], shaderSize[target] );
				parametersProvider->CompileDataBuffer( EMaterialShaderTarget(target), dataSpan, data.m_resources, true );
			}
		}

		GRender->GetCommands()->UpdateMaterialData( m_renderResource, std::move( data ) );
	}
#endif
}

const res::ResourcePath& IMaterial::GetMaterialAssetPath() const
{
	return GetPath();
}

Uint32 IMaterial::PushResource( const IMaterialDataProvider::ParameterDefinition& param, const TRenderPtr< IRenderResource >& renderResource, red::DynArray< TRenderPtr< IRenderResource > >& resources ) const
{
	Uint32 resourceIndex = InvalidResourceIndex;
	if ( renderResource )
	{
		resources.PushBack( std::move( renderResource ) );
		resourceIndex = resources.Size() - 1;
	}
	else
	{
		RED_LOG_WARNING( "Material '%hs' has empty parameter '%hs'.", GetPath().ToDebugString(), param.m_name.AsChar() );
	}
	return resourceIndex;
}

Uint32 IMaterial::GetCompiledDataBufferSize( const EMaterialShaderTarget shaderTarget ) const
{
	const IMaterialDefinition* materialDefinition = GetMaterialDefinition();
	const Uint32 blockSize = materialDefinition->GetParamBlockSize( shaderTarget );
	return blockSize;
}

void IMaterial::CompileDataBuffer( const EMaterialShaderTarget shaderTarget, const red::BlobSpan& outData, red::DynArray< TRenderPtr< IRenderResource > >& resources, Bool preloadResources ) const
{
	const IMaterialDefinition* materialDefinition = GetMaterialDefinition();

	// Create data buffer
	const Uint32 blockSize = materialDefinition->GetParamBlockSize( shaderTarget );
	RED_ASSERT( outData.Size() >= blockSize, "Provided data buffer is not big enough for this material shader target. ShaderTarget: %u; Buffer size: %u; Required size: %u", shaderTarget, outData.Size(), blockSize );
	red::Memset( outData.Data(), 0, blockSize );

	// Compile values to single data buffer
	const IMaterialDataProvider::TParameterDefinitionArray& params = materialDefinition->GetParameterDefinitions( shaderTarget );

	for ( const IMaterialDataProvider::ParameterDefinition& param : params )
	{
		static_assert( IMaterialDataProviderDesc::PT_MAX == 17, "EParameterType changed, check here!" );
		switch ( param.m_type )
		{
			case IMaterialDataProviderDesc::PT_Texture:
			case IMaterialDataProviderDesc::PT_TextureArray:
			case IMaterialDataProviderDesc::PT_Cube:
				{
					Uint32 resourceIndex = InvalidResourceIndex;
					TRenderPtr< IRenderResource > renderTexture = nullptr;
					if ( ExtractTextureParam( param.m_name, renderTexture, preloadResources ) )
					{
						resourceIndex = PushResource( param, renderTexture, resources );
					}
					outData.As<Uint32>(param.m_offset) = resourceIndex;
				}
				break;
			case IMaterialDataProviderDesc::PT_StructBuffer:
				{
					// We're not doing anything. This param cannot be set from properties - at least for now.
					outData.As<Uint32>(param.m_offset) = InvalidResourceIndex;
					break;
				}
			case IMaterialDataProviderDesc::PT_SkinProfile:
				{
					Uint32 resourceIndex = InvalidResourceIndex;
					TRenderPtr< IRenderResource > renderSkinProfile;
					if ( ExtractSkinProfileParam( param.m_name, renderSkinProfile, preloadResources ) )
					{
						resourceIndex = PushResource( param, renderSkinProfile, resources );
					}
					outData.As<Uint32>(param.m_offset) = resourceIndex;
					break;
				}
			case IMaterialDataProviderDesc::PT_HairProfile:
				{
					Uint32 resourceIndex = InvalidResourceIndex;
					TRenderPtr< IRenderResource > renderHairProfile;
					if ( ExtractHairProfileParam( param.m_name, renderHairProfile, preloadResources ) )
					{
						resourceIndex = PushResource( param, renderHairProfile, resources );
					}
					outData.As<Uint32>(param.m_offset) = resourceIndex;
					break;
				}
			case IMaterialDataProviderDesc::PT_Gradient:
			{
				Uint32 resourceIndex = InvalidResourceIndex;
				TRenderPtr< IRenderResource > gradientResource;
				if (ExtractGradientParam(param.m_name, gradientResource, preloadResources))
				{
					resourceIndex = PushResource(param, gradientResource, resources);
				}
				outData.As<Uint32>(param.m_offset) = resourceIndex;
				break;
			}
			case IMaterialDataProviderDesc::PT_FoliageProfile:
			{
				Uint32 resourceIndex = InvalidResourceIndex;
				TRenderPtr< IRenderResource > renderFoliageProfile;
				if (ExtractFoliageProfileParam(param.m_name, renderFoliageProfile, preloadResources))
				{
					resourceIndex = PushResource(param, renderFoliageProfile, resources);
				}
				outData.As<Uint32>(param.m_offset) = resourceIndex;
				break;
			}

			case IMaterialDataProviderDesc::PT_MultilayerSetup:
			case IMaterialDataProviderDesc::PT_MultilayerMask:
			case IMaterialDataProviderDesc::PT_TerrainSetup:
				{
					// These can be ignored. They aren't used directly by any rendering, and should be treated specially
					// within the renderer.
					// #TODO: Maybe for simplicity we should just pack them into the data buffer anyways here? Not sure
					// if it would be fine or might cause some sort of problem.
				} break;

			case IMaterialDataProviderDesc::PT_Cpu_NameU64:
				{
					CName name;
					ReadParameter(param.m_name, name);
	
					outData.As<Uint64>(param.m_offset) = rend::CalculateNameU64(name);
					break;
				}

			case IMaterialDataProviderDesc::PT_Color:
				{
					Color value;
					ReadParameter(param.m_name, value);
					outData.As<Color>(param.m_offset) = value;
					break;
				}
			case IMaterialDataProviderDesc::PT_Vector:
				{
					Vector4 value;
					ReadParameter(param.m_name, value);

					// Can't use Vector4, because it might not be aligned.
					using Float4 = Float[4];
					Float4& destPtr = outData.As<Float4>(param.m_offset);
					destPtr[0] = value.X;
					destPtr[1] = value.Y;
					destPtr[2] = value.Z;
					destPtr[3] = value.W;
					break;
				}
			case IMaterialDataProviderDesc::PT_Scalar:
				{
					Float value;
					ReadParameter(param.m_name, value);
					outData.As<Float>(param.m_offset) = value;
					break;
				}
			case IMaterialDataProviderDesc::PT_Bool:
				{
					/*
					Bool value;
					ReadParameter(param.m_name, value);
					Bool* destPtr = reinterpret_cast<Bool*>(&data[param.m_offset]);
					*destPtr = value;
					*/
					RED_FATAL("PT_Bool not implemented!");
					break;
				}

			default:
				RED_LOG_ERROR("Engine: Unknown parameter type %d. Skipping", param.m_type);
				break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// #TODO: For these, should make sure that the parameter types match.

Bool IMaterial::ExtractTextureParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
	{
		TResRef< ITexture > texture;
		if ( ReadParameter( name, texture ) )
		{
			if ( texture )
			{
				outValue = texture->GetRenderResource();
			}
			else
			{
				if ( preloadResources && !texture.Empty() )
				{
					if ( THandle< ITexture > texHandle = texture.Get() )
					{
						outValue = texHandle->GetRenderResource();
					}
					else
					{
						return false;
					}
				}
			}
			return true;
		}
	}
	{
		THandle<DynamicTexture> texture;
		if ( ReadParameter( name, texture ) )
		{
			if ( texture )
			{
				outValue = texture->GetRenderResource();
				return true;
			}
		}
	}
	return false;
}

Bool IMaterial::ExtractBufferParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
	// Can only be set by renderer internally, so can't do anything here.
	return false;
}

Bool IMaterial::ExtractScalarParam( CName name, Float& outValue ) const
{
	return ReadParameter( name, outValue );
}

Bool IMaterial::ExtractVectorParam( CName name, Vector4& outValue ) const
{
	return ReadParameter( name, outValue );
}

Bool IMaterial::ExtractColorParam( CName name, Color& outValue ) const
{
	return ReadParameter( name, outValue );
}

Bool IMaterial::ExtractNameU64Param( CName name, Uint64& outValue ) const
{
	CName nameValue;
	if ( ReadParameter( name, nameValue ) )
	{
		if ( nameValue != CName::NONE() )
		{
			outValue = rend::CalculateNameU64(nameValue);
		}
		return true;
	}
	return false;
}

Bool IMaterial::ExtractSkinProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< CSkinProfile > skinProfile;
	if ( ReadParameter( name, skinProfile ) )
	{
		if ( preloadResources )
		{
			skinProfile.Get();
		}
		outValue = skinProfile ? skinProfile->GetRenderResource() : nullptr;
		return true;
	}
	return false;
#endif
}

Bool IMaterial::ExtractHairProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< CHairProfile > hairProfile;
	if ( ReadParameter( name, hairProfile ) )
	{
		if ( !hairProfile.Empty() )
		{
			if ( preloadResources )
			{
				hairProfile.Get();
			}
			outValue = hairProfile ? hairProfile->GetRenderResource() : nullptr;
			return true;
		}
	}
	return false;
#endif
}

Bool IMaterial::ExtractFoliageProfileParam(CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< CFoliageProfile > foliageProfile;
	if (ReadParameter(name, foliageProfile))
	{
		if (!foliageProfile.Empty())
		{
			if (preloadResources)
			{
				foliageProfile.Get();
			}
			outValue = foliageProfile ? foliageProfile->GetRenderResource() : nullptr;
			return true;
		}
	}
	return false;
#endif
}

Bool IMaterial::ExtractGradientParam(CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< CGradient > gradient;
	if ( ReadParameter( name, gradient ) )
	{
		if ( !gradient.Empty() )
		{
			if ( preloadResources )
			{
				gradient.Get();
			}
			outValue = gradient ? gradient->GetRenderResource() : nullptr;
			return true;
		}
	}
	return false;
#endif
}

Bool IMaterial::ExtractMultilayerSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< Multilayer_Setup > setup;
	if ( ReadParameter( name, setup ) )
	{
		if ( preloadResources )
		{
			setup.Get();
		}
		if ( setup )
		{
			outValue = setup->GetRenderResource();
		}
		return true;
	}
	return false;
#endif
}

Bool IMaterial::ExtractMultilayerMaskParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< Multilayer_Mask > mask;
	if ( ReadParameter( name, mask ) )
	{
		if ( preloadResources )
		{
			mask.Get();
		}

		if ( mask )
		{
			outValue = mask->GetRenderResource();
		}
		return true;
	}
	return false;
#endif
}

Bool IMaterial::ExtractTerrainSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const
{
#if defined( VANGUARD_ASSET_HOST )
	return false;
#else
	TResRef< CTerrainSetup > setup;
	if( ReadParameter( name, setup ) )
	{
		if( preloadResources )
		{
			setup.Get();
		}

		if( setup )
		{
			outValue = setup->GetRenderResource();
		}
		return true;
	}
	return false;
#endif
}


Bool IMaterial::AddDependency( IMaterial* material )
{
#ifdef NO_RUNTIME_MATERIAL_COMPILATION
	return true;
#else

	if ( !material->IsA< CMaterialInstance >() )
	{
		return true;
	}

	red::ScopedLock< red::Mutex > lock( IMaterial::s_dependenciesMutex );
	Uint32 definitionID = material->GetDefinitionID();
	Bool result = false;
	if ( IMaterial::s_materialsDependencies.KeyExist( definitionID ) )
	{
		const Bool skipMaterial = ( definitionID == 0 ) && IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ).FindPtr( material );
		if ( !skipMaterial )
		{
			RED_ASSERT( !IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ).FindPtr( material ), "Adding duplicate MaterialTemplate -> MaterialInstance dependency." );
			IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ).PushBack( material );
		}
		
		result = true;
	}
	else
	{
		red::DynArray< IMaterial* > arr( { material }, rend::PoolMaterials() );
		result = IMaterial::s_materialsDependencies.Insert( definitionID, arr ).IsSuccessful();
	}
	return result;
#endif // NO_RUNTIME_MATERIAL_COMPILATION
}

Bool IMaterial::RemoveDependency( IMaterial* material )
{
#ifdef NO_RUNTIME_MATERIAL_COMPILATION
	return true;
#else

	if ( !material->IsA< CMaterialInstance >() )
	{
		return true;
	}

	if ( material->m_baseMaterial.Empty() )
	{
		return true;
	}

	red::ScopedLock< red::Mutex > lock( IMaterial::s_dependenciesMutex );
	Bool result = false;
	Uint32 definitionID = material->GetDefinitionID();
	if ( IMaterial::s_materialsDependencies.KeyExist( definitionID ) )
	{
		result = IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ).Remove( material ).IsSuccessful();
		if ( IMaterial::s_materialsDependencies.GetRef( definitionID, rend::PoolMaterials() ).Empty() )
		{
			result &= IMaterial::s_materialsDependencies.Remove( definitionID ).IsSuccessful();
		}
	}
	return result;
#endif // NO_RUNTIME_MATERIAL_COMPILATION
}

void IMaterial::InitDependenciesMemoryPools()
{
#if !defined( NO_RUNTIME_MATERIAL_COMPILATION )
	s_materialsDependencies.SetPool( rend::PoolMaterials() );
#endif
}
