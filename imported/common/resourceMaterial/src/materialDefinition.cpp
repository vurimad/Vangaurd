/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif
#include "materialDefinition.h"
#include "../../redContainers/include/map.h"
#include "../../renderData/include/renderResource.h"
#include "../../resourceMaterial/include/textureArray.h"
#include "../../resourceMaterial/include/cubeTexture.h"
#include "../../resourceMaterial/include/bitmapTexture.h"
#include "../../redReflection/include/variant.h"

using red::DynArray;
using red::HashMap;
using red::HashSet;
using red::Map;

RTTI_BEGIN_ABSTRACT_TYPE( IMaterialDefinition );
{
	RTTI_PARENT_TYPE( IMaterial );
	RTTI_PROPERTY( m_paramBlockSize ).readOnly();
	RTTI_PROPERTY( m_compileAllTechniques ).editable();
	RTTI_PROPERTY( m_canHaveTangentUpdate ).editable().tooltip( "Must be checked for materials that can have skinning applied in compute shader." );
	RTTI_PROPERTY( m_canHaveDismemberment ).editable().tooltip( "Must be checked for materials that are used on dismembered bodyparts." );
	RTTI_PROPERTY( m_hasDPL ).editable().tooltip( "Must be checked for materials that needs to use DPL." );
	RTTI_PROPERTY( m_materialVersion );
	RTTI_PROPERTY( m_vertexFactories );
}
RTTI_END_TYPE();

RTTI_BEGIN_ENUM( EMaterialVertexFactory );
{
	RTTI_ENUM_OPTION( MVF_Terrain );
	RTTI_ENUM_OPTION( MVF_MeshStatic );
	RTTI_ENUM_OPTION( MVF_MeshSkinned );
	RTTI_ENUM_OPTION( MVF_MeshExtSkinned );
	RTTI_ENUM_OPTION( MVF_GarmentMeshSkinned );
	RTTI_ENUM_OPTION( MVF_GarmentMeshExtSkinned );
	RTTI_ENUM_OPTION( MVF_MeshSpeedTree );
	RTTI_ENUM_OPTION( MVF_ParticleBilboard );
	RTTI_ENUM_OPTION( MVF_ParticleParallel );
	RTTI_ENUM_OPTION( MVF_ParticleMotionBlur );
	RTTI_ENUM_OPTION( MVF_ParticleSphereAligned );
	RTTI_ENUM_OPTION( MVF_ParticleVerticalFixed );
	RTTI_ENUM_OPTION( MVF_ParticleTrail );
	RTTI_ENUM_OPTION( MVF_ParticleFacingTrail );
	RTTI_ENUM_OPTION( MVF_ParticleScreen );
	RTTI_ENUM_OPTION( MVF_ParticleBeam );
	RTTI_ENUM_OPTION( MVF_ParticleFacingBeam );
	RTTI_ENUM_OPTION( MVF_Decal );
	RTTI_ENUM_OPTION( MVF_Debug );
	RTTI_ENUM_OPTION( MVF_DrawBuffer );
	RTTI_ENUM_OPTION( MVF_Fullscreen );
	RTTI_ENUM_OPTION( MVF_MeshSkinnedVehicle );
	RTTI_ENUM_OPTION( MVF_MeshStaticVehicle );
	RTTI_ENUM_OPTION( MVF_MeshProcedural );
	RTTI_ENUM_OPTION( MVF_MeshDestructible );
	RTTI_ENUM_OPTION( MVF_MeshDestructibleSkinned );
	RTTI_ENUM_OPTION( MVF_MeshSkinnedLightBlockers );
	RTTI_ENUM_OPTION( MVF_MeshExtSkinnedLightBlockers );
	RTTI_ENUM_OPTION( MVF_GarmentMeshSkinnedLightBlockers );
	RTTI_ENUM_OPTION( MVF_GarmentMeshExtSkinnedLightBlockers );
	RTTI_ENUM_OPTION( MVF_MeshSkinnedSingleBone );
	RTTI_ENUM_OPTION( MVF_MeshProxy );
	RTTI_ENUM_OPTION( MVF_MeshWindowProxy );
}
RTTI_END_ENUM();
static_assert( MVF_Max == 34, "EMaterialVertexFactory probably changed. Don't forget to update here!" );


