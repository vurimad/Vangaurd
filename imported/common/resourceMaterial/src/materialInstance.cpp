/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "bitmapTexture.h"
#include "cubeTexture.h"
#include "materialInstance.h"
#include "materialParameter.h"
#include "materialTemplate.h"
#include "texture.h"
#include "textureArray.h"
#include "multilayerSetup.h"
#include "multilayerMask.h"
#include "terrainSetup.h"

#include "../../renderData/include/meshEnum.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif
#include "../../renderData/include/renderResource.h"

#include "../../resource/include/2darray.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../core/include/debugPageHTMLDoc.h"
#endif
#include "../../redFileSystem/include/fileSkipableBlock.h"
#include "../../redReflection/include/gatheredResource.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../redJobs2/include/jobBuilder.h"

//*********************************************************************************************************************************
//*********************************************************************************************************************************

namespace Version
{
	enum Enum
	{
		BASE,
		EMISSIVE_RESAVE,
		EMISSIVE_RESAVE2,
		DECAL_MATERIALS_EMISSIVE_RESAVE,
		MASK_DISABLED_ON_GLASS_BASED_INSTANCE,

		NEWEST = MASK_DISABLED_ON_GLASS_BASED_INSTANCE
	};
};

RTTI_BEGIN_TYPE( CMaterialInstance );

	RTTI_PARENT_TYPE( IMaterial );
	RTTI_PROPERTY_CATEGORY( "BaseMaterial" );
	RTTI_PROPERTY( m_baseMaterial ).editable().customEditor( "baseMaterialRef" );

	// If this property's name is changed, make sure to update editor/materialInstanceGroupItem.cpp as well!
	RTTI_PROPERTY( m_enableMask ).editable();

	RTTI_PROPERTY( m_audioTag ).editable().tooltip( "Should be set only on decal materials that affect the footsteps sounds, keep it empty otherwise" );

	RTTI_PROPERTY( m_resourceVersion );

RTTI_END_TYPE();


//*********************************************************************************************************************************
//*********************************************************************************************************************************

CMaterialInstance::CMaterialInstance()
	: IMaterial( nullptr )
	, m_parameters{ rend::PoolMaterials() }
	, m_enableMask( false )
	, m_definitionID( 0 )
{
}

CMaterialInstance::CMaterialInstance( ISerializable* parent, const THandle< IMaterial >& material )
	: IMaterial( material )
	, m_parameters{ rend::PoolMaterials() }
	, m_enableMask( false )
{
	CacheDefinitionID();
	RED_VERIFY( IMaterial::AddDependency( this ) );
	SetParent( parent );
}

CMaterialInstance::~CMaterialInstance()
{
	IMaterial::RemoveDependency( this );
}

