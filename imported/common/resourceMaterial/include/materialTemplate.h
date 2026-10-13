/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once 

#include "materialDefinition.h"
#include "materialMetaDefinition.h"


class CMaterialParameter;

typedef red::DynArray< THandle< CMaterialParameter > >		TMaterialParameters;

class RESOURCEMATERIAL_API CMaterialTemplate : public IMaterialDefinition
{
	friend class CMaterialTemplateFactory;

	RTTI_DECLARE_TYPE( CMaterialTemplate );
	IMPLEMENT_RESOURCE_INTERFACE( "remt", "mt", "Material template" );
	RED_BASE_CLASS( IMaterialDefinition );

public:
	CMaterialTemplate();
	virtual ~CMaterialTemplate() = default;

	RED_INLINE const TMaterialParameters& GetParameters( EMaterialShaderTarget target ) const { return m_parameters[ target ]; }

	RED_INLINE const MaterialTechniqueArray& GetTechniques() const { return m_techniques; }

	RED_INLINE void SetName( const CName name ) { m_name = name; }
	RED_INLINE void SetPriority( EMaterialPriority materialPriority ) { m_materialPriority = materialPriority; }
	RED_INLINE void SetMaterialType( ERenderMaterialType materialType ) { m_materialType = materialType; }
	RED_INLINE void SetIsValid( Bool isValid ) { m_isValid = isValid; }

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	virtual red::DateTime GetTimeStamp( const CName stagePassName, EMaterialShaderTarget target ) const override;

	virtual CName GetDisplayableName() const override { return m_name; }

	void SetTimeStamp( const CName stagePassName, EMaterialShaderTarget target, red::DateTime timestamp );
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	virtual const CName GetName() const override { return m_name; }

	virtual Bool IsSpeedTreeBillboard() const override { return m_isSpeedTreeBillboard; }

	virtual void GetPasses( Uint32 techniqueIndex, EngineStagePassDataCollection& stagePassCollection ) const override;
		
	virtual void GetTechniquesFeatures( red::DynArray<TechniqueFeatures> &outTechFeatures ) const override;

	virtual Uint32 GetNumTechniques() const override { return m_techniques.Size(); }

	virtual const TMaterialUsedParameterArray& GetUsedParameters( const EMaterialShaderTarget target ) const override { return m_usedParameters[ target ]; }

	virtual const TSamplerStateInfoArray& GetSamplerStates( const EMaterialShaderTarget target ) const override { return m_samplerStates[ target ]; }

	virtual EMaterialPriority GetPriority() const override { return m_materialPriority; }
	virtual ERenderMaterialType GetMaterialType() const override { return m_materialType; }

	virtual Bool ValidateParameters( red::StringBuilder< red::String >& error ) const override;

	virtual CName GetAudioTag() const override;

public:
	virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override;

	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue ) override;

	virtual Bool OnPropertyPreChange(const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue) override final;

	// Serialize material
	// Serialization
	virtual void OnPreSave( const PreSaveContext& context ) override;
	virtual void OnPostLoad( const PostLoadContext& context ) override;
	virtual const job::Counter* HACK_GetPostLoadWaitCounter() const override;

	//! Update list of parameters
	Bool UpdateParametersList( const MaterialMetaDefinition& materialMetaDefinition );

	Bool UpdateTechniques( const MaterialMetaDefinition& materialMetaDefinition );

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	virtual Bool Recompile( const Bool includesMatch, const Bool onlyHighPriority = false ) override;
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	virtual red::String GetShaderName() const override
	{
		const auto path = GetPath().ToStringView();
		return !path.Empty() ? red::paths::ExtractFileStem( path ) : String::EMPTY();
	}

	virtual Bool IsValid() const override { return m_isValid; }

	virtual Bool IsGlass() const override { return m_isGlass; }

public:
	//! Write material parameter data
	virtual Bool WriteParameterRaw( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) override;

	virtual Bool WriteParameterRaw_NoDefinitionCheck( const CName& name, const rtti::IType* dataType, const void* data, Bool recreateResource = true ) override;

	//! Read parameter data
	virtual Bool ReadParameterRaw( const CName& name, const rtti::IType* dataType, void* data ) const override;

	virtual Bool ReadParameterRaw_NoBase( const CName& name, const rtti::IType* dataType, void* data ) const override;
	
	//! Find graph parameter
	const CMaterialParameter* FindParameter( const CName& name ) const;

private:
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	Bool ShouldRecompile( const Bool includesMatch ) const;

	Bool RecompileSynchronously( const Bool includesMatch );
#endif // NO_RUNTIME_MATERIAL_COMPILATION

protected:
	TMaterialParameters			m_parameters[ MSH_MAX ] = { rend::PoolRenderData(), rend::PoolRenderData(), rend::PoolRenderData() };
	TMaterialUsedParameterArray	m_usedParameters[ MSH_MAX ] = { rend::PoolRenderData(), rend::PoolRenderData(), rend::PoolRenderData() };
	TSamplerStateInfoArray		m_samplerStates[ MSH_MAX ] = { rend::PoolRenderData(), rend::PoolRenderData(), rend::PoolRenderData() };
	MaterialTechniqueArray		m_techniques = { rend::PoolRenderData() };
	CName						m_audioTag;			//!< Support for audio related features (e.g. indicating what sound to play when stepping on puddles)
	CName						m_name;
	job::Counter				m_postloadCounter;
	Uint8						m_resourceVersion { 0 };
	EMaterialPriority			m_materialPriority;
	ERenderMaterialType			m_materialType;

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	StagePassFileTimesMap		m_fileTimes;
#endif // NO_RUNTIME_MATERIAL_COMPILATION

	Bool						m_isValid : 1;
	Bool						m_isGlass : 1;
	Bool						m_isSpeedTreeBillboard : 1;
};
