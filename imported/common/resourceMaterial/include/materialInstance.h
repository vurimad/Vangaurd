/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "material.h"
#include "materialParameterInstance.h"

#include "../../renderData/include/materialDataProvider.h"

typedef red::DynArray< MaterialParameterInstance >		TMaterialParameterInstances;

namespace job
{
	struct RunContext;
}

/// Instance of material
class RESOURCEMATERIAL_API CMaterialInstance final : public IMaterial
{
	RTTI_DECLARE_TYPE( CMaterialInstance );
	IMPLEMENT_RESOURCE_INTERFACE( "w2mi", "mi", "Material instance" );
	RED_BASE_CLASS( IMaterial );

protected:
	TMaterialParameterInstances	m_parameters;		//!< Local instance parameters
	Bool						m_enableMask;		//!< Whether masking should be performed for this material instance (does not include things like dissolves).
	Uint8						m_resourceVersion{0};
	Uint32						m_definitionID;
	CName						m_audioTag;			//!< Support for audio related features (e.g. indicating what sound to play when stepping on puddles)

public:
	//! Get parameters map
	RED_INLINE const TMaterialParameterInstances& GetParameters() const { return m_parameters; }

	//! Is any of the parameters instanced in this material instance ?
	RED_INLINE Bool IsAnyParameterInstanced() const { return m_parameters.Size() > 0; }

public:
	CMaterialInstance();

	// Create material instance from other material. If runtime compilation is available and compileNow is true, then ForceRecompilation
	// will be called, causing the render resources to be created immediately.
	CMaterialInstance( ISerializable* parent, const THandle< IMaterial >& material );

	virtual ~CMaterialInstance();

	// Serialization
	virtual void OnPreSave( const PreSaveContext& context ) override;
	virtual void OnSerialize( IFile& file ) override;
	virtual void OnPostLoad( const PostLoadContext& context ) override;

	void EnsureGlassMaskUpdated( job::Builder& builder );

	virtual Uint32 GetDefinitionID() const override { return m_definitionID; }

	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;

	// Property is about to be changed
	virtual Bool OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue ) override final;

	// Property changed
	virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final;

	virtual const CName GetName() const override { return RED_NAME_CONSTEXPR( "MaterialInstance" ); }

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	virtual Bool Recompile( const Bool includesMatch, const Bool onlyHighPriority = false ) override;
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	virtual Bool ValidateParameters( red::StringBuilder< String >& errorMessageBuilder ) const;

	virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;

	virtual Bool IsMaskedMaterial() const override { return m_enableMask; }
public:
	//! Set base material
	void SetBaseMaterial( const THandle< IMaterial >& material );

	//! Clear all instanced parameters
	void ClearInstanceParameters();

	//! Clear given instanced parameter
	Bool ClearInstanceParameter( const CName& name );

	//! Is given parameter instanced in this instance ?
	Bool IsParameterInstanced( const CName& name ) const;

	//! Auto assign textures to this material instance ( crappy editor code )
	void AutoAssignTextures();

public:
	//! Get base material graph 
	virtual IMaterialDefinition* GetMaterialDefinition() override;

	//! Get base material graph 
	virtual const IMaterialDefinition* GetMaterialDefinition() const override;

	//! Write parameter data
	virtual Bool WriteParameterRaw( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) override;

	virtual Bool WriteParameterRaw_NoDefinitionCheck( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) override;

	//! Read parameter data
	virtual Bool ReadParameterRaw( const CName& name, const rtti::IType* dataType, void* data ) const override;

	virtual Bool ReadParameterRaw_NoBase( const CName& name, const rtti::IType* dataType, void* data ) const override;

	//! Material instance is masked if its definition is masked and masking has been enabled (m_enableMask).
	virtual Bool IsMasked() const override { return m_enableMask; }
	
	//! Set whether this instance should use masking (if available). Render resource needs to be recreated after a call to this. This
	//! function will _not_ force a recreate.
	RED_INLINE void SetMaskEnabled( Bool enable ) { m_enableMask = enable; }

	Bool UpdateParametersListBasedOnTemplate();

	//! Get the audio tag
	virtual CName GetAudioTag() const override;

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	static void RecompileMaterialsUsingTexture( ITexture* texture );
#endif // NO_RUNTIME_MATERIAL_COMPILATION

protected:
	// Create rendering resource for material
	virtual TRenderPtr<IRenderResource> CreateRenderResource() override;
	
	Bool ContainsParameterWithTheSameNameButDifferentType( const CName& name, const rtti::IType*, MaterialParameterInstance& outParam );
private:
	void RecreateParentResource();

	void CacheDefinitionID();

	void RemoveUnusedParameters();
};