namespace Version
{
	enum Enum
	{
		BASE,
		VER_MATERIAL_PARAM_OFFSET_8B_TO_16B					,		// Material param offset changed to 16-bit
		VER_MATERIAL_EXTENDED_PARAMETER_DEFINITIONS			,
		VER_MATERIAL_SEPARATE_FLAGS_FOR_VERTEX_FACTORIES	,
		VER_MATERIAL_EMISSIVE_EV							,
		VER_MATERIAL_EMISSIVE_EV2							,

		NEWEST = VER_MATERIAL_EMISSIVE_EV2
	};
}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
red::Mutex IMaterialDefinition::st_accessMutex;
HashSet< IMaterialDefinition* > IMaterialDefinition::st_allMaterials{ red::PoolResource() };
#endif //NO_RUNTIME_MATERIAL_COMPILATION

IMaterialDefinition::IMaterialDefinition()
	: IMaterial( nullptr )
	, m_compileAllTechniques( false )
	, m_canHaveTangentUpdate( false )
	, m_canHaveDismemberment( false )
	, m_hasDPL( false )
	, m_materialVersion( 0 )
	, m_parameterDefinitions( MSH_MAX, rend::PoolMaterials() )
	, m_vertexFactories( rend::PoolMaterials() )
{
	for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
	{
		m_paramBlockSize[ target ] = 0;
	}
}

IMaterialDefinition::~IMaterialDefinition()
{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	{
		// Unregister from the list of materials
		red::ScopedLock< red::Mutex > mutex( st_accessMutex );
		st_allMaterials.Remove( this );
	}
#endif //NO_RUNTIME_MATERIAL_COMPILATION
}

void IMaterialDefinition::OnPreSave( const PreSaveContext& context )
{
	m_materialVersion = Version::NEWEST;

	TBaseClass::OnPreSave( context );
}
void IMaterialDefinition::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	// remove invalid vertex factories
	{
		const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName<EMaterialVertexFactory>::GetTypeName() );
		RED_FATAL_ASSERT( vertexFactoryEnumType );

		for ( Uint32 i = 0; i < m_vertexFactories.Size(); )
		{
			CName vertexFactoryName;
			if ( !vertexFactoryEnumType->FindName( m_vertexFactories[i], vertexFactoryName ) )
			{
				m_vertexFactories.RemoveAt( i );
			}
			else
			{
				i++;
			}
		}
	}

	if ( m_materialVersion < Version::VER_MATERIAL_EMISSIVE_EV )
	{
		for( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			for( auto& param : m_parameterDefinitions[target] )
			{
				if( param.m_name == RED_NAME_CONSTEXPR_NOREG("EmissiveBrightness") && param.m_type == IMaterialDataProviderDesc::EParameterType::PT_Scalar )
				{
					param.m_name = RED_NAME_CONSTEXPR("EmissiveEV");
					// Keep offset the same - its still Float
				}
			}
		}
	}

	if ( m_materialVersion < Version::VER_MATERIAL_EMISSIVE_EV2 )
	{
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			for (auto& param : m_parameterDefinitions[target])
			{
				if (param.m_name == RED_NAME_CONSTEXPR_NOREG("EmissiveIntensity") && param.m_type == IMaterialDataProviderDesc::EParameterType::PT_Scalar)
				{
					param.m_name = RED_NAME_CONSTEXPR("EmissiveEV");
					// Keep offset the same - its still Float
				}
			}
		}
	}
}

