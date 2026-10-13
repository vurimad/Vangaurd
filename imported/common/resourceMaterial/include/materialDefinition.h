/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "material.h"

#include "../../renderData/include/materialDataProvider.h"

/// Material definition is a shader that has user editable parameters
/// Material definition compiles to different techniques depending on rendering context
class RESOURCEMATERIAL_API IMaterialDefinition : public IMaterial, public IMaterialDataProvider
{
	RTTI_DECLARE_TYPE( IMaterialDefinition );
	RED_BASE_CLASS( IMaterial );
	RED_USE_MEMORY_POOL( rend::PoolMaterials );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
public:
	static red::HashSet< IMaterialDefinition* >	st_allMaterials;	//!< All used engine materials
	static red::Mutex							st_accessMutex;		//!< Mutex for accessing the list
#endif

protected:
	red::StaticArray< TParameterDefinitionArray, MSH_MAX >	m_parameterDefinitions;	//!< Material parameters
	red::DynArray< EMaterialVertexFactory >	m_vertexFactories;
	Uint32									m_paramBlockSize[ MSH_MAX ];		//!< Size of parameters data
	Bool									m_compileAllTechniques;				//!< Compile all allowed techniques for this material
	Bool									m_canHaveTangentUpdate;				//!< We can use custom tangent updates on this material
	Bool									m_canHaveDismemberment;				//!< We can use dismemberment on this material
	Bool									m_hasDPL;							//!< DPL tiles should be allocated for the material
	Uint8									m_materialVersion;					//!< Material version (means we need to resave shit)
	mutable red::SpinLock					m_parametersAccessLock;

public:
	IMaterialDefinition();
	virtual ~IMaterialDefinition();

	// Get material parameters
	virtual const TParameterDefinitionArray& GetParameterDefinitions( const EMaterialShaderTarget target ) const override { return m_parameterDefinitions[ target ]; }

	// Get size of compiled parameters block
	virtual Uint32 GetParamBlockSize( const EMaterialShaderTarget target ) const override
	{
		red::ScopedLock< red::SpinLock > lock( m_parametersAccessLock );
		return m_paramBlockSize[ target ];
	}

	// Get the base material definition
	virtual IMaterialDefinition* GetMaterialDefinition() override { return this; }
	virtual const IMaterialDefinition* GetMaterialDefinition() const override { return this; }

	virtual Bool CanUseOnDecals() const override { return m_vertexFactories.FindPtr( MVF_Decal ) != nullptr; }
	virtual Bool CanHaveTangentUpdate() const override { return m_canHaveTangentUpdate; }
	virtual Bool CanHaveDismemberment() const override { return m_canHaveDismemberment; }
	virtual Bool HasDPL() const override { return m_hasDPL; }

	virtual const VertexFactoryMask GetSupportedVertexFactories() const override;
	
	// Compile all techniques for this material
	RED_INLINE Bool CompileAllTechniques() const { return m_compileAllTechniques; }

	//! Is masked
	virtual Bool IsMasked() const override final { return false; }

	virtual Bool IsGlass() const { return false; }
	
	// ISerializable
	virtual void OnPreSave( const PreSaveContext& context ) override;
	virtual void OnSerialize( IFile& file ) override;
	virtual void OnPostLoad( const PostLoadContext& context ) override;
	virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override;
	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
	virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;
	virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) override;

	virtual red::String GetShaderName() const { return String::EMPTY(); }
	
	virtual CName GetDisplayableName() const override { return GetName(); }

	virtual Uint32 GetHash() const override;
	virtual Uint32 GetDefinitionID() const override { return GetHash(); }

	// Is this material compiled
	RED_INLINE Bool IsCompiled() const { return HasRenderResource(); }

protected:
	// Create rendering resource for material
	virtual TRenderPtr<IRenderResource> CreateRenderResource() override;

};