Bool DoParamTypesMatch( IMaterialDataProviderDesc::EParameterType definitionType, CName existingParamTypeName )
{
	/*
	static CName constNamesForTypes[ IMaterialDataProviderDesc::PT_MAX ] =
	{
	RED_NAME_CONSTEXPR(""),								//PT_None
	RED_NAME_CONSTEXPR("rRef:ITexture"),					//PT_Texture
	RED_NAME_CONSTEXPR("Color"),							//PT_Color
	RED_NAME_CONSTEXPR("rRef:ITexture"),					//PT_Cube
	RED_NAME_CONSTEXPR("Vector4"),						//PT_Vector
	RED_NAME_CONSTEXPR("Float"),							//PT_Scalar
	RED_NAME_CONSTEXPR(""),					// none		//PT_Bool
	RED_NAME_CONSTEXPR(""),					// none		//PT_TextureArray
	RED_NAME_CONSTEXPR(""),					// none		//PT_StructBuffer
	RED_NAME_CONSTEXPR("CName"),							//PT_Cpu_NameU64
	RED_NAME_CONSTEXPR("rRef:CSkinProfile"),				//PT_SkinProfile
	RED_NAME_CONSTEXPR("rRef:Multilayer_Setup"),			//PT_MultilayerSetup
	RED_NAME_CONSTEXPR("rRef:Multilayer_Mask"),			//PT_MultilayerMask
	RED_NAME_CONSTEXPR("rRef:CHairProfile"),				//PT_HairProfile
	RED_NAME_CONSTEXPR("rRef:CFoliageProfile"),			//PT_FoliageProfile
	RED_NAME_CONSTEXPR("rRef:CTerrainSetup"),				//PT_TerrainSetup
	RED_NAME_CONSTEXPR("rRef:CGradient"),					//PT_Gradient
	};
	*/
	static_assert( IMaterialDataProviderDesc::PT_MAX == 17, "EParameterType changed, check here!" );
	static CName namesForTypes[ IMaterialDataProviderDesc::PT_MAX ] =
	{
		/*PT_None*/				CName::NONE(),
		/*PT_Texture*/			::GetTypeName< TResRef< ITexture > >(),
		/*PT_Color*/			::GetTypeName< Color >(),
		/*PT_Cube*/				::GetTypeName< TResRef< ITexture > >(),
		/*PT_Vector*/			::GetTypeName< Vector4 >(),
		/*PT_Scalar*/			::GetTypeName< Float >(),
		/*PT_Bool*/				CName::NONE(),					// none
		/*PT_TextureArray*/		CName::NONE(),					// none
		/*PT_StructBuffer*/		CName::NONE(),					// none
		/*PT_Cpu_NameU64*/		::GetTypeName< CName >(),
#if defined( VANGUARD_ASSET_HOST )
		/*PT_SkinProfile*/		CName::NONE(),
		/*PT_MultilayerSetup*/	CName::NONE(),
		/*PT_MultilayerMask*/	CName::NONE(),
		/*PT_HairProfile*/		CName::NONE(),
		/*PT_FoliageProfile*/	CName::NONE(),
		/*PT_TerrainSetup*/		CName::NONE(),
		/*PT_Gradient*/			CName::NONE(),
#else
		/*PT_SkinProfile*/		::GetTypeName< TResRef< CSkinProfile > >(),
		/*PT_MultilayerSetup*/	::GetTypeName< TResRef< Multilayer_Setup > >(),
		/*PT_MultilayerMask*/	::GetTypeName< TResRef< Multilayer_Mask > >(),
		/*PT_HairProfile*/		::GetTypeName< TResRef< CHairProfile > >(),
		/*PT_FoliageProfile*/	::GetTypeName< TResRef< CFoliageProfile > >(),
		/*PT_TerrainSetup*/		::GetTypeName< TResRef< CTerrainSetup > >(),
		/*PT_Gradient*/			::GetTypeName< TResRef< CGradient > >(),
#endif
	};
	return namesForTypes[ definitionType ] == existingParamTypeName;
}
void CMaterialInstance::RemoveUnusedParameters()
{
	if ( m_parameters.Empty() )
	{
		return;
	}

	/*
	Some parameters overriden by this material instance can no longer exist in the template that they use, eg. parameter was renamed or removed in the original MaterialTemplate fxmeta.
	To avoid serializing unused data, all parameters are checked, their name and type against the parameter definitions from the template.
	*/
	if ( IMaterialDefinition* def = GetMaterialDefinition() )
	{
		const auto& vertexParams = def->GetParameterDefinitions( MSH_VertexShader );
		const auto& pixelParams = def->GetParameterDefinitions( MSH_PixelShader );
		for ( Int32 i = m_parameters.Size() - 1; i >= 0; --i )
		{
			const auto& param = m_parameters[ i ];
			const CName paramName = param.GetName();
			const CName paramTypeName = param.GetType()->GetName();
			Bool foundDefinition = false;
			for ( const auto& defParam : vertexParams )
			{
				if ( defParam.m_name == paramName && DoParamTypesMatch( defParam.m_type, paramTypeName ) )
				{
					foundDefinition = true;
					break;
				}
			}
			if ( !foundDefinition )
			{
				for ( const auto& defParam : pixelParams )
				{
					if ( defParam.m_name == paramName && DoParamTypesMatch( defParam.m_type, paramTypeName ) )
					{
						foundDefinition = true;
						break;
					}
				}
			}
			if ( !foundDefinition )
			{
				RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Rendering, "MaterialInstance '%hs' had unused parameter '%s' of type '%hs'. Removing...", GetFriendlyName().AsChar(), paramName.AsChar(), paramTypeName.AsChar() );
				m_parameters.RemoveAt( i );
			}
		}
	}
}

void CMaterialInstance::OnPreSave( const PreSaveContext& context )
{
	m_resourceVersion = Version::NEWEST;

	TBaseClass::OnPreSave( context );
}

void HandleGlassMaskUpdate( THandle< CResource > res, WeakHandle< CMaterialInstance > weakSelf, const job::RunContext& ctx )
{
	if ( THandle< IMaterial > baseMat = Cast< IMaterial >( res ) )
	{
		if ( THandle< IMaterialDefinition > def = Cast< IMaterialDefinition >( baseMat ) )
		{
			const auto& self = weakSelf.ToHandle();
			if ( self && def->IsGlass() )
			{
				self->SetMaskEnabled( false );
			}
		}
		else
		{
			const res::ResourcePath basePath = baseMat->GetBaseMaterialPath();
			res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( basePath );

			job::Builder builder( ctx );
			builder.DispatchWait( token->GetWaitCounter() );
			builder.DispatchJob( "MaterialInstance/EnsureGlassMaskUpdated", [token, weakSelf]( const job::RunContext& context )
			{
				const auto& self = weakSelf.ToHandle();
				if ( self && token->IsLoaded() )
				{
					HandleGlassMaskUpdate( token->GetResource(), weakSelf, context );
				}
			} );
		}
	}
};

void CMaterialInstance::EnsureGlassMaskUpdated( job::Builder& builder )
{
	if ( m_resourceVersion < Version::MASK_DISABLED_ON_GLASS_BASED_INSTANCE )
	{
		const res::ResourcePath basePath = m_baseMaterial.GetPath();
		res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( basePath );

		builder.DispatchWait( token->GetWaitCounter() );
		builder.DispatchJob( "MaterialInstance/EnsureGlassMaskUpdated", [token, weakSelf = WeakHandleFromThis< CMaterialInstance >()] ( const job::RunContext& context )
		{
			const auto& self = weakSelf.ToHandle();
			if ( self && token->IsLoaded() )
			{
				HandleGlassMaskUpdate( token->GetResource(), weakSelf, context );
			}
		} );
	}
}