void IMaterialDefinition::OnSerialize( IFile& file )
{
	// TODO remove after resave
	if ( file.IsReader() && m_materialVersion < Version::VER_MATERIAL_SEPARATE_FLAGS_FOR_VERTEX_FACTORIES )
	{
		red::alg::PushBackUnique( m_vertexFactories, MVF_MeshStatic );
		red::alg::PushBackUnique( m_vertexFactories, MVF_MeshSkinned );
		red::alg::PushBackUnique( m_vertexFactories, MVF_MeshExtSkinned );
		red::alg::PushBackUnique( m_vertexFactories, MVF_MeshStaticVehicle );
		red::alg::PushBackUnique( m_vertexFactories, MVF_MeshSkinnedVehicle );
		red::alg::PushBackUnique( m_vertexFactories, MVF_GarmentMeshSkinned );
		red::alg::PushBackUnique( m_vertexFactories, MVF_GarmentMeshExtSkinned );
	}

	TBaseClass::OnSerialize( file );

	// Serialize
	if ( file.IsReader() && m_materialVersion < Version::VER_MATERIAL_EXTENDED_PARAMETER_DEFINITIONS )
	{
		file << m_parameterDefinitions[ MSH_PixelShader ];
		file << m_parameterDefinitions[ MSH_VertexShader ];
	}
	else
	{
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			file << m_parameterDefinitions[ target ];
		}
	}
}

Bool IMaterialDefinition::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "pixelParamBlockSize" ) )
	{
		Uint32 oldParamBlockSize;
		RED_VERIFY( readValue.Get( oldParamBlockSize ) );
		m_paramBlockSize[ MSH_PixelShader ] = oldParamBlockSize;
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "vertexParamBlockSize" ) )
	{
		Uint32 oldParamBlockSize;
		RED_VERIFY( readValue.Get( oldParamBlockSize ) );
		m_paramBlockSize[ MSH_VertexShader ] = oldParamBlockSize;
		return true;
	}

	// TODO remove after resave
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnMeshes" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( !value ) // Note: "CanUseOnMeshes" was true by default, that's why condition is reversed
		{
			m_vertexFactories.Remove( MVF_MeshStatic );
			m_vertexFactories.Remove( MVF_MeshSkinned );
			m_vertexFactories.Remove( MVF_MeshExtSkinned );
			m_vertexFactories.Remove( MVF_MeshStaticVehicle );
			m_vertexFactories.Remove( MVF_MeshSkinnedVehicle );
			m_vertexFactories.Remove( MVF_GarmentMeshSkinned );
			m_vertexFactories.Remove( MVF_GarmentMeshExtSkinned );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnDestructibleMeshes" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_MeshDestructible );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnProceduralMeshes" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_MeshProcedural );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnDestructibleVehicles" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_MeshSkinnedVehicle );
			red::alg::PushBackUnique( m_vertexFactories, MVF_MeshStaticVehicle );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnDebugFragments" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_Debug );
			red::alg::PushBackUnique( m_vertexFactories, MVF_DrawBuffer );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnParticles" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleBilboard );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleParallel );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleMotionBlur );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleSphereAligned );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleVerticalFixed );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleTrail );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleFacingTrail );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleScreen );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleBeam );
			red::alg::PushBackUnique( m_vertexFactories, MVF_ParticleFacingBeam );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnMorphMeshes" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );

		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnFullscreen" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_Fullscreen );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "canUseOnTerrain" ) )
	{
		Bool value;
		RED_VERIFY( readValue.Get( value ) );
		if ( value )
		{
			red::alg::PushBackUnique( m_vertexFactories, MVF_Terrain );
		}
		return true;
	}

	return TBaseClass::OnPropertyMissing( propertyName, readValue );
}

