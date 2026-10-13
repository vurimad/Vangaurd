/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "pipelineStateObject.h"
#include "materialStructures.h"

#include "../../redReflection/include/resourcePath.h"


//////////////////////////////////////////////////////////////////////

class CRenderMaterialParameters;
class IRenderResource;
struct TechniqueFeatures;
enum EDissolveMode : Uint8;
enum ERenderMaterialType : Uint8;

struct RenderStageId
{
	RenderStageId () : m_value ( 0xffffaaaa ) {}
	explicit RenderStageId ( Uint32 index ) : m_value ( index ) {}
	Bool IsValid() const { return m_value < 0xffffaaaa; }
	Uint32 GetIndex() const { RED_FATAL_ASSERT( IsValid(), "" ); return m_value; }
	Bool operator==( const RenderStageId &other ) const { return m_value == other.m_value; }
	Bool operator!=( const RenderStageId &other ) const { return m_value != other.m_value; }

public:
	static RenderStageId Invalid() { return RenderStageId(); }

private:
	Uint32 m_value;
};

// some of the values will be filled from engine-side MaterialTemplates, others created just after filling the descs on the render side
struct EngineStagePassData
{
	RED_USE_MEMORY_POOL( rend::PoolRenderData );

	SOMState					m_omState;
	CName						m_stageNameRegular;
	CName						m_stageNameDiscarded;
	Bool						m_enablePixelShader;
	Uint32						m_streamsToBind;
	Uint8						m_orderIndex;
};
typedef red::DynArray< EngineStagePassData > EngineStagePassDataCollection;
typedef red::DynArray< EngineStagePassDataCollection > EngineStagePassDataCollectionArray;

struct StagePassFileTimes
{
	red::DateTime	fileTime[ MSH_MAX ];
};
typedef red::HashMap< CName, StagePassFileTimes > StagePassFileTimesMap;

#define STATIC_PARAMETERS_LIMIT			( 32 )
#define MAX_STATIC_PARAMETER_REGISTER	( STATIC_PARAMETERS_LIMIT - 1 )

static constexpr Uint32 InvalidResourceIndex = std::numeric_limits< Uint32 >::max();

namespace IMaterialDataProviderDesc
{
	/// Material dynamic parameter type
	enum EParameterType : Uint8
	{
		PT_None,			//!< No parameter
		PT_Texture,			//!< Texture
		PT_Color,			//!< Color
		PT_Cube,			//!< Cubemap
		PT_Vector,			//!< 4 element vector
		PT_Scalar,			//!< Single scalar
		PT_Bool,			//!< bool 
		PT_TextureArray,	
		PT_StructBuffer,	//!< Structured buffer

		// Non-GPU parameters
		PT_Cpu_NameU64,		//!< CName parameter, which gets converted to a Uint64 during compilation

		PT_SkinProfile,		//!< Skin params
		PT_MultilayerSetup,
		PT_MultilayerMask,

		PT_HairProfile,

		PT_FoliageProfile,

		PT_TerrainSetup,

		PT_Gradient,

		PT_MAX,
	};

	enum EParameterRegisterType : Uint8
	{
		PRT_Texture,		// Bound as a texture
		PRT_Constant,		// Packed into constant buffer
		PRT_Dummy,			// Not used as gpu data
	};

	static RED_INLINE Bool IsParameterStatic( EParameterType paramType )
	{
		static_assert( PT_MAX == 17, "ParameterType enum changed" );
		switch ( paramType )
		{
			case PT_Color:
			case PT_Vector:
			case PT_Scalar:
			case PT_Bool:
			case PT_SkinProfile:
			case PT_HairProfile:
			case PT_FoliageProfile:
			case PT_Gradient:
			case PT_TerrainSetup:
				return true;
		};
		return false;
	}

	static Bool ValidateParameterRegister( EParameterType paramType, Uint8 reg )
	{
		if ( IsParameterStatic( paramType ) )
		{
			return reg <= MAX_STATIC_PARAMETER_REGISTER;
		}
		return true;
	}