void CMaterialInstance::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	if ( m_resourceVersion < Version::EMISSIVE_RESAVE2 )
	{
		Bool foundEV = false;

		// swap parameters when new ones need to be resaved
		for( Int32 i = m_parameters.Size() - 1; i >= 0; --i )
		{
			auto& param = m_parameters[ i ];
			if ( param.GetName() == RED_NAME_CONSTEXPR_NOREG( "EmissiveBrightness" ) && param.GetType() == ::GetTypeObject<Float>() )
			{
				if ( foundEV )
				{
					m_parameters.RemoveAt( i );
				}
				else
				{
					param.SetName( RED_NAME_CONSTEXPR( "EmissiveEV" ) );

					Float* propData = (Float*)param.GetData();
					*propData = Clamp( std::log2( *propData ) * (10 / 16.0f), 0.0f, 10.0f );
				}
				foundEV = true;
			}
			else
			{
				// Put some new material names here if needed
			}
		}
	}
	if ( m_resourceVersion < Version::DECAL_MATERIALS_EMISSIVE_RESAVE )
	{
		// swap parameters when new ones need to be resaved
		for ( Int32 i = m_parameters.Size() - 1; i >= 0; --i )
		{
			auto& param = m_parameters[ i ];
			if ( param.GetName() == RED_NAME_CONSTEXPR_NOREG( "EmissiveIntensity" ) && param.GetType() == ::GetTypeObject<Float>() )
			{
				Float* propData = (Float*)param.GetData();
				Float newValue = Clamp( std::log2( *propData ) * ( 10.f / 16.0f ), 0.0f, 10.0f );

				m_parameters.EmplaceBack( RED_NAME_CONSTEXPR( "EmissiveEV" ), ::GetTypeName< Float >(), &newValue );
			}
			else
			{
				// Put some new material names here if needed
			}
		}
	}

#ifndef RED_CONFIGURATION_FINAL
	job::Builder builder;
	EnsureGlassMaskUpdated( builder );
#endif
	
	CacheDefinitionID();
	RED_VERIFY( IMaterial::AddDependency( this ) );

#ifndef RED_CONFIGURATION_FINAL
	RemoveUnusedParameters();

	if ( !GetMaterialDefinition() ) // Test for missing external material, use some fallback when missing.
	{
		String pathDesc;
		res::ResourcePath path = GetPath();
		if( path.IsEmpty() )
		{
			Uint32 dependencyCount = 0;
			ISerializable* parent = this;
			do
			{
				parent = parent->GetParent();
				if( parent )
				{
					path = parent->GetPath();
				}
			} while( path.IsEmpty() && parent && dependencyCount++ < 10 );

			pathDesc = path.IsEmpty()
				? "<Could not establish resource or parent resource, inform rendering team>"
				: String::Printf( "Local instance in: %hs", path.ToDebugString() );
		}
		else
		{
			pathDesc = path.ToString();
		}

		RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, false, "MaterialInstance has no base material\r\n"
							  "MaterialInstance '%hs'\r\nBaseMaterial: '%hs'", pathDesc.AsChar(), m_baseMaterial.GetPath().ToDebugString() );
		RED_DATA_ERROR_ACTION_SHOW_ASSET( path.ToDebugString() );
		RED_DATA_ERROR_END();
	}
#endif // RED_CONFIGURATION_FINAL
}

