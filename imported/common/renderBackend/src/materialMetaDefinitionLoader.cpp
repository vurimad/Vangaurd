/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialMetaDefinitionLoader.h"
#include "../../renderData/include/renderPublicEnums.h"

using red::DynArray;
using red::String;
using red::StringView;
using namespace PSODesc;

#define PARSE_MANDATORY_PARAMETER( doc, node, ParamType, ParamName, OutVariable, DefaultValue )	\
	{																							\
		ParamType tempValue;																	\
		if ( !TryParseValue< ParamType >( doc, node, ParamName, tempValue, DefaultValue ) )		\
		{																						\
			String fullNodePath = doc->GetNodeName( node );										\
			m_logger.Error( "Parsing Parameter \"%hs\" FAILED", fullNodePath.AsChar() );		\
			return false;																		\
		}																						\
		OutVariable = tempValue;																\
	}

#define PARSE_OPTIONAL_PARAMETER( doc, node, ParamType, ParamName, OutVariable, DefaultValue )	\
	{																							\
		ParamType tempValue;																	\
		TryParseValue< ParamType >( doc, node, ParamName, tempValue, DefaultValue );			\
		OutVariable = tempValue;																\
	}


namespace helpers
{
	RED_INLINE Bool ReadAttribute( const xml::Document* doc, const xml::Document::Node* node, const AnsiChar* attrName, String& attrValue )
	{
		const xml::Document::Attribute* attribute = doc->GetFirstAttribute( node, attrName );
		if ( attribute )
		{
			attrValue = doc->GetAttributeValue( attribute );
			return true;
		}
		return false;
	}
		
	template< class T >
	RED_INLINE Bool ReadAttributeT( const xml::Document* doc, const xml::Document::Node* node, const AnsiChar* attrName, T& value, T defaultValue )
	{
		value = defaultValue;
		String str;
		if ( ReadAttribute( doc, node, attrName, str ) )
		{
			return FromString( str.AsChar(), value );
		}
		return false;
	}
}


#define PARSE_MANDATORY_ATTRIBUTE( doc, node, AttributeType, AttributeName, OutVariable, DefaultValue )				\
{																													\
	if ( !helpers::ReadAttributeT< AttributeType >( doc, node, AttributeName, OutVariable, DefaultValue ) )			\
	{																												\
		m_logger.Error( "Parsing Attribute '%hs' of node '%hs' FAILED", AttributeName, doc->GetNodeName( node ) );	\
		return false;																								\
	}																												\
}

#define PARSE_OPTIONAL_ATTRIBUTE( doc, node, AttributeType, AttributeName, OutVariable, DefaultValue )	\
	helpers::ReadAttributeT< AttributeType >( doc, node, AttributeName, OutVariable, DefaultValue )

//////////////////////////////////////////////////////////////////////////

CMaterialMetaDefinitionLoader::Logger::Logger( const CName name, Bool verbose /*=false*/ )
#ifndef RED_CONFIGURATION_FINAL
	: m_name( name )
	, m_verbose( verbose )
#endif // RED_CONFIGURATION_FINAL
{
}


