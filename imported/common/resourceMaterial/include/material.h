/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "resourceMaterialApi.h"

#include "../../redReflection/include/resource.h"

#include "../../renderData/include/materialDataProvider.h"
#include "../../renderData/include/renderResource.h"

/// The definition of renderable material
class ITexture;
class CSkinProfile;
class CHairProfile;
class CFoliageProfile;
class CGradient;
class IMaterialDefinition;
class DynamicTexture;
class Multilayer_Setup;
class Multilayer_Mask;
class CTerrainSetup;

/// Color channel
enum EColorChannel
{
	COLCHANNEL_Red,
	COLCHANNEL_Green,
	COLCHANNEL_Blue,
	COLCHANNEL_Alpha,
};

RTTI_DECLARE_ENUM( EColorChannel );

/// Base material
class RESOURCEMATERIAL_API IMaterial : public CResource, public IMaterialParametersProvider
{
	RTTI_DECLARE_TYPE( IMaterial );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
public:
	static red::HashMap< Uint32, red::DynArray< IMaterial* > >	s_materialsDependencies;	//!< Dependencies (template -> list of instances)
	static red::Mutex											s_dependenciesMutex;		//!< Mutex for accessing the structure above
#endif // NO_RUNTIME_MATERIAL_COMPILATION

protected:
	TResRef< IMaterial >				m_baseMaterial;			//!< Material on which we are based on
	Bool								m_proxyFailed;			//!< Creating rendering proxy for material has failed

	TRenderPtr<IRenderResource>			m_renderResource;		//!< Rendering resource created for this material
	mutable red::LightMutex				m_createRenderResourceLock;

public:
	// Get base material
	const res::ResourcePath& GetBaseMaterialPath() const { return m_baseMaterial.GetPath(); }
	IMaterial* GetBaseMaterial() const { return m_baseMaterial.Get().Get(); }

	virtual const CName GetName() const = 0;

	// Remove rendering proxy resource for this material
	void InvalidateRenderResource();

	// Get the rendering proxy for this material
	TRenderPtr<IRenderResource> GetRenderResource() const;

	// Update rendering resource for material
	void UpdateRenderResource();

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }

	virtual Bool ValidateParameters( red::StringBuilder< red::String >& errorMessageBuilder ) const { return true; }

protected:
	// Create rendering resource for material
	virtual TRenderPtr<IRenderResource> CreateRenderResource() = 0;

	Uint32 PushResource( const IMaterialDataProvider::ParameterDefinition& param, const TRenderPtr< IRenderResource >& renderResource, red::DynArray< TRenderPtr< IRenderResource > >& resources ) const;

public:
	virtual Uint32 GetDefinitionID() const = 0;

public:
	IMaterial( const THandle< IMaterial >& baseMaterial );
	virtual ~IMaterial();

	// Is this material using mask ?
	virtual Bool IsMasked() const = 0;


	// IMaterialParametersProvider
	virtual const res::ResourcePath& GetMaterialAssetPath() const override;
	virtual Uint32 GetCompiledDataBufferSize( const EMaterialShaderTarget shaderTarget ) const override;
	virtual void CompileDataBuffer( const EMaterialShaderTarget shaderTarget, const red::BlobSpan& outData, red::DynArray< TRenderPtr< IRenderResource > >& resources, Bool preloadResources ) const override;
	virtual Bool ExtractTextureParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractBufferParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractScalarParam( CName name, Float& outValue ) const override;
	virtual Bool ExtractVectorParam( CName name, Vector4& outValue ) const override;
	virtual Bool ExtractColorParam( CName name, Color& outValue ) const override;
	virtual Bool ExtractNameU64Param( CName name, Uint64& outValue ) const override;
	virtual Bool ExtractSkinProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractHairProfileParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractFoliageProfileParam(CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources) const override;
	virtual Bool ExtractGradientParam(CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources) const override;
	virtual Bool ExtractMultilayerSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractMultilayerMaskParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;
	virtual Bool ExtractTerrainSetupParam( CName name, TRenderPtr< IRenderResource >& outValue, Bool preloadResources ) const override;