void CMaterialInstance::OnSerialize( IFile& file )
{
	// Serialize base class
	TBaseClass::OnSerialize( file );

	// Save parameters
	if ( file.IsWriter() )
	{
		// Write number of parameters
		Uint32 count = m_parameters.Size();
		file << count;

		// Save each parameter
		for ( MaterialParameterInstance& param : m_parameters )
		{
			// Save param
			{
				fs::SkipableBlock block( file );

				// Save parameter name
				CName paramName = param.GetName();
				file << paramName;

				// Save parameter type
				CName typeName = param.GetType()->GetName();
				file << typeName;

				// Save parameter data
				param.SerializeDirectly( file );
			}
		}
	}

	// Load parameters
	else if ( file.IsReader() )
	{
		// Load number of parameters
		Uint32 numParams = 0;
		file << numParams;

		// Clear current parameter list and reserve space for params
		m_parameters.Clear();
		m_parameters.Reserve( numParams );

		// Load parameters
		for ( Uint32 i = 0; i < numParams; i++ )
		{
			fs::SkipableBlock block( file );

			if ( file.IsCooker() )
			{
				// Cooker will not load the imports, but the path should still be correct
				if ( !m_baseMaterial.GetPath().IsValid() )
				{
					block.Skip();
					continue;
				}
			}
			
			// Load param name
			CName paramName;
			file << paramName;

			// Load parameter type
			CName paramType;
			file << paramType;

			// Create parameter data
			MaterialParameterInstance& param = m_parameters.EmplaceBack( paramName, paramType );
			if ( !param.SerializeDirectly( file ) )
			{
				m_parameters.PopBack();
				block.Skip();
			}

			// Convert
			{
				const MaterialParameterInstance& lastParam = m_parameters.Back();

				// Convert old, non handle values
				if ( lastParam.GetType() == GetTypeObject< ITexture* >() )
				{
					const THandle< ITexture > value( *(ITexture**)lastParam.GetData() );

					m_parameters.PopBack();

					m_parameters.EmplaceBack( paramName, ::GetTypeName< THandle< ITexture > >(), &value );
				}
#if !defined( VANGUARD_ASSET_HOST )
					else if ( lastParam.GetType() == GetTypeObject< CTextureArray* >() )
					{
					const THandle< CTextureArray > value( *(CTextureArray**)lastParam.GetData() );

					m_parameters.PopBack();

					m_parameters.EmplaceBack( paramName, ::GetTypeName< THandle< CTextureArray > >(), &value );
				}
				else if ( lastParam.GetType() == GetTypeObject< CCubeTexture* >() )
				{
					const THandle< CCubeTexture > value( *(CCubeTexture**)lastParam.GetData() );

					m_parameters.PopBack();

						m_parameters.EmplaceBack( paramName, ::GetTypeName< THandle< CCubeTexture > >(), &value );
					}
#endif
					else if ( lastParam.GetType() == GetTypeObject< THandle< ITexture > >() )
				{
					const THandle< ITexture > value = *( THandle< ITexture >* )lastParam.GetData();

					m_parameters.PopBack();

					TResRef< ITexture > newValue( value );

					m_parameters.EmplaceBack( paramName, ::GetTypeName< TResRef< ITexture > >(), &newValue );
				}
				else if ( lastParam.GetType()->GetType() == RT_Pointer )
				{
					RED_LOG_ERROR( "Pointer types are not supported inside CMaterialInstance. Parameter '%hs', type '%hs'",
						lastParam.GetName().AsChar(), lastParam.GetType()->GetName().AsChar() );
				}
			}
		}
	}
}

void CMaterialInstance::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
	TBaseClass::GetCustomEditableProperties( outRootProperties );

	Bool isGlass = false;
	if ( const IMaterialDefinition* def = GetMaterialDefinition() )
	{
		isGlass = def->IsGlass();
	}
	outRootProperties.RemoveIf( [ this, isGlass ]( const rtti::ClassEditablePropertyInfo& info )
	{
		if ( info.m_name == RED_NAME_CONSTEXPR_NOREG( "enableMask" ) && isGlass )
		{
			return true;
		}
		return false;
	} );
}

Bool CMaterialInstance::OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue )
{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	if ( path.ToString() == "baseMaterial" )
	{
		RED_VERIFY( IMaterial::RemoveDependency( this ) );
	}
#endif

	//texture group validation
	if ( inOutValue->IsSingle() )
	{
		// Acquire base material.
		if ( const auto materialDefinition = GetMaterialDefinition() )
		{
			if ( const CMaterialTemplate* materialTemplate = SafeCast< CMaterialTemplate >( materialDefinition ) )
			{
				if ( const CMaterialParameter* param = materialTemplate->FindParameter( RED_NAME( path.ToString() ) ) )
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
		}
	}

	// Pass to base class
	return ISerializable::OnPropertyPreChange( path, inOutValue );
}

void CMaterialInstance::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	if ( path.ToString() == "baseMaterial" )
	{
		TMaterialParameterInstances	previousParameters = m_parameters;
		m_parameters.Clear();
		CacheDefinitionID();
		RED_VERIFY( IMaterial::AddDependency( this ) );

		for (auto& prevParam : previousParameters)
		{
			WriteParameterRaw( prevParam.GetName(), prevParam.GetType(), prevParam.GetData(), false );
		}

		if ( m_baseMaterial && m_baseMaterial.IsValid() && m_baseMaterial->IsA< CMaterialInstance >() )
		{
			m_enableMask = m_baseMaterial->IsMasked();
		}

		RecreateParentResource();
	}
	else if ( path.ToString() == "enableMask" )
	{
		RecreateParentResource();
	}
#endif

	if ( !m_parameters.Empty() && ( value->IsEmpty() || ( value->IsSingle() && value->GetSingleValue().EqualsNC( "null" ) ) ) )
	{
		Bool shouldRecreate = false;
		CName paramName = RED_NAME_NOREG( path.ToString() );
		for ( Int32 i = m_parameters.Size() - 1; i >= 0; --i )
		{
			if ( m_parameters[ i ].GetName() == paramName )
			{
				m_parameters.RemoveAt( i );
				shouldRecreate = true;
				break;
			}
		}
		if ( shouldRecreate )
		{
			RecreateParentResource();
		}
	}
	
	// Pass to base class
	ISerializable::OnPropertyPostChange( path, oldValue, value );
}

void CMaterialInstance::SetBaseMaterial( const THandle< IMaterial >& material )
{
	RED_VERIFY( IMaterial::RemoveDependency( this ) );

	// Reset
	m_baseMaterial = material;

	CacheDefinitionID();

	RED_VERIFY( IMaterial::AddDependency( this ) );

	RecreateParentResource();
}

void CMaterialInstance::ClearInstanceParameters()
{
	// Reset
	m_parameters.Clear();
	RecreateParentResource();
}

