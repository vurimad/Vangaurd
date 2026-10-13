/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderBackendApi.h"

#include "materialLibrary.h"

#include "../../../common/renderData/include/materialStructures.h"
#include "../../../common/renderData/include/shaderCompilerLogger.h"
#include "../../../common/resourceMaterial/include/materialMetaDefinition.h"
#include "../../../common/resource/include/xmlDocument.h"
#include "../../../common/renderBackend/include/shaderMetaLoader.h"


class RENDERBACKEND_API CMaterialMetaDefinitionLoader
{
public:
	enum LoaderStatus
	{
		Ok = 0,
		MaterialLibraryMissing,
		MetadataFileDoesNotExits,
		MetadataFileParsingFailed,
		InvalidMetadataFile,
		InvalidResource,
		MaterialExcludedFromFinalBuild,
	};

	class RENDERBACKEND_API Logger : public IShaderCompilerLogger
	{
	public:
		Logger( const CName name, Bool verbose = false );

	public:
		virtual void Log( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;
		virtual void LogAlways( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;
		virtual void LogSpam( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;
		virtual void Warn( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;
		virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;

	private:
		void VLog( const AnsiChar* txt, va_list args );

#ifndef RED_CONFIGURATION_FINAL
		CName				m_name;
		red::AbsolutePath	m_fxMetaPath;
		Bool				m_verbose;

		static const Int32	s_lenShort	= 4096;
#endif // RED_CONFIGURATION_FINAL
	};

public:
	CMaterialMetaDefinitionLoader( const Bool isFinalBuild, const CMaterialLibrary* materialLibrary, Logger& logger );

	LoaderStatus Load( const CName name, MaterialMetaDefinition& metaDefinition );

private:
	LoaderStatus LoadMetaDefinition( const xml::Document* doc, const xml::Document::Node* shaderMetadataNode, MaterialMetaDefinition& metaDefinition );

	Bool LoadRenderingParameters( const xml::Document* doc, const xml::Document::Node* renderingParamsNode, MaterialMetaDefinition& metaDefinition );

	Bool LoadOutputMergerInclude( const String& name, MaterialMetaDefinition& metaDefinition);

	Bool LoadOutputMergerStates( const xml::Document* doc, const xml::Document::Node* omStatesNode, MaterialMetaDefinition& metaDefinition, Bool isLoadingOMStatesLibrary = false, Bool isLoadingInclude = false );

	Bool LoadTechniquesInclude( const String& name, MaterialMetaDefinition& metaDefinition );

	Bool LoadTechniques( const xml::Document* doc, const xml::Document::Node* techniquesNode, MaterialMetaDefinition& metaDefinition, Bool isLoadingLibrary = false );

	Bool LoadTechniquePasses( const xml::Document* doc, const xml::Document::Node* techniqueNode, MaterialMetaDefinition& metaDefinition, MaterialTechnique& technique, const MaterialPass& templatePass );

	Bool LoadStreamsToBind( const xml::Document* doc, const xml::Document::Node* streamsToBindNode, MaterialTechnique& technique, Bool matchingTechniqueExists );

	Bool LoadFeatureFlags( const xml::Document* doc, const xml::Document::Node* featureFlagsNode, MaterialTechnique& technique, Bool matchingTechniqueExists );

	Bool LoadShaderSources( const xml::Document* doc, const xml::Document::Node* shadersNode, MaterialPass& pass, const CName defaultName );

	Bool LoadStencilValues( const xml::Document* doc, const xml::Document::Node* stencilNode, MaterialPass& pass, Bool matchingTechniqueFound );

	Bool LoadShaderDefines( const xml::Document* doc, const xml::Document::Node* shaderDefinesNode, MaterialPass& pass );

	Bool LoadParameters( const xml::Document* doc, const xml::Document::Node* parametersNode, MaterialMetaDefinition& metaDefinition );

	Bool LoadSamplerStates( const xml::Document* doc, const xml::Document::Node* samplerStatesNode, MaterialMetaDefinition& metaDefinition );

	Bool LoadShaderTargets( const xml::Document* doc, const xml::Document::Node* shaderTargetsNode, Int32& targets );

	void LoadSamplerStateAddress( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState );

	void LoadSamplerStateTextureFiltering( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState );

	void LoadSamplerStateAdditionalInfo( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState );

	template< typename T >
	Bool TryParseValue( const xml::Document* doc, const xml::Document::Node* node, const red::String& fieldName, T& value, const T defaultValue )
	{
		value = defaultValue;

		red::String fieldValue;
		const xml::Document::Attribute* attribute = doc->GetFirstAttribute( node, fieldName.AsChar() );
		if ( attribute )
		{
			fieldValue = doc->GetAttributeValue( attribute );
			auto* field = static_cast< const rtti::EnumType* >( GetTypeObject<T>() );
			if ( field )
			{
				Int64 val;
				if ( field->FindValue( RED_NAME( fieldValue ), val ) )
				{
					value = static_cast< T >( val );
					return true;
				}
			}
		}

		return false;
	}

	Bool					m_isFinalBuild;
	Logger&					m_logger;
	const CMaterialLibrary* m_materialLibrary;
	OMStateLoader			m_omStateLoader;
};