void IMaterialDefinition::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
	TBaseClass::GetCustomEditableProperties( outRootProperties );

	const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName<EMaterialVertexFactory>::GetTypeName() );
	RED_FATAL_ASSERT( vertexFactoryEnumType );

	rtti::ClassEditablePropertyInfo info;
	info.m_category = RED_NAME_CONSTEXPR( "SupportedVertexFactories" );
	info.m_type = GetTypeObject< Bool >();

	for ( Uint32 vertexFactoryIndex = 0; vertexFactoryIndex < MVF_Max; ++vertexFactoryIndex )
	{
		const EMaterialVertexFactory vertexFactory = static_cast<EMaterialVertexFactory>( vertexFactoryIndex );

		if ( MVF_Invalid != vertexFactory )
		{
			if ( vertexFactoryEnumType->FindName( vertexFactory, info.m_name ) )
			{
				outRootProperties.Add( info );
			}
		}
	}
}

Bool IMaterialDefinition::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
{
	const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName<EMaterialVertexFactory>::GetTypeName() );
	RED_FATAL_ASSERT( vertexFactoryEnumType );

	for ( Uint32 vertexFactoryIndex = 0; vertexFactoryIndex < MVF_Max; ++vertexFactoryIndex )
	{
		const EMaterialVertexFactory vertexFactory = static_cast<EMaterialVertexFactory>( vertexFactoryIndex );
		if ( MVF_Invalid != vertexFactory )
		{
			CName vertexFactoryName;
			if ( vertexFactoryEnumType->FindName( vertexFactory, vertexFactoryName ) )
			{
				if ( propertyName == vertexFactoryName )
				{
					Bool value = m_vertexFactories.Exist( vertexFactory );
					return GetTypeObject< Bool >()->ReadValue( ctx, &value, restOfThePath, outValue );
				}
			}
		}
	}

	return TBaseClass::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
}

Bool IMaterialDefinition::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
{
	const rtti::EnumType* vertexFactoryEnumType = GetRttiSystem().FindEnum( TTypeName<EMaterialVertexFactory>::GetTypeName() );
	RED_FATAL_ASSERT( vertexFactoryEnumType );

	for ( Uint32 vertexFactoryIndex = 0; vertexFactoryIndex < MVF_Max; ++vertexFactoryIndex )
	{
		const EMaterialVertexFactory vertexFactory = static_cast<EMaterialVertexFactory>( vertexFactoryIndex );
		if ( MVF_Invalid != vertexFactory )
		{
			CName vertexFactoryName;
			if ( vertexFactoryEnumType->FindName( vertexFactory, vertexFactoryName ) )
			{
				if ( propertyName == vertexFactoryName )
				{
					Bool originalValue = m_vertexFactories.Exist( vertexFactory );
					Bool value = originalValue;
					if ( !GetTypeObject< Bool >()->WriteValue( ctx, &value, restOfThePath, *newValue ) )
					{
						return false;
					}

					if ( originalValue == false && value == true )
					{
						m_vertexFactories.PushBack( vertexFactory );
					}
					if ( originalValue == true && value == false )
					{
						m_vertexFactories.Remove( vertexFactory );
					}

					return true;
				}
			}
		}
	}

	return TBaseClass::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
}

TRenderPtr<IRenderResource> IMaterialDefinition::CreateRenderResource()
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return nullptr;
	}

	// Create rendering resource for material
	TRenderPtr<IRenderResource> renderResource( GRender->UploadMaterial( GetMaterialDefinition(), this ) );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	if ( renderResource )
	{
		red::ScopedLock< red::Mutex > mutex( st_accessMutex );
		// Register in the list
		if ( !st_allMaterials.Exist( this ) )
		{
			RED_VERIFY( st_allMaterials.Insert( this ).IsSuccessful(), "Material %hs was not added to global list, expect problems", GetFriendlyName().AsChar() );
		}
	}
#endif

	return renderResource;
#endif
}

Uint32 IMaterialDefinition::GetHash() const
{
	return red::CalculateAnsiHash32LowerCase( GetName().AsChar() );
}

const VertexFactoryMask IMaterialDefinition::GetSupportedVertexFactories() const
{
	VertexFactoryMask mask;

	for ( const EMaterialVertexFactory vf : m_vertexFactories )
	{
		mask.Set( vf );
	}

	return mask;
}
