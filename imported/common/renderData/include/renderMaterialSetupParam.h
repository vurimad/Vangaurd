/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "materialStructures.h"
#include "renderResource.h"
#include "materialDataProvider.h"
#include "renderUtils.h"
#include "materialFeatureFlags.h"

//----------------------------------------

struct MaterialParameter
{
	RED_USE_MEMORY_POOL( rend::PoolRenderMaterialParams );

	CName m_name;
};

struct MaterialTextureParameter : public MaterialParameter
{
	TRenderPtr< IRenderResource > m_value;

	MaterialTextureParameter() = default;
	MaterialTextureParameter(CName name, const TRenderPtr< IRenderResource >& param)
		: MaterialParameter{ name }
		, m_value(param)
	{
	}
};

struct MaterialScalarParameter : public MaterialParameter
{
	Float m_value;

	MaterialScalarParameter() = default;
	MaterialScalarParameter(CName name, Float param)
		: MaterialParameter{ name }
		, m_value(param)
	{
	}
};

struct MaterialVectorParameter : public MaterialParameter
{
	Vector4 m_value;

	MaterialVectorParameter() = default;
	MaterialVectorParameter(CName name, const Vector4& param)
		: MaterialParameter{ name }
		, m_value(param)
	{
	}
};

struct MaterialColorParameter : public MaterialParameter
{
	Color  m_value;

	MaterialColorParameter() = default;
	MaterialColorParameter(CName name, const Color& param)
		: MaterialParameter{ name }
		, m_value(param)
	{
	}
};

struct MaterialNameU64Parameter : public MaterialParameter
{
	Uint64 m_value;

	MaterialNameU64Parameter() = default;
	MaterialNameU64Parameter(CName name, Uint64 param)
		: MaterialParameter{ name }
		, m_value(param)
	{
	}
};

struct MaterialSpecialResourceParameter : public MaterialParameter
{
	TRenderPtr< IRenderResource > m_value;
	IMaterialDataProviderDesc::EParameterType m_type;

	MaterialSpecialResourceParameter() = default;
	MaterialSpecialResourceParameter(CName name, const TRenderPtr< IRenderResource >& param, IMaterialDataProviderDesc::EParameterType type)
		: MaterialParameter{ name }
		, m_value(param)
		, m_type(type)
	{
	}
};


struct SMaterialSetupParameters
{
	RED_USE_MEMORY_POOL( rend::PoolRenderMaterialParams );

	red::DynArray< MaterialTextureParameter >			m_textureParameters{ rend::PoolRenderMaterialParams() };	//!< Texture render resources
	red::DynArray< MaterialScalarParameter >			m_scalarParameters{ rend::PoolRenderMaterialParams() };		//!< Scalar parameters
	red::DynArray< MaterialVectorParameter >			m_vectorParameters{ rend::PoolRenderMaterialParams() };		//!< Vector parameters
	red::DynArray< MaterialColorParameter >				m_colorParameters{ rend::PoolRenderMaterialParams() };		//!< Color parameters
	red::DynArray< MaterialNameU64Parameter >			m_nameU64Parameters{ rend::PoolRenderMaterialParams() };
	red::DynArray< MaterialSpecialResourceParameter >	m_specialParameters{ rend::PoolRenderMaterialParams() };	//!< Special-purpose render resources

	SMaterialSetupParameters() {}