void CMaterialMetaDefinitionLoader::Logger::Log( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
{
#ifndef RED_CONFIGURATION_FINAL
	if ( m_verbose )
	{
		va_list args;
		va_start( args, txt );
		VLog( txt, args );
		va_end( args );
	}
#endif // RED_CONFIGURATION_FINAL
}

void CMaterialMetaDefinitionLoader::Logger::LogAlways( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
{
#ifndef RED_CONFIGURATION_FINAL
	va_list args;
	va_start( args, txt );
	VLog( txt, args );
	va_end( args );
#endif // RED_CONFIGURATION_FINAL
}

void CMaterialMetaDefinitionLoader::Logger::LogSpam( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
{
#ifndef RED_CONFIGURATION_FINAL
	if ( m_verbose )
	{
		va_list args;
		va_start( args, txt );
		VLog( txt, args );
		va_end( args );
	}
#endif // RED_CONFIGURATION_FINAL
}

void CMaterialMetaDefinitionLoader::Logger::VLog( const AnsiChar* txt, va_list args )
{
#ifndef RED_CONFIGURATION_FINAL
	AnsiChar buffer[s_lenShort];
	sprintf_s( buffer, s_lenShort, "FXMetaLoader('%hs', '%hs'): ", m_name.AsChar(), m_fxMetaPath.AsChar() );
	const size_t length = red::Strlen( buffer );

	red::VSPrintF( buffer + length, s_lenShort - length, txt, args );

	RED_LOG( buffer );
#endif // RED_CONFIGURATION_FINAL
}


void CMaterialMetaDefinitionLoader::Logger::Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
{
#ifndef RED_CONFIGURATION_FINAL
	AnsiChar buffer[ s_lenShort ];
	sprintf_s( buffer, s_lenShort, "FXMetaLoader('%hs', '%hs'): ", m_name.AsChar(), m_fxMetaPath.AsChar() );
	const size_t length = red::Strlen(buffer);

	va_list args;
	va_start( args, txt );
	red::VSPrintF( buffer + length, s_lenShort - length, txt, args );
	va_end( args );

	RED_LOG_ERROR( buffer );
#endif // RED_CONFIGURATION_FINAL
}

void CMaterialMetaDefinitionLoader::Logger::Warn( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
{
#ifndef RED_CONFIGURATION_FINAL
	AnsiChar buffer[ s_lenShort ];
	sprintf_s( buffer, s_lenShort, "FXMetaLoader('%hs', '%hs'): ", m_name.AsChar(), m_fxMetaPath.AsChar() );
	const size_t length = red::Strlen(buffer);

	va_list args;
	va_start( args, txt );
	red::VSNPrintF(buffer + length, s_lenShort - length, txt, args);
	va_end( args );

	RED_LOG_WARNING( buffer );
#endif // RED_CONFIGURATION_FINAL
}

//////////////////////////////////////////////////////////////////////////

CMaterialMetaDefinitionLoader::CMaterialMetaDefinitionLoader( const Bool isFinalBuild, const CMaterialLibrary* materialLibrary, Logger& logger )
	: m_isFinalBuild( isFinalBuild )
	, m_materialLibrary( materialLibrary )
	, m_logger( logger )
{
}

CMaterialMetaDefinitionLoader::LoaderStatus CMaterialMetaDefinitionLoader::Load( const CName name, MaterialMetaDefinition& metaDefinition )
{
	if ( !m_materialLibrary )
	{
		return CMaterialMetaDefinitionLoader::MaterialLibraryMissing;
	}

	String meta;
	if ( !m_materialLibrary->GetMeta( name, meta ) )
	{
		return CMaterialMetaDefinitionLoader::MetadataFileDoesNotExits;
	}

	Uint64 crc = red::CalculateHash64( meta.Data(), meta.Length() );

	red::UniquePtr< xml::Document > doc( xml::Document::Create( meta.AsChar() ) );
	if ( !doc )
	{
		m_logger.Error( "Document not found." );
		return CMaterialMetaDefinitionLoader::InvalidMetadataFile;
	}

	m_omStateLoader.Initialize( doc.Get(), m_logger );

	CMaterialMetaDefinitionLoader::LoaderStatus loadResult = LoadMetaDefinition( doc.Get(), doc->GetRootNode(), metaDefinition );
	RED_ERROR_ONCE( loadResult == LoaderStatus::Ok, "Material metadata load failed for material %hs", name.AsChar() );
	metaDefinition.m_crc = crc;
	return loadResult;
}

CMaterialMetaDefinitionLoader::LoaderStatus CMaterialMetaDefinitionLoader::LoadMetaDefinition( const xml::Document* doc, const xml::Document::Node* shaderMetadataNode, MaterialMetaDefinition& metaDefinition )
{
	RED_ASSERT( doc, "Empty XML Document" );
	if ( !shaderMetadataNode )
	{
		m_logger.Error( "Root node <ShaderMetadata> not found." );
		return LoaderStatus::MetadataFileParsingFailed;
	}

	PARSE_OPTIONAL_ATTRIBUTE( doc, shaderMetadataNode, Bool, "IsFallback", metaDefinition.m_isFallback, false ); // disable by default
	PARSE_OPTIONAL_ATTRIBUTE( doc, shaderMetadataNode, Bool, "UseShaderModel51", metaDefinition.m_useShaderModel51, false ); // disable by default

	Bool excludeFromFinal = false;
	PARSE_OPTIONAL_ATTRIBUTE( doc, shaderMetadataNode, Bool, "ExcludeOnFinal", excludeFromFinal, false ); // disable by default

	if ( excludeFromFinal && m_isFinalBuild )
	{
		m_logger.Error( "Material is excluded from final build. Skipping..." );
		return LoaderStatus::MaterialExcludedFromFinalBuild;
	}

	if ( !LoadRenderingParameters( doc, doc->GetFirstNode( shaderMetadataNode, "RenderingParameters" ), metaDefinition ) )
	{
		m_logger.Error( "LoadRederingParameters FAILED." );
		return LoaderStatus::MetadataFileParsingFailed;
	}
	m_logger.Log( "LoadRederingParameters SUCCEEDED." );

	if ( !LoadOutputMergerStates( doc, doc->GetFirstNode( shaderMetadataNode, "PSOStates" ), metaDefinition, true ) )
	{
		m_logger.Error( "Loading Output-Merger States FAILED." );
		return LoaderStatus::MetadataFileParsingFailed;
	}
	m_logger.Log( "Loading Output-Merger States SUCCEEDED." );

	if ( !LoadTechniques( doc, doc->GetFirstNode( shaderMetadataNode, "Techniques" ), metaDefinition ) )
	{
		m_logger.Error( "Loading Techniques FAILED." );
		return LoaderStatus::MetadataFileParsingFailed;
	}
	m_logger.Log( "Loading Techniques SUCCEEDED." );

	if ( !LoadParameters( doc, doc->GetFirstNode( shaderMetadataNode, "Parameters" ), metaDefinition ) )
	{
		m_logger.Error( "Loading Parameters FAILED." );
		return LoaderStatus::MetadataFileParsingFailed;
	}
	m_logger.Log( "Loading Parameters SUCCEEDED." );

	if ( !LoadSamplerStates( doc, doc->GetFirstNode( shaderMetadataNode, "SamplerStates" ), metaDefinition ) )
	{
		m_logger.Error( "Loading SamplerStates FAILED." );
		return LoaderStatus::MetadataFileParsingFailed;
	}
	m_logger.Log( "Loading SamplerStates SUCCEEDED." );

	return LoaderStatus::Ok;
}

Bool CMaterialMetaDefinitionLoader::LoadRenderingParameters( const xml::Document* doc, const xml::Document::Node* renderingParamsNode, MaterialMetaDefinition& metaDefinition )
{
	metaDefinition.m_materialPriority	= EMP_Normal;
	metaDefinition.m_materialType		= RMT_Standard;

	if ( renderingParamsNode )
	{
		PARSE_OPTIONAL_PARAMETER( doc, renderingParamsNode, EMaterialPriority, "MaterialPriority", metaDefinition.m_materialPriority, EMP_Normal ); // disable by default
		PARSE_OPTIONAL_PARAMETER( doc, renderingParamsNode, ERenderMaterialType, "MaterialType", metaDefinition.m_materialType, RMT_Standard ); // disable by default
	}
	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadOutputMergerInclude( const String& name, MaterialMetaDefinition& metaDefinition )
{
	String includeContent;
	if ( !m_materialLibrary || !m_materialLibrary->GetMetaInclude( name, includeContent ) )
	{
		return false;
	}

	red::UniquePtr< xml::Document > includeDoc( xml::Document::Create( includeContent.AsChar() ) );
	if ( !includeDoc )
	{
		m_logger.Error( "Include Document not found." );
		return false;
	}

	const xml::Document::Node* rootNode = includeDoc->GetRootNode();
	if ( !rootNode )
	{
		m_logger.Error( "Error parsing Include Document" );
		return false;
	}

	const xml::Document::Node* omStatesNode = includeDoc->GetFirstNode( rootNode, "PSOStates" );
	Bool loadResult = LoadOutputMergerStates( includeDoc.Get(), omStatesNode, metaDefinition, true, true );
	RED_ERROR_ONCE( loadResult, "Material metadata include load failed for include %hs", name.AsChar() );
	return loadResult;
}

Bool CMaterialMetaDefinitionLoader::LoadOutputMergerStates( const xml::Document* doc, const xml::Document::Node* omStatesNode, MaterialMetaDefinition& metaDefinition, Bool isLoadingOMStatesLibrary /*=false*/, Bool isLoadingInclude /*=false*/ )
{
	if ( !omStatesNode )
	{
		m_logger.Log( "No Output-Merger States in current meta file." );
		// it can be empty
		return true;
	}

	if ( const xml::Document::Attribute* includeAttrib = doc->GetFirstAttribute( omStatesNode, "Include" ) )
	{
		const AnsiChar* includeAttribName = doc->GetAttributeValue( includeAttrib );
		if ( !LoadOutputMergerInclude( includeAttribName, metaDefinition ) )
		{
			m_logger.Error( "Failed to load ShaderMeta Include '%hs'.", includeAttribName );
			return false;
		}
	}

	if ( !m_omStateLoader.LoadBases( omStatesNode, false ) )
	{
		m_logger.Error( "Failed to load OMState bases" );
		return false;
	}

	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadTechniquesInclude( const String& name, MaterialMetaDefinition& metaDefinition )
{
	String includeContent;
	if (!m_materialLibrary || !m_materialLibrary->GetMetaInclude(name, includeContent))
	{
		return false;
	}

	red::UniquePtr< xml::Document > includeDoc(xml::Document::Create(includeContent.AsChar()));
	if (!includeDoc)
	{
		m_logger.Error("Include Document not found.");
		return false;
	}

	const xml::Document::Node* rootNode = includeDoc->GetRootNode();
	if (!rootNode)
	{
		m_logger.Error("Error parsing Include Document");
		return false;
	}

	const xml::Document::Node* techniquesNode = includeDoc->GetFirstNode(rootNode, "Techniques");
	if (!techniquesNode)
	{
		// We are not interested in .fxmetaincludes that do not have techniques defined.
		return true;
	}

	Bool loadResult = LoadTechniques( includeDoc.Get(), techniquesNode, metaDefinition, true );
	RED_ERROR_ONCE( loadResult, "Material metadata include load failed for include %hs", name.AsChar() );
	return loadResult;
}

Bool CMaterialMetaDefinitionLoader::LoadTechniquePasses( const xml::Document* doc, const xml::Document::Node* techniqueNode, MaterialMetaDefinition& metaDefinition, MaterialTechnique& technique, const MaterialPass& templatePass )
{
	for ( xml::NodeIterator passesIterator( *doc, techniqueNode, "Pass" ); passesIterator; ++passesIterator )
	{
		const xml::Document::Node* passNode = passesIterator.GetCurrent();

		MaterialPass pass( metaDefinition.m_name );

		String statePassName;
		PARSE_MANDATORY_ATTRIBUTE(	doc, passNode, String, "StagePassName", statePassName, "Default" );

		pass.m_stagePassNameRegular = RED_NAME( statePassName );

		// Find if pass already exists in the technique.
		const CName passName = pass.m_stagePassNameRegular;
		const auto matchedPass = std::find_if( technique.m_passes.Begin(), technique.m_passes.End(), [ passName ]( const MaterialPass& obj ) { return obj.m_stagePassNameRegular == passName; } );
		const Bool isPassMatched = ( matchedPass != technique.m_passes.End() );
		if ( isPassMatched )
		{
			pass = *matchedPass;
		}

		PARSE_OPTIONAL_ATTRIBUTE(	doc, passNode, String, "StagePassNameDiscarded", statePassName, isPassMatched ? pass.m_stagePassNameDiscarded.AsChar() : pass.m_stagePassNameRegular.AsChar() );
		pass.m_stagePassNameDiscarded = RED_NAME( statePassName );

		PARSE_OPTIONAL_ATTRIBUTE(	doc, passNode, Bool,   "EnablePixelShader",      pass.m_enablePixelShader,      isPassMatched ? pass.m_enablePixelShader      : true ); 
		
		const xml::Document::Node* shadersNode = doc->GetFirstNode( passNode, "Shaders" );
		if ( !LoadShaderSources( doc, shadersNode, pass, metaDefinition.m_name ) )
		{
			m_logger.Error( "LoadShaderSources FAILED." );
			return false;
		}

		const xml::Document::Node* dsNode = doc->GetFirstNode( passNode, "DepthStencilMode" );
		if ( !m_omStateLoader.LoadDepthStencilMode( dsNode, pass.m_depthStencilMode ) )
		{
			m_logger.Error( "LoadDepthStencilMode FAILED." );
			return false;
		}

		const xml::Document::Node* rsNode = doc->GetFirstNode( passNode, "RasterizerMode" );
		if ( !m_omStateLoader.LoadRasterizerMode( rsNode, pass.m_rasterizerMode ) )
		{
			m_logger.Error( "LoadRasterizerMode FAILED." );
			return false;
		}

		const xml::Document::Node* bmNode = doc->GetFirstNode( passNode, "BlendMode" );
		if ( !m_omStateLoader.LoadBlendMode( bmNode, pass.m_blendMode ) )
		{
			m_logger.Error( "LoadBlendMode FAILED." );
			return false;
		}

		const xml::Document::Node* stencilNode = doc->GetFirstNode( passNode, "Stencil" );
		if ( !LoadStencilValues( doc, stencilNode, pass, isPassMatched) )
		{
			m_logger.Error( "LoadStencilValues FAILED." );
			return false;
		}

		const xml::Document::Node* shaderDefinesNode = doc->GetFirstNode( passNode, "ShaderDefines" );
		if ( !LoadShaderDefines( doc, shaderDefinesNode, pass ) )
		{
			m_logger.Error( "LoadShaderDefines FAILED." );
			return false;
		}
		
		// If the pass is not matched it will be pushed-back into the array of passes.
		// Since index in the array corresponds to the orderIndex of particular pass
		// we assign it beforehand. Passes that are matched already have their indices in place.
		if ( !isPassMatched )
		{
			pass.m_orderIndex = technique.m_passes.Size();
		}
		
		isPassMatched ? *matchedPass = pass : technique.m_passes.PushBack( pass );
	}

	// update shader sources in all passes
	// NOTE: pass from library can have overriden sources!!! Do not update paths in that case
	
	String defaultPath = String::Printf( "%hs.fx", metaDefinition.m_name.AsChar() );
	for ( MaterialPass& pass : technique.m_passes )
	{
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( pass.m_shaderSourcePath[ target ] == defaultPath )
			{
				pass.m_shaderSourcePath[ target ] = templatePass.m_shaderSourcePath[ target ];
			}
		}
	}
	
	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadStreamsToBind( const xml::Document* doc, const xml::Document::Node* streamsToBindNode, MaterialTechnique& technique, Bool matchingTechniqueExists )
{
	if ( !streamsToBindNode )
	{
		if ( matchingTechniqueExists )
		{
			return true;
		}

		m_logger.Error("Tag <StreamsToBind> not found");
		return false;
	}

	Uint32 streamsToBind = matchingTechniqueExists ? technique.m_streamsToBind : 0;
	for ( xml::NodeIterator streamsIterator( *doc, streamsToBindNode, "Stream" ); streamsIterator; ++streamsIterator )
	{
		Uint32 stream;
		PARSE_MANDATORY_PARAMETER( doc, streamsIterator.GetCurrent(), ERenderMeshStreams, "Type", stream, RMS_BindAll );
		streamsToBind |= stream;
	}
	technique.m_streamsToBind = streamsToBind;

	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadFeatureFlags( const xml::Document* doc, const xml::Document::Node* featureFlagsNode, MaterialTechnique& technique, Bool matchingTechniqueExists )
{
	if ( !featureFlagsNode )
	{
		if ( matchingTechniqueExists )
		{
			return true;
		}

		m_logger.Error( "Tag <FeatureFlags> not found!" );
		return false;
	}

	FeatureFlagsMask enabledMask;
	if ( matchingTechniqueExists ) enabledMask = technique.m_featureFlagsEnabledMask;
	for ( xml::NodeIterator flagsIterator( *doc, featureFlagsNode, "Flag" ); flagsIterator; ++flagsIterator )
	{
		EFeatureFlag flag;
		PARSE_MANDATORY_PARAMETER( doc, flagsIterator.GetCurrent(), EFeatureFlag, "Type", flag, FEATFLAG_MAX );
		RED_ASSERT( flag < (Uint32)FEATFLAG_MAX );
		enabledMask.Set( flag );
	}

	if ( !enabledMask.IsAnySet() )
	{
		m_logger.Error( "Enabled feature flags can't be empty" );
		return false;
	}
	
	technique.m_featureFlagsEnabledMask = enabledMask;
	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadShaderSources( const xml::Document* doc, const xml::Document::Node* shadersNode, MaterialPass& pass, const CName defaultName )
{
	if ( shadersNode )
	{
		for ( xml::NodeIterator shadersIterator( *doc, shadersNode, "Shader" ); shadersIterator; ++shadersIterator )
		{
			const xml::Document::Node* shaderNode = shadersIterator.GetCurrent();

			String shaderSource;
			PARSE_MANDATORY_ATTRIBUTE( doc, shaderNode, String, "Src", shaderSource, "default.fx" );

			const xml::Document::Node* shaderTargetsNode = doc->GetFirstNode( shaderNode, "Targets" );
			Int32 targets = MSH_PixelShader | MSH_VertexShader;
			LoadShaderTargets( doc, shaderTargetsNode, targets );

			for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
			{
				if ( targets & target )
				{
					pass.m_shaderSourcePath[ target ] = shaderSource;
				}
			}
		}
	}
	return true;
}

template< typename T, typename TMap >
Bool GetBaseDesc( const xml::Document* doc, const xml::Document::Node* node, const T& defaultDesc, const TMap& descMap, T& outDesc, String& name, Bool isLoadingOMStatesLibrary, Bool isLoadingInclude, CMaterialMetaDefinitionLoader::Logger& logger )
{
	outDesc = defaultDesc;
	if ( isLoadingOMStatesLibrary )
	{
		PARSE_OPTIONAL_ATTRIBUTE( doc, node, String, "Name", name, "" );		
		if ( name.Empty() )
		{
			logger.Error( "Node cannot have empty name." );
			return false;
		}
		if ( descMap.KeyExist( name ) )
		{
			if ( isLoadingInclude )
			{
				logger.Error( "'%hs' already exists - overriding not allowed.", name.AsChar() );
				return false;
			}
			else
			{
				logger.Warn( "'%hs' already exists - it will be overridden.", name.AsChar() );
				outDesc = descMap[ name ];
			}
		}
	}
	else
	{
		if ( PARSE_OPTIONAL_ATTRIBUTE( doc, node, String, "Base", name, "" ) )
		{
			if ( descMap.KeyExist( name ) )
			{
				outDesc = descMap[ name ];
			}
			else
			{
				logger.Error( "Could not find with name '%hs' in library - check .fxmeta file", name.AsChar() );
			}
		}
	}

	return true;
}

template< typename T, typename TMap >
Bool StoreDesc( const T& desc, String& name, TMap& descMap, Bool isLoadingOMStatesLibrary, Bool isLoadingInclude, CMaterialMetaDefinitionLoader::Logger& logger )
{
	if ( isLoadingOMStatesLibrary )
	{
		if ( descMap.KeyExist( name ) )
		{
			if ( isLoadingInclude )
			{
				logger.Error( "Duplicate RenderStateMode '%hs' in fxmeta include file.", name );
				return false;
			}
			descMap[ name ] = desc;
		}
		else
		{
			descMap.Insert( name, desc );
		}
	}
	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadTechniques( const xml::Document* doc, const xml::Document::Node* techniquesNode, MaterialMetaDefinition& metaDefinition, Bool isLoadingLibrary )
{
	if ( !techniquesNode )
	{
		m_logger.Error("Tag <Techniques> not found");
		return false;
	}

	if ( const xml::Document::Attribute* includeAttrib = doc->GetFirstAttribute( techniquesNode, "Include" ) )
	{
		const AnsiChar* includeAttribName = doc->GetAttributeValue( includeAttrib );
		if ( !LoadTechniquesInclude( includeAttribName, metaDefinition ) )
		{
			m_logger.Error( "Failed to load ShaderMeta Include '%hs'.", includeAttribName );
			return false;
		}
	}

	MaterialPass tempPass( metaDefinition.m_name );
	const xml::Document::Node* shadersNode = doc->GetFirstNode( techniquesNode, "Shaders" );
	if ( !LoadShaderSources( doc, shadersNode, tempPass, metaDefinition.m_name ) )
	{
		m_logger.Error( "Loading Shader Source for Techniques globally FAILED." );
		return false;
	}

	for ( xml::NodeIterator techniquesIterator( *doc, techniquesNode, "Technique" ); techniquesIterator; ++techniquesIterator )
	{
		const xml::Document::Node* techniqueNode = techniquesIterator.GetCurrent();
		RED_ASSERT( techniqueNode, "Invalid node." );

		MaterialTechnique technique;
		String name;
		if ( !GetBaseDesc< MaterialTechnique, MaterialMetaDefinition::TechniquesLibrary >( doc, techniqueNode, technique, metaDefinition.m_techniquesLibrary, technique, name, isLoadingLibrary, false, m_logger ) )
		{
			return false;
		}
		const Bool matchingTechniqueExists = metaDefinition.m_techniquesLibrary.KeyExist( name );

		const xml::Document::Node* featureFlagsNode = doc->GetFirstNode( techniqueNode, "FeatureFlags" );
		if ( !LoadFeatureFlags( doc, featureFlagsNode, technique, matchingTechniqueExists ) )
		{
			m_logger.Error("Loading FeatureFlags FAILED.");
			return false;
		}

		if ( !LoadTechniquePasses( doc, techniqueNode, metaDefinition, technique, tempPass ) )
		{
			m_logger.Error("Loading TechniquePasses FAILED.");
			return false;
		}

		const xml::Document::Node* streamsToBindNode = doc->GetFirstNode( techniqueNode, "StreamsToBind" );
		if ( !LoadStreamsToBind( doc, streamsToBindNode, technique, matchingTechniqueExists ) )
		{
			m_logger.Error("Loading StreamsToBind FAILED.");
			return false;
		}

		PARSE_OPTIONAL_ATTRIBUTE( doc, techniqueNode, Bool, "ExcludeOnFinal", technique.m_excludeOnFinal, isLoadingLibrary ? false : technique.m_excludeOnFinal );

		if ( !StoreDesc< MaterialTechnique, MaterialMetaDefinition::TechniquesLibrary >( technique, name, metaDefinition.m_techniquesLibrary, isLoadingLibrary, false, m_logger ) )
		{
			return false;
		}
		
		if( !isLoadingLibrary )
		{
			metaDefinition.m_techniques.PushBack( technique );
		}
	}

	return true;
}


Bool CMaterialMetaDefinitionLoader::LoadStencilValues( const xml::Document* doc, const xml::Document::Node* stencilNode, MaterialPass& pass, Bool matchingTechniqueFound )
{
	if ( !stencilNode )
	{
		if ( matchingTechniqueFound )
		{
			return true;
		}

		m_logger.Error("Missing <Stencil> tag.");
		return false;
	}

	// If exists, default values to already defined technique.
	PARSE_OPTIONAL_ATTRIBUTE( doc, stencilNode, Uint8, "ReadMask", 	pass.m_stencilReadMask,		matchingTechniqueFound ? pass.m_stencilReadMask  : 0 );
	PARSE_OPTIONAL_ATTRIBUTE( doc, stencilNode, Uint8, "WriteMask",	pass.m_stencilWriteMask,    matchingTechniqueFound ? pass.m_stencilWriteMask : 0 );
	PARSE_OPTIONAL_ATTRIBUTE( doc, stencilNode, Uint8, "Ref",		pass.m_stencilRef,          matchingTechniqueFound ? pass.m_stencilRef       : 0 );

	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadShaderDefines( const xml::Document* doc, const xml::Document::Node* shaderDefinesNode, MaterialPass& pass )
{
	if ( !shaderDefinesNode )
	{
		// it's possible to have no defines at all
		m_logger.Log( "No Shader Defines." );
		return true;
	}

	for ( xml::NodeIterator definesIterator( *doc, shaderDefinesNode, "Define" ); definesIterator; ++definesIterator )
	{
		const xml::Document::Node* defineNode = definesIterator.GetCurrent();

		ShaderDefine define;

		PARSE_MANDATORY_ATTRIBUTE( doc, defineNode, String, "Name", define.m_name, "" );
		PARSE_MANDATORY_ATTRIBUTE( doc, defineNode, String, "Value", define.m_value, "" );

		const xml::Document::Node* targetsNode = doc->GetFirstNode( defineNode, "Targets" );

		Int32 targets = 0;
		if ( !LoadShaderTargets( doc, targetsNode, targets ) )
		{
			m_logger.Log( "Failed to load ShaderTargets for ShaderDefine - it will be assigned to VertexShader and PixelShader." );
			targets = MSH_VertexShader | MSH_PixelShader;
		}

		// push define to appropriate target
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( targets & target )
			{
				// If any define already resides within a given target, check whether
				// there is a matching of its name and override its value if needed.
				Bool isOverriden = false;
				for ( auto& existingDefine : pass.m_shaderDefines[target] )
				{
					if (existingDefine.m_name == define.m_name)
					{
						existingDefine.m_value = define.m_value;
						isOverriden = true;
					}
				}

				// If the target either does not have any define or no define matches
				// the one that we are considering, just push it.
				if ( !isOverriden )
				{
					pass.m_shaderDefines[target].PushBack(define);
				}

			}
		}
	}
	return true;
}


const char* GetOverrideParameterName( IMaterialDataProviderDesc::EParameterType paramType )
{
	static_assert( IMaterialDataProviderDesc::PT_MAX == 17, "ParameterType enum changed" );
	switch ( paramType )
	{
	case IMaterialDataProviderDesc::PT_SkinProfile:			return "SkinProfile";
	case IMaterialDataProviderDesc::PT_HairProfile:			return "HairProfile";
	case IMaterialDataProviderDesc::PT_FoliageProfile:		return "FoliageProfile";
	case IMaterialDataProviderDesc::PT_MultilayerSetup:		return "MultilayerSetup";
	case IMaterialDataProviderDesc::PT_MultilayerMask:		return "MultilayerMask";
	case IMaterialDataProviderDesc::PT_TerrainSetup:        return "TerrainSetup";
	default:
		return nullptr;
	}
}

Bool CMaterialMetaDefinitionLoader::LoadParameters( const xml::Document* doc, const xml::Document::Node* parametersNode, MaterialMetaDefinition& metaDefinition )
{
	if ( !parametersNode )
	{
		m_logger.Log( "Missing <Parameters> tag - no parameters will be read." );
		return true;
	}

	Uint8 textureRegisters[ MSH_MAX ];
	red::Memzero( &textureRegisters, sizeof( textureRegisters ) );
	Uint8 nonTextureRegisters[ MSH_MAX ];
	red::Memzero( &nonTextureRegisters, sizeof( nonTextureRegisters ) );
	Uint8 dummyRegisters[ MSH_MAX ];
	red::Memzero( &dummyRegisters, sizeof( dummyRegisters ) );

	for ( xml::NodeIterator parametersIterator( *doc, parametersNode, "Parameter" ); parametersIterator; ++parametersIterator )
	{
		const xml::Document::Node* parameterNode = parametersIterator.GetCurrent();

		IMaterialDataProviderDesc::EParameterType type;
		PARSE_MANDATORY_PARAMETER( doc, parameterNode, IMaterialDataProviderDesc::EParameterType, "Type", type, IMaterialDataProviderDesc::PT_None );
		
		String name;
		if ( const char* overrideName = GetOverrideParameterName( type ) )
		{
			name = overrideName;

			String explicitName;
			PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, String, "Name", explicitName, "" );
			if ( !explicitName.Empty() )
			{
				m_logger.Error( "Parameter with override name '%hs' has explicit name '%hs'. Explicit name from fxmeta will be ignored.", overrideName, explicitName.AsChar() );
			}
		}
		else
		{
			PARSE_MANDATORY_ATTRIBUTE( doc, parameterNode, String, "Name", name, "" );
		}


		Bool haveReg;
		Uint8 reg;
		if ( IMaterialDataProviderDesc::IsRegisterMandatory( type ) )
		{
			PARSE_MANDATORY_ATTRIBUTE( doc, parameterNode, Uint8, "Register", reg, 0 );
			haveReg = true;
		}
		else
		{
			haveReg = PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, Uint8, "Register", reg, Uint8(-1) );
		}
		
		Float min = 0.f;
		Float max = 0.f;
		if ( type == IMaterialDataProviderDesc::PT_Scalar )
		{
			PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, Float, "Min", min, -RED_FLT_MAX );
			PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, Float, "Max", max, RED_FLT_MAX );
		}

		Bool forceNoIndirection = false;
		String dataType;
		GpuWrapApi::eTextureGroup textureGroup = GpuWrapApi::TEXG_Unknown;
		if ( IMaterialDataProviderDesc::GetParameterRegisterType( type ) == IMaterialDataProviderDesc::PRT_Texture )
		{
			PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, Bool, "ForceNoIndirection", forceNoIndirection, false );
			PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, String, "DataType", dataType, String::EMPTY() );

			//TODO moradin - this is a parameter here because enums have to be parameters, attributes are pretty much the same but can't be enums
			// if possible these should be unified at some point
			PARSE_OPTIONAL_PARAMETER( doc, parameterNode, GpuWrapApi::eTextureGroup, "TextureGroup", textureGroup, GpuWrapApi::TEXG_Unknown );
		}

		String tooltip;
		PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, String, "Tooltip", tooltip, String::EMPTY() );

		String customEditorName;
		PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, String, "CustomEditor", customEditorName, String::EMPTY() );

		Bool hidden = false;
		PARSE_OPTIONAL_ATTRIBUTE( doc, parameterNode, Bool, "Hidden", hidden, false );

		Int32 targets = 0;
		const xml::Document::Node* targetsNode = doc->GetFirstNode( parameterNode, "Targets" );
		if ( !LoadShaderTargets( doc, targetsNode, targets ) )
		{
			m_logger.Log( "Failed to load ShaderTargets for Parameter - it will be assigned to PixelShader." );
			targets = MSH_PixelShader;
		}

		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( targets & target )
			{
				if ( !haveReg )
				{
					// types that must have the register specified already have it assigned
					// for all other types the register is generated
					switch ( IMaterialDataProviderDesc::GetParameterRegisterType( type ) )
					{
					case IMaterialDataProviderDesc::PRT_Texture:
						reg = textureRegisters[ target ]++;
						break;

					case IMaterialDataProviderDesc::PRT_Constant:
						reg = nonTextureRegisters[ target ]++;
						break;

					case IMaterialDataProviderDesc::PRT_Dummy:
						reg = dummyRegisters[ target ]++;
						break;

					default: RED_HALT("Unknown parameter register type");
					}
				}

				// check if parameter names are unique
				for ( const auto& existingParam : metaDefinition.m_parameters[ target ] )
				{
					if ( existingParam.m_name == name )
					{
						m_logger.Error( "Parameter with name '%hs' already exists (parameter being parsed [name: '%hs', type: %d, register: %d], existing param [name: '%hs', type: %d, register: %d])", name.AsChar(), name.AsChar(), type, reg, existingParam.m_name.AsChar(), existingParam.m_type, existingParam.m_register );
						return false;
					}
				}

				if ( !IMaterialDataProviderDesc::ValidateParameterRegister( type, reg ) )
				{
					m_logger.Error( "Parameter with name '%hs' (type: %d) has invalid register: %d", name.AsChar(), type, reg );
					return false;
				}
				
				MaterialMetaDefinition::Param param;
				param.m_type				= type;
				param.m_register			= reg;
				param.m_name				= name;
				param.m_min					= min;
				param.m_max					= max;
				param.m_forceNoIndirection	= forceNoIndirection;
				param.m_textureDataType		= dataType;
				param.m_textureGroup		= textureGroup;
				param.m_tooltip				= tooltip;
				param.m_hidden				= hidden;
				param.m_customEditorName	= customEditorName;
				metaDefinition.m_parameters[ target ].PushBack( param );
			}
		}
	}

	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadSamplerStates( const xml::Document* doc, const xml::Document::Node* samplerStatesNode, MaterialMetaDefinition& metaDefinition )
{
	if ( !samplerStatesNode )
	{
		return true;
	}

	Uint8 samplerStateRegisters[ MSH_MAX ];
	red::Memzero( &samplerStateRegisters, sizeof( samplerStateRegisters ) );
	for ( xml::NodeIterator samplerStatesIterator( *doc, samplerStatesNode, "SamplerState" ); samplerStatesIterator; ++samplerStatesIterator )
	{
		const xml::Document::Node* samplerStateNode = samplerStatesIterator.GetCurrent();

		SamplerStateInfo samplerState;

		String name;
		PARSE_MANDATORY_ATTRIBUTE( doc, samplerStateNode, String, "Name", name, "" );

		const xml::Document::Node* addressNode = doc->GetFirstNode( samplerStateNode, "Address" );
		LoadSamplerStateAddress( doc, addressNode, samplerState );

		const xml::Document::Node* textureFilteringNode = doc->GetFirstNode( samplerStateNode, "TextureFiltering" );
		LoadSamplerStateTextureFiltering( doc, textureFilteringNode, samplerState );

		const xml::Document::Node* additionalInfoNode = doc->GetFirstNode( samplerStateNode, "Other" );
		LoadSamplerStateAdditionalInfo( doc, additionalInfoNode, samplerState );

		Int32 targets = MSH_PixelShader;

		const xml::Document::Node* targetsNode = doc->GetFirstNode( samplerStateNode, "Targets" );
		LoadShaderTargets( doc, targetsNode, targets );

		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( targets & target )
			{
				samplerState.m_register = samplerStateRegisters[ target ]++;
				metaDefinition.m_samplerStates[ target ].PushBack( samplerState );
			}
		}
	}

	return true;
}