Bool CMaterialInstance::ClearInstanceParameter( const CName& name )
{
	for ( Uint32 i = 0; i < m_parameters.Size(); ++i )
	{
		if ( m_parameters[i].GetName() == name )
		{
			m_parameters.RemoveAt( i );
			return true;
		}
	}
	return false;
}

Bool CMaterialInstance::IsParameterInstanced( const CName& name ) const
{
	for ( const MaterialParameterInstance& param : m_parameters )
	{
		if ( param.GetName() == name )
		{
			return true;
		}
	}

	return false;
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
Bool CMaterialInstance::Recompile( const Bool includesMatch, const Bool onlyHighPriority )
{
	if ( IMaterialDefinition* baseMaterial = GetMaterialDefinition() )
	{
		return baseMaterial->Recompile( includesMatch, onlyHighPriority );
	}
	return false;
}
#endif

// todo:
void CMaterialInstance::AutoAssignTextures()
{
	// TODO Another logic that should be moved to resourceBackend
	#if 0
	// Auto-assign default textures
	CMaterialGraph* graph = Cast< CMaterialGraph >( GetMaterialDefinition() );
	if( !graph )
	{
		return;
	}

	// No mesh
	CMesh *mesh = Cast<CMesh>( GetParent() );
	if( !mesh )
	{
		return;
	}

	red::String separator = "_";

	red::String meshDir = mesh->GetFile()->GetDirectory()->GetDepotPath();
	red::String meshName = mesh->GetFile()->GetFileName().StringBeforeFromRight( TXT( "." ) );
	red::String modelInfo;
	red::String modelName;

	Uint32 sepsepPos;
	if( meshName.FindSubstring( TXT( "__" ), sepsepPos, true ) && sepsepPos > 0 )
	{
		modelName = meshName.LeftString( sepsepPos );
		modelInfo = meshName.MidString( sepsepPos + 2 );
	}
	if( modelName.Empty() )
		modelName = meshName;

	String bodyPart = modelInfo.StringAfterFromRight( "_" );
	if( !bodyPart.Empty() )
		bodyPart = separator + bodyPart;

	// Load info about texture parameters auto binding
	C2dArray *autoBindConf = resTextureAutoBindDef.LoadAndGet< C2dArray >();
	if( !autoBindConf )
		return;

	Uint32 n2daCols, n2daRows, nParamNameCol, nSuffixCol = 0;
	autoBindConf->GetSize( n2daCols, n2daRows );

	// Check if expected columns exist in the configuration file
	if( !autoBindConf->FindHeader( TXT( "ParamName" ), nParamNameCol ) ||
		!autoBindConf->FindHeader( TXT( "Suffix" ), nSuffixCol ) )
	{
		autoBindConf->Discard();
		return;
	}

	for( Uint32 iRow = 0; iRow < n2daRows; ++iRow )
	{
		String rowValue = autoBindConf->GetValue( nParamNameCol, iRow );
		CName paramName( rowValue );
		if( !paramName ) continue;

		CMaterialParameter* param = graph->FindParameter( paramName, false );
		if( !param )
			continue;

		red::String sufix = autoBindConf->GetValue( nSuffixCol, iRow );
		sufix = sufix.Empty() ? sufix : separator + sufix;

		// Look for the texture with body part name
		red::String fileName = sufix.Empty() && bodyPart.Empty()
			? meshDir + modelName + TXT( ".xbm" )
			: meshDir + modelName + separator + bodyPart + sufix + TXT( ".xbm" );
		res::ResourceProxy* file = GDepot->FindFile( fileName );

		// Look for the texture without body part name
		if( !file && !bodyPart.Empty() )
		{
			fileName = sufix.Empty()
				? meshDir + modelName + TXT( ".xbm" )
				: meshDir + modelName + separator + sufix + TXT( ".xbm" );
			file = GDepot->FindFile( fileName );
		}
		if( !file ) continue;

		// Bind the texture if it has been found
		CPropertyDataBuffer buffer( param->GetParameterProperty() );
		if( param->GetParameterProperty()->GetType()->FromString( buffer.Data(), fileName ) )
			WriteParameterRaw( param->GetParameterName(), buffer.Data() );
	}
	#endif
}

const IMaterialDefinition* CMaterialInstance::GetMaterialDefinition() const
{
	const IMaterial* material = m_baseMaterial.Get().Get();
	while ( material && !material->IsA< IMaterialDefinition >() )
	{
		material = material->GetBaseMaterial();
	}
	RED_ASSERT( !material || material->IsA< IMaterialDefinition >() );
	return static_cast< const IMaterialDefinition* >( material );
}

IMaterialDefinition* CMaterialInstance::GetMaterialDefinition()
{
	IMaterial* material = m_baseMaterial.Get().Get();
	while ( material && !material->IsA< IMaterialDefinition >() )
	{
		material = material->GetBaseMaterial();
	}
	RED_ASSERT( !material || material->IsA< IMaterialDefinition >() );
	return static_cast< IMaterialDefinition* >( material );
}

Bool CMaterialInstance::WriteParameterRaw( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource /*= true*/ )
{
	// Write to existing parameter
	for ( MaterialParameterInstance& info : m_parameters )
	{
		if ( info.GetName() == name )
		{
			info.SetData( dataType, data );
			if ( recreateResource )
			{
				RecreateParentResource();
			}
			return true;
		}
	}

	if ( IMaterialDefinition* definition = GetMaterialDefinition() )
	{
		RED_ASSERT( definition->IsA< CMaterialTemplate >(), "Must be a Material Template" );
		CMaterialTemplate* materialTemplate = SafeCast< CMaterialTemplate >( definition );
		if ( const CMaterialParameter* param = materialTemplate->FindParameter( name ) )
		{
			Bool typeMatch = param->GetProperty()->GetType() == dataType;

			// Check that the new data is a supported type for this parameter?
			if (typeMatch)
			{
				// Create instanced parameter
				m_parameters.EmplaceBack(name, dataType, data);
				if (recreateResource)
				{
					RecreateParentResource();
				}
				return true;
			}
		}
	}

	// Unknown parameter or parameter type mismatch
	return false;
}

Bool CMaterialInstance::WriteParameterRaw_NoDefinitionCheck( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource /*= true*/ )
{
	// Write to existing parameter
	for ( MaterialParameterInstance& info : m_parameters )
	{
		if ( info.GetName() == name )
		{
			info.SetData( dataType, data );
			if ( recreateResource )
			{
				RecreateParentResource();
			}
			return true;
		}
	}

	// Create instanced parameter
	m_parameters.EmplaceBack( name, dataType, data );
	if ( recreateResource )
	{
		RecreateParentResource();
	}
	return true;
}

Bool CMaterialInstance::ReadParameterRaw( const CName& name, const rtti::IType* dataType, void* data ) const
{
	// Check for value in this instance
	for ( const MaterialParameterInstance& info : m_parameters )
	{
		if ( info.GetName() == name )
		{
			if ( info.GetType() == dataType )
			{
				info.GetType()->Copy( data, info.GetData() );
				return true;
			}
			else
			{
				// Have matching name, but incorrect type. Don't pass to base, in case the parameter has been replaced
				// by something with a different type.
				// #TODO: Allow some limited conversion of types?
				return false;
			}
		}
	}

	// Read from base material
	if ( m_baseMaterial )
	{
		return m_baseMaterial->ReadParameterRaw( name, dataType, data );
	}

	// Invalid
	return false;
}

Bool CMaterialInstance::ReadParameterRaw_NoBase(const CName& name, const rtti::IType* dataType, void* data) const
{
	// Check for value in this instance
	for (const MaterialParameterInstance& info : m_parameters)
	{
		if (info.GetName() == name)
		{
			if (info.GetType() == dataType)
			{
				info.GetType()->Copy(data, info.GetData());
				return true;
			}
			else
			{
				// Have matching name, but incorrect type. Don't pass to base, in case the parameter has been replaced
				// by something with a different type.
				// #TODO: Allow some limited conversion of types?
				return false;
			}
		}
	}

	// Invalid
	return false;
}

TRenderPtr<IRenderResource> CMaterialInstance::CreateRenderResource()
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return nullptr;
	}
	if ( !GetMaterialDefinition() )
	{
		// this was already reported in OnPostLoad()
		return nullptr;
	}
	TRenderPtr<IRenderResource> renderResource( GRender->UploadMaterial( GetMaterialDefinition(), this ) );
	return renderResource;