	RED_INLINE Bool IsRegisterMandatory( EParameterType paramType )
	{
		static_assert( PT_MAX == 17, "ParameterType enum changed" );
		switch ( paramType )
		{
			case PT_StructBuffer:
				return true;
	
			case PT_Texture:
			case PT_Cube:
			case PT_TextureArray:
			case PT_Color:
			case PT_Vector:
			case PT_Scalar:
			case PT_Bool:
			case PT_Cpu_NameU64:
			case PT_SkinProfile:
			case PT_HairProfile:
			case PT_FoliageProfile:
			case PT_Gradient:
			case PT_MultilayerSetup:
			case PT_MultilayerMask:
			case PT_TerrainSetup:
				return false;
	
			default:
				RED_ASSERT( false, "Invalid parameter type" );
				return false;
		};
	}


	RED_INLINE EParameterRegisterType GetParameterRegisterType( EParameterType paramType )
	{
		static_assert( PT_MAX == 17, "ParameterType enum changed" );
		switch ( paramType )
		{
		case PT_Texture:
		case PT_Cube:
		case PT_TextureArray:
		case PT_StructBuffer:
			return PRT_Texture;

		case PT_Color:
		case PT_Vector:
		case PT_Scalar:
		case PT_Bool:
		case PT_SkinProfile:
		case PT_HairProfile:
		case PT_FoliageProfile:
		case PT_Gradient:
		case PT_TerrainSetup:
			return PRT_Constant;

		case PT_Cpu_NameU64:
		case PT_MultilayerSetup:
		case PT_MultilayerMask:
			return PRT_Dummy;

		default:
			RED_ASSERT( false, "Invalid parameter type" );
			return PRT_Dummy;
		};
	}
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EParameterType, IMaterialDataProviderDesc );

/// Info about parameter used by rendering technique
class UsedParameter
{
	RED_USE_MEMORY_POOL(rend::PoolRenderMaterialParams);

public:
	Uint16										m_offset;				//!< Offset in parameter table
	IMaterialDataProviderDesc::EParameterType	m_type;					//!< Type of parameter
	Uint8										m_register;				//!< Register we should bind the parameter to

public:
	RED_INLINE UsedParameter()
		: m_type(IMaterialDataProviderDesc::PT_None)
		, m_offset(0)
	{};

	RED_INLINE UsedParameter(IMaterialDataProviderDesc::EParameterType type, Uint16 offset, Uint8 registerIndex)
		: m_type(type)
		, m_offset(offset)
		, m_register(registerIndex)
	{
	};
};
typedef red::DynArray< UsedParameter > TUsedParameterArray;

/// Base render resource
class IMaterialDataProvider
{
	RED_USE_MEMORY_POOL( rend::PoolRenderMaterialParams );

public:
	/// Material parameter 
	class ParameterDefinition
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderMaterialParams );

	public:
		CName										m_name;			//!< Parameter name
		Uint16										m_offset;		//!< Offset in parameter table
		IMaterialDataProviderDesc::EParameterType	m_type;			//!< Type of parameter

	public:
		RED_INLINE ParameterDefinition()
			: m_type( IMaterialDataProviderDesc::PT_None )
			, m_offset( 0 )
		{};

		RED_INLINE ParameterDefinition( IMaterialDataProviderDesc::EParameterType type, const CName& name, Uint16 offset )
			: m_type( type )
			, m_name( name )
			, m_offset( offset )
		{};

	public:
		// Serialization
		friend void operator<<( IFile& file, ParameterDefinition& param )
		{
			file.Serialize( &param.m_type, sizeof( param.m_type ) );
			file << param.m_offset;
			file << param.m_name;
		}
	};
	typedef red::DynArray< ParameterDefinition > TParameterDefinitionArray;

protected:
	virtual ~IMaterialDataProvider() { }

public:
	IMaterialDataProvider()	{ }

public:
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	virtual red::DateTime GetTimeStamp( const CName stagePassName, EMaterialShaderTarget target ) const = 0;
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	virtual CName GetDisplayableName() const = 0;

	virtual Uint32 GetHash() const = 0;

	virtual Bool CanUseOnDecals() const = 0;
	virtual Bool CanHaveTangentUpdate() const = 0;
	virtual Bool CanHaveDismemberment() const = 0;
	virtual Bool HasDPL() const = 0;
	virtual Bool IsSpeedTreeBillboard() const { return false; }

	virtual const VertexFactoryMask GetSupportedVertexFactories() const = 0;

	virtual Bool IsValid() const { return true; }