	RED_INLINE Bool HasTextureParam( CName name ) const
	{
		for ( auto& param : m_textureParameters )
		{
			if ( param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool HasScalarParam( CName name ) const
	{
		for ( auto& param : m_scalarParameters )
		{
			if ( param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool HasVectorParam( CName name ) const
	{
		for ( auto& param : m_vectorParameters )
		{
			if ( param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool HasColorParam( CName name ) const
	{
		for ( auto& param : m_colorParameters )
		{
			if ( param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool HasNameU64Param( CName name ) const
	{
		for ( auto& param : m_nameU64Parameters )
		{
			if ( param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool HasSpecialResourceParam( CName name, IMaterialDataProviderDesc::EParameterType type ) const
	{
		for ( auto& param : m_specialParameters )
		{
			if ( param.m_type == type && param.m_name == name )
			{
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractTextureParam( CName name, TRenderPtr< IRenderResource >& outValue ) const
	{
		for( auto& param : m_textureParameters )
		{
			if( param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractScalarParam( CName name, Float& outValue ) const
	{
		for( auto& param : m_scalarParameters )
		{
			if( param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractVectorParam( CName name, Vector4& outValue ) const
	{
		for( auto& param : m_vectorParameters )
		{
			if( param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractColorParam( CName name, Color& outValue ) const
	{
		for( auto& param : m_colorParameters )
		{
			if( param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractNameU64Param( CName name, Uint64& outValue ) const
	{
		for( auto& param : m_nameU64Parameters )
		{
			if( param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE Bool ExtractSpecialResourceParam( CName name, IMaterialDataProviderDesc::EParameterType type, TRenderPtr< IRenderResource >& outValue ) const
	{
		for ( auto& param : m_specialParameters )
		{
			if ( param.m_type == type && param.m_name == name )
			{
				outValue = param.m_value;
				return true;
			}
		}
		return false;
	}

	RED_INLINE TRenderPtr< IRenderResource > ExtractTextureParam( CName name ) const
	{
		TRenderPtr< IRenderResource > value;
		ExtractTextureParam( name, value );
		return value;
	}

	RED_INLINE Float ExtractScalarParam( CName name ) const
	{
		Float value = 0.0f;
		ExtractScalarParam( name, value );
		return value;
	}

	RED_INLINE Vector4 ExtractVectorParam( CName name ) const
	{
		Vector4 value = Vector4::ZEROS();
		ExtractVectorParam( name, value );
		return value;
	}

	RED_INLINE Color ExtractColorParam( CName name ) const
	{
		Color value = Color();
		ExtractColorParam( name, value );
		return value;
	}

	RED_INLINE Uint64 ExtractNameU64Param( CName name ) const
	{
		Uint64 value = 0;
		ExtractNameU64Param( name, value );
		return value;
	}

	RED_INLINE TRenderPtr< IRenderResource > ExtractSpecialResourceParam( CName name, IMaterialDataProviderDesc::EParameterType type ) const
	{
		TRenderPtr< IRenderResource > value;
		ExtractSpecialResourceParam( name, type, value );
		return value;
	}


	//
	// Add a parameter, without checking for an existing value. Simply adds the parameter to the back of the appropriate
	// parameter array.
	//

	RED_INLINE void AddScalarParameter( CName name, const Float value )
	{
		m_scalarParameters.PushBack({ name, value });
	}

	RED_INLINE void AddColorParameter( CName name, const Color& value )
	{
		m_colorParameters.PushBack({ name, value });
	}

	RED_INLINE void AddTextureParameter( CName name, const TRenderPtr<IRenderResource>& value )
	{
		m_textureParameters.PushBack({ name, value });
	}

	RED_INLINE void AddBufferParameter( CName name, const TRenderPtr<IRenderResource>& value )
	{
		m_specialParameters.PushBack({ name, value, IMaterialDataProviderDesc::PT_StructBuffer });
	}


	//
	// Set a parameter, checking for the existance of an existing value. If replaceExisting==true, and a similar-typed
	// parameter already exists, then the old value is replaced. If replaceExisting==false, and a similar-typed
	// parameter already exists, then the old value is kept and the new value is ignored.
	//

	RED_INLINE void SetScalarParameter( CName name, const Float value, Bool replaceExisting = true )
	{
		SetParameterInternal( m_scalarParameters, name, value, replaceExisting );
	}

	RED_INLINE void SetVectorParameter( CName name, const Vector4& value, Bool replaceExisting = true )
	{
		SetParameterInternal( m_vectorParameters, name, value, replaceExisting );
	}

	RED_INLINE void SetColorParameter( CName name, const Color& value, Bool replaceExisting = true )
	{
		SetParameterInternal( m_colorParameters, name, value, replaceExisting );
	}

	RED_INLINE void SetNameU64Parameter( CName name, const Uint64 value, Bool replaceExisting = true )
	{
		SetParameterInternal( m_nameU64Parameters, name, value, replaceExisting );
	}

	RED_INLINE void SetNameU64Parameter( CName name, const CName& value, Bool replaceExisting = true )
	{
		Uint64 nameU64 = rend::CalculateNameU64( value );
		SetParameterInternal( m_nameU64Parameters, name, nameU64, replaceExisting );
	}

	RED_INLINE void SetTextureParameter( CName name, const TRenderPtr<IRenderResource>& texture, Bool replaceExisting = true )
	{
		SetParameterInternal( m_textureParameters, name, texture, replaceExisting );
	}

	RED_INLINE void SetBufferParameter( CName name, const TRenderPtr<IRenderResource>& buffer, Bool replaceExisting = true )
	{
		SetParameterInternal( m_specialParameters, name, buffer, replaceExisting, IMaterialDataProviderDesc::PT_StructBuffer );
	}

	RED_INLINE void SetBufferParameter( CName name, IRenderResource* buffer, Bool replaceExisting = true )
	{
		TRenderPtr<IRenderResource> ptr;
		ptr.Reset( buffer );
		SetParameterInternal( m_specialParameters, name, ptr, replaceExisting, IMaterialDataProviderDesc::PT_StructBuffer );
	}

	RED_INLINE void SetSpecialResourceParameter( CName name, IMaterialDataProviderDesc::EParameterType type, const TRenderPtr<IRenderResource>& resource, Bool replaceExisting = true )
	{
		SetParameterInternal( m_specialParameters, name, resource, replaceExisting, type );
	}

private:
	template<typename ParamType, typename ValueType, typename... ExtraConstructArgs>
	RED_INLINE void SetParameterInternal( red::DynArray<ParamType>& parameters, CName name, const ValueType& value, Bool replaceExisting, ExtraConstructArgs&&... constructArgs )
	{
		for ( auto& param : parameters )
		{
			if ( param.m_name == name )
			{
				if ( replaceExisting )
				{
					param = { name, value, std::forward<ExtraConstructArgs>( constructArgs )... };
				}
				return;
			}
		}

		parameters.PushBack({ name, value, std::forward<ExtraConstructArgs>( constructArgs )... });
	}
};

struct RENDER_DATA_API SMaterialSetupEntry
{
	RED_USE_MEMORY_POOL( rend::PoolRenderMaterialParams );

public:
	TRenderPtr<IRenderResource>		m_materialResource;		// Base material render resource

	Bool							m_isMasked;				// Is masked
	Bool							m_isMultilayered;		// Is multilayered

	SMaterialSetupParameters		m_parameters[MSH_MAX];	// Parameters

	mutable Uint64 m_cachedHash = 0;

public:
	SMaterialSetupEntry();
	~SMaterialSetupEntry();

	SMaterialSetupEntry(const SMaterialSetupEntry&) = default;
	SMaterialSetupEntry& operator =(const SMaterialSetupEntry&) = default;

	SMaterialSetupEntry(SMaterialSetupEntry&&) = default;
	SMaterialSetupEntry& operator =(SMaterialSetupEntry&&) = default;

public:
	RED_INLINE const TRenderPtr<IRenderResource>& GetMaterialResource() const { return m_materialResource; }

	Uint64 CalculateHash() const;
};
using MaterialSetupEntryPtr = red::SharedPtr<SMaterialSetupEntry>;

struct SMaterialUpdateData
{
	RED_USE_MEMORY_POOL( rend::PoolRenderData );

public:
	SMaterialUpdateData()
		: m_samplerStateInfos( MSH_MAX, rend::PoolRenderMaterialParams() )
		, m_parameters( MSH_MAX, rend::PoolRenderMaterialParams() )
		, m_usedParameters( MSH_MAX, rend::PoolRenderMaterialParams() )
		, m_parametersData( red::MakeEmptyUniqueBuffer<rend::PoolRenderMaterialParams>(1) )
	{
	}

	SMaterialUpdateData( const SMaterialUpdateData& ) = delete;
	SMaterialUpdateData& operator =( const SMaterialUpdateData& ) = delete;

	SMaterialUpdateData( SMaterialUpdateData&& ) = default;
	SMaterialUpdateData& operator =( SMaterialUpdateData&& ) = default;

public:
	Uint32 m_totalTechniquesNum;
	Uint32 m_engineMaterialHash;
	CName m_engineMaterialName;

	EMaterialPriority m_materialPriority;

	Bool m_canHaveDismemberment;
	Bool m_hasDPL;
	Bool m_isValid;

	VertexFactoryMask m_supportedVertexFactories;

	EngineStagePassDataCollectionArray m_engineStatePassDataCollectionArray { rend::PoolMaterialStagePassData() };

	red::DynArray< TechniqueFeatures > m_techFeatures{ rend::PoolMaterialTechniques() };

	red::StaticArray< TSamplerStateInfoArray, MSH_MAX > m_samplerStateInfos;
	red::StaticArray<IMaterialDataProvider::TParameterDefinitionArray, MSH_MAX > m_parameters;
	Uint32 m_paramBlockSize[ MSH_MAX ];
	red::StaticArray<TUsedParameterArray, MSH_MAX > m_usedParameters;

	red::Blob m_parametersData;
	red::FixedArray< Uint32, MSH_MAX > m_parametersDataShaderOffsets;
	red::DynArray< TRenderPtr< IRenderResource > > m_resources{ rend::PoolRenderMaterialParams() };
};


RENDER_DATA_API Uint64 CalculateMaterialSetupParametersHash( const SMaterialSetupParameters& params, Uint64 baseHash );