#endif
}

Bool CMaterialInstance::ContainsParameterWithTheSameNameButDifferentType( const CName& name, const rtti::IType* type, MaterialParameterInstance& outParam )
{
	for ( const auto internalParam : m_parameters )
	{
		const auto internalParamName = internalParam.GetName();
		const auto internalParamType = internalParam.GetType();

		if ( internalParamName == name && internalParamType != type )
		{
			outParam = internalParam;
			return true;
		}

	}

	return false;
}

Bool CMaterialInstance::UpdateParametersListBasedOnTemplate()
{
	Bool wasModified = false;

	// Acquire base material.
	const auto baseMaterial = GetMaterialDefinition();
	if ( !baseMaterial )
	{
		return wasModified;
	}
	const CMaterialTemplate* baseMaterialTemplate = SafeCast< CMaterialTemplate > ( baseMaterial );

	// Iterate over all material shader targets.
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		// Compare base material parameters with the instance's ones.
		const auto baseMaterialTemplateParams = baseMaterialTemplate->GetParameters( EMaterialShaderTarget( target ) );
		for ( auto baseMaterialTemplateParam : baseMaterialTemplateParams )
		{
			const auto baseMaterialRTTIProperty = baseMaterialTemplateParam->GetProperty();
			if ( !baseMaterialRTTIProperty )
			{
				continue;
			}

			const auto baseType = baseMaterialRTTIProperty->GetType();
			const auto baseName = baseMaterialTemplateParam->GetName();

			MaterialParameterInstance mismatchedParameter;
			if ( ContainsParameterWithTheSameNameButDifferentType( baseName, baseType, mismatchedParameter) )
			{
				RED_ASSERT( baseName == mismatchedParameter.GetName() );
				const auto baseTypeName = baseType->GetName();
				const auto mismatchedTypeName = mismatchedParameter.GetType()->GetName();

				// Base material contains Vector4 type, while the instance holds Color type.
				if ( baseTypeName == RED_NAME_CONSTEXPR_NOREG( "Vector4" ) && mismatchedTypeName == RED_NAME_CONSTEXPR_NOREG( "Color" ) )
				{
					const auto mismatchedParamData = mismatchedParameter.GetData();
					const Color* mismatchedParamValues = ( Color* ) (mismatchedParamData);
					const Vector4 scaledValues( static_cast< Float > ( mismatchedParamValues->R ) / 255.0f,
						                        static_cast< Float > ( mismatchedParamValues->G ) / 255.0f,
						                        static_cast< Float > ( mismatchedParamValues->B ) / 255.0f,
						                        static_cast< Float > ( mismatchedParamValues->A ) / 255.0f );

					WriteParameterRaw( mismatchedParameter.GetName(), baseType, ( void* ) &scaledValues );
					wasModified = true;
				}
				// Base material contains Color type, while the instance holds Vector4 type.
				else if ( baseTypeName == RED_NAME_CONSTEXPR_NOREG( "Color" ) && mismatchedTypeName == RED_NAME_CONSTEXPR_NOREG( "Vector4" ) )
				{
					const auto mismatchedParamData = mismatchedParameter.GetData();
					const Vector4* mismatchedParamValues = ( Vector4* )( mismatchedParamData );
					const Color scaledValues( static_cast< unsigned char > ( Clamp< Float >( mismatchedParamValues->X * 255.0f, 0.0f, 255.0f ) ),
						                      static_cast< unsigned char > ( Clamp< Float >( mismatchedParamValues->Y * 255.0f, 0.0f, 255.0f ) ),
						                      static_cast< unsigned char > ( Clamp< Float >( mismatchedParamValues->Z * 255.0f, 0.0f, 255.0f ) ),
						                      static_cast< unsigned char > ( Clamp< Float >( mismatchedParamValues->W * 255.0f, 0.0f, 255.0f ) ) );

					WriteParameterRaw( mismatchedParameter.GetName(), baseType, ( void* ) &scaledValues );
					wasModified = true;
				}
				// Base material contains Float (scalar) type, while the instance holds Color type.
				else if ( baseTypeName == RED_NAME_CONSTEXPR_NOREG( "Float" ) && mismatchedTypeName == RED_NAME_CONSTEXPR_NOREG( "Color" ) )
				{
					const auto mismatchedParamData = mismatchedParameter.GetData();
					const Color* mismatchedParamValues = ( Color* )( mismatchedParamData );
					const Float extractedValue = mismatchedParamValues->R / 255.0f;

					WriteParameterRaw( mismatchedParameter.GetName(), baseType, ( void* ) &extractedValue );
					wasModified = true;
				}
				// Base material contains Float (scalar) type, while the instance holds Vector type.
				else if ( baseTypeName == RED_NAME_CONSTEXPR_NOREG( "Float" ) && mismatchedTypeName == RED_NAME_CONSTEXPR_NOREG( "Vector4" ) )
				{
					const auto mismatchedParamData = mismatchedParameter.GetData();
					const Vector4* mismatchedParamValue = ( Vector4* )( mismatchedParamData );
					const Float extractedValue = mismatchedParamValue->X;

					WriteParameterRaw( mismatchedParameter.GetName(), baseType, ( void* ) &extractedValue );
					wasModified = true;
				}
				// Base material contains Vector type, while the instance holds Float (scalar) type.
				else if ( baseTypeName == RED_NAME_CONSTEXPR_NOREG( "Vector4" ) && mismatchedTypeName == RED_NAME_CONSTEXPR_NOREG( "Float" ) )
				{
					const auto mismatchedParamData = mismatchedParameter.GetData();
					const Float* mismatchedParamValue = ( Float* ) ( mismatchedParamData );
					const Vector4 extractedValues( *mismatchedParamValue,
												   *mismatchedParamValue,
												   *mismatchedParamValue,
												   *mismatchedParamValue );

					WriteParameterRaw( mismatchedParameter.GetName(), baseType, ( void* ) &extractedValues );
					wasModified = true;
				}
				// NOT SUPPORTED: Base material contains Color type, while the instance holds Float (scalar) type.
				// ... due to a way that Variant works - Float is a fundamental type, while Color is not, 
				// which prevents correct interpretation of union of m_data and m_dataDirect.
			}

		}

	}

	return wasModified;
}