Bool CMaterialMetaDefinitionLoader::LoadShaderTargets( const xml::Document* doc, const xml::Document::Node* node, Int32& targets )
{
	if ( !node )
	{
		return false;
	}

	for ( xml::NodeIterator targetsIterator( *doc, node, "Target" ); targetsIterator; ++targetsIterator )
	{
		EMaterialShaderTarget shaderTarget;
		PARSE_MANDATORY_PARAMETER( doc, targetsIterator.GetCurrent(), EMaterialShaderTarget, "Name", shaderTarget, MSH_PixelShader );
		targets |= shaderTarget;
	}

	return targets > 0;
}

void CMaterialMetaDefinitionLoader::LoadSamplerStateAddress( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState )
{
	if ( node )
	{
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureAddressing, "U", samplerState.m_addressU, TA_Wrap );
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureAddressing, "V", samplerState.m_addressV, TA_Wrap );
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureAddressing, "W", samplerState.m_addressW, TA_Wrap );
	}
}

void CMaterialMetaDefinitionLoader::LoadSamplerStateTextureFiltering( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState )
{
	if ( node )
	{
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureFilteringMin, "Min", samplerState.m_filteringMin, TFMin_Anisotropic );
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureFilteringMag, "Mag", samplerState.m_filteringMag, TFMag_Linear );
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureFilteringMip, "Mip", samplerState.m_filteringMip, TFMip_Linear );
	}
}

void CMaterialMetaDefinitionLoader::LoadSamplerStateAdditionalInfo( const xml::Document* doc, const xml::Document::Node* node, SamplerStateInfo& samplerState )
{
	if ( node )
	{
		PARSE_OPTIONAL_PARAMETER( doc, node, ETextureComparisonFunction, "ComparisonFunction", samplerState.m_comparisonFunc, TCF_None );
	}
}