public:
	// Get material parameters
	virtual const TParameterDefinitionArray& GetParameterDefinitions( const EMaterialShaderTarget target ) const = 0;
	
	// Get size of compiled parameters block
	virtual Uint32 GetParamBlockSize( const EMaterialShaderTarget target ) const = 0;

	virtual const TMaterialUsedParameterArray& GetUsedParameters( const EMaterialShaderTarget target ) const = 0;

	virtual const TSamplerStateInfoArray& GetSamplerStates( const EMaterialShaderTarget target ) const = 0;

	virtual void GetPasses( Uint32 techniqueIndex, EngineStagePassDataCollection& stagePassCollection ) const = 0;

	virtual void GetTechniquesFeatures( red::DynArray<TechniqueFeatures> &outTechFeatures ) const = 0;

	virtual Uint32 GetNumTechniques() const = 0;

	virtual EMaterialPriority GetPriority() const = 0;

	virtual ERenderMaterialType GetMaterialType() const = 0;
};

struct ExtendedMaterialParameters
{
	struct SRayTracedEmissiveData
	{
		Float emissiveEV;				// Relative EV of emissive brightness
		Float emissiveDirectionality;	// Directionality control of emissive spread
	};
	SRayTracedEmissiveData m_rtEmissiveData;

	// Specifies the size of each material data block (4-byte words)
	// this must match what is defined in rayTracingMaterials.fx
	Uint32 m_rtMaterialType;
	static constexpr int RT_MATERIAL_DATA_SIZE = 24;
	Uint32 m_rtMaterialData[RT_MATERIAL_DATA_SIZE];

	static constexpr int RT_MATERIAL_ALPHATEST_DATA_SIZE = 8;
	Uint32 m_rtMaterialAlphatestData[RT_MATERIAL_ALPHATEST_DATA_SIZE];
};

class IMaterialParametersProvider
{
public:
	virtual const res::ResourcePath& GetMaterialAssetPath() const = 0;

	virtual Uint32 GetCompiledDataBufferSize( const EMaterialShaderTarget shaderTarget ) const = 0;
	virtual void CompileDataBuffer( const EMaterialShaderTarget shaderTarget, const red::BlobSpan& outData, red::DynArray< TRenderPtr< IRenderResource > >& resources, Bool preloadResources ) const = 0;

	virtual Bool ExtractTextureParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractBufferParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractScalarParam( CName name, Float& outValue ) const = 0;
	virtual Bool ExtractVectorParam( CName name, Vector4& outValue ) const = 0;
	virtual Bool ExtractColorParam( CName name, Color& outValue ) const = 0;
	virtual Bool ExtractNameU64Param( CName name, Uint64& outValue ) const = 0;
	virtual Bool ExtractSkinProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractHairProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractFoliageProfileParam(CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources) const = 0;
	virtual Bool ExtractGradientParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractMultilayerSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractMultilayerMaskParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;
	virtual Bool ExtractTerrainSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const = 0;

	virtual Bool IsMaskedMaterial() const { return false; }
};

namespace MaterialHelpers
{
	static void MapUsedParameters( const TMaterialUsedParameterArray& materialUsedParameters, const IMaterialDataProvider::TParameterDefinitionArray& localParameters, TUsedParameterArray& usedParameters )
	{
		// Convert material dynamic data to list of used parameters
		usedParameters.Reserve( usedParameters.Size() + materialUsedParameters.Size() );
		for ( const MaterialUsedParameter& usedParam : materialUsedParameters )
		{
			for ( const IMaterialDataProvider::ParameterDefinition& localParam : localParameters )
			{
				if ( localParam.m_name == usedParam.m_name )
				{
					usedParameters.EmplaceBack( localParam.m_type, localParam.m_offset, usedParam.m_register );
				}
			}
		}
	}
}