void CMaterialInstance::RecreateParentResource()
{
	if( auto resource = Cast< CResource >( GetParent() ) )
	{
		resource->RecreateInternalRenderResources( 1 << 3 ); // ctremblay See CMesh internal flags. This should be move to renderdata...
	}
}

void CMaterialInstance::CacheDefinitionID()
{
	m_definitionID = 0;
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	if ( const IMaterialDefinition* def = GetMaterialDefinition() )
	{
		m_definitionID = def->GetDefinitionID();
	}
#endif // NO_RUNTIME_MATERIAL_COMPILATION
}

// TODO: pczatrowski, resolve this in a better way or make tool copy-paste mechanism check types so that we would never end up with resource paths parsed from floats
Bool CMaterialInstance::ValidateParameters( red::StringBuilder< String >& errorMessageBuilder ) const
{
	const CMaterialTemplate* baseMaterialTemplate = SafeCast< const CMaterialTemplate >( GetMaterialDefinition() );
	RED_ASSERT( baseMaterialTemplate, "MaterialInstance '%hs' with no material definition!!!", GetPath().ToDebugString() );

	Bool result = true;
#ifndef RED_CONFIGURATION_FINAL
	for ( const MaterialParameterInstance& param : m_parameters )
	{
		const CMaterialParameter* templateParam = baseMaterialTemplate->FindParameter( param.GetName() );
		if ( templateParam && templateParam->IsHidden() )
		{
			// skip validation of hidden parameters, they are provided by systems, not users and should have their own validation
			continue;
		}

		const rtti::IType* paramType = param.GetType();
		if ( paramType->GetType() == RT_ResourceReference )
		{
			const rtti::ResourceReferenceType* refType = static_cast< const rtti::ResourceReferenceType* >( paramType );
			if ( refType->GetPointedType()->IsA< ITexture >() )
			{
				TResRef< ITexture > texture;
				RED_VERIFY( ReadParameterRaw( param.GetName(), param.GetType(), &texture ) );
				if ( !texture.Get() )
				{
					errorMessageBuilder.Append( String::Printf( "Texture '%hs' with path '%hs' is invalid. ", param.GetName().AsChar(), texture.GetPath().ToDebugString() ) );
					result = false;
				}
			}
		}
	}
#endif // RED_CONFIGURATION_FINAL
	return result;
}