public:
	//! Get base material
	virtual IMaterialDefinition* GetMaterialDefinition()=0;
	virtual const IMaterialDefinition* GetMaterialDefinition() const = 0;

public:
	// Dynamic editable properties - for materials, blocks with dynamic content, etc.
	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;

	// Read dynamic property
	virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;

	// Write dynamic property
	virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) override;

public:
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	virtual Bool Recompile( const Bool includesMatch, const Bool onlyHighPriority = false ) = 0;

	// Force material recompilation
	void ForceRecompilation( Bool createRenderResource = true );
#endif

public:

	//! Write material parameter data. Optionally force a recreate on the material render resource.
	virtual Bool WriteParameterRaw( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) = 0;

	virtual Bool WriteParameterRaw_NoDefinitionCheck( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) = 0;

	// Read parameter data. Only succeeds if the given type can be read (if there exists a value, but with an
	// incompatible type, then this can fail).
	virtual Bool ReadParameterRaw( const CName& name, const rtti::IType* dataType, void* data ) const = 0;

	virtual Bool ReadParameterRaw_NoBase( const CName& name, const rtti::IType* dataType, void* data ) const = 0;

	// Predefined read and write functions. Not just exposing the general templates, so we can control what types are
	// actually allowed to be set.
#define IMATERIAL_READ_PARAMETER(Type) RED_INLINE Bool ReadParameter( const CName& name, Type& outVal ) const { return ReadParameterInternal( name, outVal ); }
	IMATERIAL_READ_PARAMETER( Float );
	IMATERIAL_READ_PARAMETER( Color );
	IMATERIAL_READ_PARAMETER( Vector4 );
	IMATERIAL_READ_PARAMETER( Matrix );
	IMATERIAL_READ_PARAMETER( CName );
	IMATERIAL_READ_PARAMETER( TResRef<ITexture> );
	IMATERIAL_READ_PARAMETER( TResRef<CSkinProfile> );
	IMATERIAL_READ_PARAMETER( TResRef<CHairProfile> );
	IMATERIAL_READ_PARAMETER( TResRef<CFoliageProfile> );
	IMATERIAL_READ_PARAMETER( TResRef<CGradient> );
	IMATERIAL_READ_PARAMETER( TResRef<Multilayer_Setup> );
	IMATERIAL_READ_PARAMETER( TResRef<Multilayer_Mask> );
	IMATERIAL_READ_PARAMETER( TResRef< CTerrainSetup > );
	IMATERIAL_READ_PARAMETER( THandle<DynamicTexture> );
#undef IMATERIAL_READ_PARAMETER

#define IMATERIAL_READ_PARAMETER(Type) RED_INLINE Bool ReadParameter_NoBase( const CName& name, Type& outVal ) const { return ReadParameterInternal_NoBase( name, outVal ); }
	IMATERIAL_READ_PARAMETER(Float);
	IMATERIAL_READ_PARAMETER(Color);
	IMATERIAL_READ_PARAMETER(Vector4);
	IMATERIAL_READ_PARAMETER(Matrix);
	IMATERIAL_READ_PARAMETER(CName);
	IMATERIAL_READ_PARAMETER(TResRef<ITexture>);
	IMATERIAL_READ_PARAMETER(TResRef<CSkinProfile>);
	IMATERIAL_READ_PARAMETER(TResRef<CHairProfile>);
	IMATERIAL_READ_PARAMETER(TResRef<CFoliageProfile>);
	IMATERIAL_READ_PARAMETER(TResRef<CGradient>);
	IMATERIAL_READ_PARAMETER(TResRef<Multilayer_Setup>);
	IMATERIAL_READ_PARAMETER(TResRef<Multilayer_Mask>);
	IMATERIAL_READ_PARAMETER(TResRef< CTerrainSetup >);
	IMATERIAL_READ_PARAMETER(THandle<DynamicTexture>);
#undef IMATERIAL_READ_PARAMETER

#define IMATERIAL_WRITE_PARAMETER(Type) RED_INLINE Bool WriteParameter( const CName& name, const Type& val, Bool recreateResource = true ) { return WriteParameterInternal( name, val, recreateResource ); }
	IMATERIAL_WRITE_PARAMETER( Float );
	IMATERIAL_WRITE_PARAMETER( Color );
	IMATERIAL_WRITE_PARAMETER( Vector4 );
	IMATERIAL_WRITE_PARAMETER( Matrix );
	IMATERIAL_WRITE_PARAMETER( CName );
	IMATERIAL_WRITE_PARAMETER( TResRef<ITexture> );
	IMATERIAL_WRITE_PARAMETER( TResRef<CSkinProfile> );
	IMATERIAL_WRITE_PARAMETER( TResRef<CHairProfile> );
	IMATERIAL_WRITE_PARAMETER( TResRef<CFoliageProfile> );
	IMATERIAL_WRITE_PARAMETER( TResRef<CGradient> );
	IMATERIAL_WRITE_PARAMETER( TResRef<Multilayer_Setup> );
	IMATERIAL_WRITE_PARAMETER( TResRef<Multilayer_Mask> );
	IMATERIAL_WRITE_PARAMETER( TResRef< CTerrainSetup > );
	IMATERIAL_WRITE_PARAMETER( THandle<DynamicTexture> );
#undef IMATERIAL_WRITE_PARAMETER

#define IMATERIAL_WRITE_PARAMETER(Type) RED_INLINE Bool WriteParameter_NoDefinitionCheck( const CName& name, const Type& val, Bool recreateResource = true ) { return WriteParameterInternal_NoDefinitionCheck( name, val, recreateResource ); }
	IMATERIAL_WRITE_PARAMETER(Float);
	IMATERIAL_WRITE_PARAMETER(Color);
	IMATERIAL_WRITE_PARAMETER(Vector4);
	IMATERIAL_WRITE_PARAMETER(Matrix);
	IMATERIAL_WRITE_PARAMETER(CName);
	IMATERIAL_WRITE_PARAMETER(TResRef<ITexture>);
	IMATERIAL_WRITE_PARAMETER(TResRef<CSkinProfile>);
	IMATERIAL_WRITE_PARAMETER(TResRef<CHairProfile>);
	IMATERIAL_WRITE_PARAMETER(TResRef<CFoliageProfile>);
	IMATERIAL_WRITE_PARAMETER(TResRef<CGradient>);
	IMATERIAL_WRITE_PARAMETER(TResRef<Multilayer_Setup>);
	IMATERIAL_WRITE_PARAMETER(TResRef<Multilayer_Mask>);
	IMATERIAL_WRITE_PARAMETER(TResRef< CTerrainSetup >);
	IMATERIAL_WRITE_PARAMETER(THandle<DynamicTexture>);
#undef IMATERIAL_WRITE_PARAMETER

	// Get the audio tag
	virtual CName GetAudioTag() const = 0;

private:
	template<typename T>
	RED_INLINE Bool ReadParameterInternal( const CName& name, T& outVal ) const
	{
		return ReadParameterRaw( name, GetTypeObject<T>(), &outVal );
	}

	template<typename T>
	RED_INLINE Bool ReadParameterInternal_NoBase(const CName& name, T& outVal) const
	{
		return ReadParameterRaw_NoBase(name, GetTypeObject<T>(), &outVal);
	}
	
	template<typename T>
	RED_INLINE Bool WriteParameterInternal( const CName& name, const T& val, Bool recreateResource )
	{
		return WriteParameterRaw( name, GetTypeObject<T>(), &val, recreateResource );
	}

	template<typename T>
	RED_INLINE Bool WriteParameterInternal_NoDefinitionCheck( const CName& name, const T& val, Bool recreateResource )
	{
		return WriteParameterRaw_NoDefinitionCheck( name, GetTypeObject<T>(), &val, recreateResource );
	}

public:
	// Serialization support
	friend void operator<<( IFile& ar, IMaterial*& c )
	{
		serialization::SerializePointer( ar, GetStaticClass(), (void*&)c );
	}

	static Bool AddDependency( IMaterial* material );
	static Bool RemoveDependency( IMaterial* material );

	static void InitDependenciesMemoryPools();
};