Bool CMaterialInstance::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
{
	CMaterialTemplate* matTemplate = Cast< CMaterialTemplate >( GetMaterialDefinition() );
	if ( !matTemplate )
	{
		return false;
	}

	Bool isDefaultValue = false;
	rtti::ValuePtr baseValue;

	String accessPath;
	if ( restOfThePath.IsEmpty() )
	{
		accessPath = propertyName.AsChar();
	}
	else
	{
		accessPath =  String::Printf( "%hs.%hs", propertyName.AsChar(), restOfThePath.ToString().AsChar() );
	}
	
	if ( matTemplate->GetClass()->ReadValue( IRTTIContext::GetDefault(), matTemplate, rtti::AccessPath( accessPath.AsChar() ), baseValue )
		|| matTemplate->ReadCustomEditableProperty( IRTTIContext::GetDefault(), propertyName, restOfThePath, baseValue ) )
	{
		if ( baseValue->ToString().EqualsNC( newValue->ToString() ) )
		{
			isDefaultValue = true;
		}
	}

	if ( isDefaultValue )
	{
		if ( IsParameterInstanced( propertyName ) )
		{
			return ClearInstanceParameter( propertyName );
		}
		return true;
	}

	return TBaseClass::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
}

CName CMaterialInstance::GetAudioTag() const
{
	// Check for value in this instance
	if ( m_audioTag && m_audioTag != CName::NONE() )
	{
		return m_audioTag;
	}

	// Read from base material
	if ( m_baseMaterial )
	{
		return m_baseMaterial->GetAudioTag();
	}

	// Invalid
	return CName::NONE();
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION

void CMaterialInstance::RecompileMaterialsUsingTexture( ITexture* texture )
{
	red::ScopedLock< red::Mutex > mutex( IMaterialDefinition::st_accessMutex );

	Uint8 paramType = IMaterialDataProviderDesc::PT_Texture;
	if( texture->IsA< CTextureArray >() )
	{
		paramType = IMaterialDataProviderDesc::PT_TextureArray;
	}
	else if( texture->IsA< CCubeTexture >() )
	{
		paramType = IMaterialDataProviderDesc::PT_Cube;
	}

	auto& depMutex = IMaterial::s_dependenciesMutex;

	// Scan all materials
	for( IMaterialDefinition* material : IMaterialDefinition::st_allMaterials )
	{
		if( !material->IsCompiled() )
		{
			continue;
		}

		if( IMaterialDefinition* def = material->GetMaterialDefinition() )
		{
			const Uint32 defID = def->GetDefinitionID();

			RED_SCOPE_LOCK( depMutex );

			const auto& set = IMaterial::s_materialsDependencies.GetRef(defID, rend::PoolMaterials());

			for( const auto& it : set )
			{
				// We can access the material pointer while it is during construction or destruction (where we update the dependencies).
				// In such a case, HandleFromThis asserts and Unsafe_WeakHandleFromThis returns nullptr which is wanted behavior.
				THandle< CMaterialInstance > instance = Cast<CMaterialInstance>( it->Unsafe_WeakHandleFromThis() );

				for( const IMaterialDataProvider::ParameterDefinition& param : def->GetParameterDefinitions( MSH_PixelShader ) )
				{
					if( param.m_type == paramType )
					{
						TResRef< ITexture > theTexture;
						instance->ReadParameter( param.m_name, theTexture );

						// Is it that texture ?
						if( theTexture.Get() == texture )
						{
							def->ForceRecompilation();
							goto nextDef;	// satan 666
						}
					}
				}
			}

		nextDef: {}

		}
	}
}

#endif // NO_RUNTIME_MATERIAL_COMPILATION
