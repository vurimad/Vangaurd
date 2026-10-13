/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "shaderMetaLoader.h"


// #TODO: There helpers are duplicated here and in materialMetaDefinitionLoader.cpp. Combine it!


#define PARSE_MANDATORY_PARAMETER( doc, node, ParamType, ParamName, OutVariable, DefaultValue )			\
	{																									\
		ParamType tempValue;																			\
		if ( !helpers::TryParseValue< ParamType >( doc, node, ParamName, tempValue, DefaultValue, m_log ) )	\
		{																								\
			String fullNodePath = doc->GetNodeName( node );												\
			m_logger.Error( "Parsing Parameter \"%hs\" FAILED", fullNodePath.AsChar() );				\
			return false;																				\
		}																								\
		OutVariable = tempValue;																		\
	}

#define PARSE_OPTIONAL_PARAMETER( doc, node, ParamType, ParamName, OutVariable, DefaultValue )			\
	{																									\
		ParamType tempValue;																			\
		helpers::TryParseValue< ParamType >( doc, node, ParamName, tempValue, DefaultValue, m_log );			\
		OutVariable = tempValue;																		\
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

	template< typename T >
	static Bool TryParseValue( const xml::Document* doc, const xml::Document::Node* node, const red::String& fieldName, T& value, const T defaultValue, IShaderCompilerLogger* log )
	{
		value = defaultValue;

		red::String fieldValue;
		const xml::Document::Attribute* attribute = doc->GetFirstAttribute( node, fieldName.AsChar() );
		if ( attribute )
		{
			fieldValue = doc->GetAttributeValue( attribute );
			auto* field = static_cast<const rtti::EnumType*>(GetTypeObject<T>());
			if ( field )
			{
				Int64 val;
				if ( field->FindValue( RED_NAME( fieldValue.AsChar() ), val ) )
				{
					value = static_cast<T>(val);
					return true;
				}
				else
				{
					log->Error( "Failed to parse %hs value: %hs", fieldName.AsChar(), fieldValue.AsChar() );
					return false;
				}
			}
		}

		return false;
	}

}


#define PARSE_MANDATORY_ATTRIBUTE( doc, node, AttributeType, AttributeName, OutVariable, DefaultValue )				\
{																													\
	if ( !helpers::ReadAttributeT< AttributeType >( doc, node, AttributeName, OutVariable, DefaultValue ) )			\
	{																												\
		RED_LOG_ERROR( "Parsing Attribute '%hs' of node '%hs' FAILED", AttributeName, doc->GetNodeName( node ) );	\
		return false;																								\
	}																												\
}

#define PARSE_OPTIONAL_ATTRIBUTE( doc, node, AttributeType, AttributeName, OutVariable, DefaultValue )	\
	helpers::ReadAttributeT< AttributeType >( doc, node, AttributeName, OutVariable, DefaultValue )



enum GetBaseResult
{
	Base_Error = 0,
	Base_Found,
	Base_NoBase
};


template< typename T, typename TMap >
static GetBaseResult GetBaseDesc( const xml::Document* doc, const xml::Document::Node* node, const T& defaultDesc, const TMap& descMap, T& outDesc, String& name, Bool isLoadingOMStatesLibrary, Bool isLoadingInclude, IShaderCompilerLogger* log )
{
	outDesc = defaultDesc;
	if ( isLoadingOMStatesLibrary )
	{
		PARSE_OPTIONAL_ATTRIBUTE( doc, node, String, "Name", name, "" );
		if ( name.Empty() )
		{
			log->Error( "Node cannot have empty name." );
			return Base_Error;
		}
		if ( descMap.KeyExist( name ) )
		{
			if ( isLoadingInclude )
			{
				log->Error( "'%hs' already exists - overriding not allowed.", name.AsChar() );
				return Base_Error;
			}
			else
			{
				log->Warn( "'%hs' already exists - it will be overridden.", name.AsChar() );
				outDesc = descMap[name];
				return Base_Found;
			}
		}
		else
		{
			return Base_Found;
		}
	}
	else
	{
		if ( PARSE_OPTIONAL_ATTRIBUTE( doc, node, String, "Base", name, "" ) )
		{
			if ( descMap.KeyExist( name ) )
			{
				outDesc = descMap[name];
				return Base_Found;
			}
			else
			{
				RED_LOG_ERROR( "Could not find with name '%hs' in library", name.AsChar() );
				return Base_Error;
			}
		}
		else
		{
			return Base_NoBase;
		}
	}
}

template< typename T, typename TMap >
static Bool StoreDesc( const T& desc, String& name, TMap& descMap, Bool isLoadingOMStatesLibrary, Bool isLoadingInclude, IShaderCompilerLogger* log )
{
	if ( isLoadingOMStatesLibrary )
	{
		if ( descMap.KeyExist( name ) )
		{
			if ( isLoadingInclude )
			{
				log->Error( "Duplicate RenderStateMode '%hs' in fxmeta include file.", name );
				return false;
			}
			descMap[name] = desc;
		}
		else
		{
			descMap.Insert( name, desc );
		}
	}
	return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OMStateLoader::OMStateLoader()
{
	m_defaultDepthStencilDesc = PSODesc::DepthStencilModeDesc::Default();
	//m_defaultRasterizerDesc = PSODesc::RasterizerModeDesc::Default();
	m_defaultBlendModeDesc = PSODesc::BlendModeDesc::Default();

	red::Memzero( &m_defaultOMState, sizeof( m_defaultOMState ) );
	m_defaultOMState.m_depthStencilModeDesc = m_defaultDepthStencilDesc;
	m_defaultOMState.m_rasterizerModeDesc = m_defaultRasterizerDesc;
	m_defaultOMState.m_blendModeDesc = m_defaultBlendModeDesc;
}

void OMStateLoader::Initialize( const xml::Document* doc, IShaderCompilerLogger& log )
{
	RED_ASSERT( doc != nullptr, "Must have a valid xml document" );
	m_xmlDoc = doc;
	m_log = &log;
}



Bool OMStateLoader::LoadBases( const xml::Document::Node* omStatesRootNode, Bool isUniqueInclude )
{
	LoadType loadType = isUniqueInclude ? LoadType::UniqueBase : LoadType::Base;

	if ( const xml::Document::Node* depthStencilModesNode = m_xmlDoc->GetFirstNode( omStatesRootNode, "DepthStencilModes" ) )
	{
		for ( xml::NodeIterator dsModeIterator( *m_xmlDoc, depthStencilModesNode, "DepthStencilMode" ); dsModeIterator; ++dsModeIterator )
		{
			DepthStencilModeDesc depthStencilMode;
			if ( !LoadDepthStencilMode_Internal( dsModeIterator.GetCurrent(), depthStencilMode, loadType ) )
			{
				m_log->Error( "LoadDepthStencilMode FAILED." );
				return false;
			}
		}
	}

	if ( const xml::Document::Node* rasterizerModesNode = m_xmlDoc->GetFirstNode( omStatesRootNode, "RasterizerModes" ) )
	{
		for ( xml::NodeIterator rsModeIterator( *m_xmlDoc, rasterizerModesNode, "RasterizerMode" ); rsModeIterator; ++rsModeIterator )
		{
			RasterizerModeDesc rasterizerMode;
			if ( !LoadRasterizerMode_Internal( rsModeIterator.GetCurrent(), rasterizerMode, loadType ) )
			{
				m_log->Error( "LoadRasterizerMode FAILED." );
				return false;
			}
		}
	}

	if ( const xml::Document::Node* blendModesNode = m_xmlDoc->GetFirstNode( omStatesRootNode, "BlendModes" ) )
	{
		for ( xml::NodeIterator bmModeIterator( *m_xmlDoc, blendModesNode, "BlendMode" ); bmModeIterator; ++bmModeIterator )
		{
			BlendModeDesc blendMode;
			if ( !LoadBlendMode_Internal( bmModeIterator.GetCurrent(), blendMode, loadType ) )
			{
				m_log->Error( "LoadBlendMode FAILED." );
				return false;
			}
		}
	}

	if ( const xml::Document::Node* omStatesNode = m_xmlDoc->GetFirstNode( omStatesRootNode, "OMStates" ) )
	{
		for ( xml::NodeIterator omStateIterator( *m_xmlDoc, omStatesNode, "OMState" ); omStateIterator; ++omStateIterator )
		{
			SOMState omState;
			if ( !LoadOMState_Internal( omStateIterator.GetCurrent(), omState, loadType ) )
			{
				m_log->Error( "LoadOMState FAILED." );
				return false;
			}
		}
	}

	return true;
}


Bool OMStateLoader::LoadDepthStencilMode( const xml::Document::Node* depthStencilModeNode, DepthStencilModeDesc& depthStencilMode )
{
	return LoadDepthStencilMode_Internal( depthStencilModeNode, depthStencilMode, LoadType::State );
}

Bool OMStateLoader::LoadRasterizerMode( const xml::Document::Node* rasterizerModeNode, RasterizerModeDesc& rasterizerMode )
{
	return LoadRasterizerMode_Internal( rasterizerModeNode, rasterizerMode, LoadType::State );
}

Bool OMStateLoader::LoadBlendMode( const xml::Document::Node* blendModeNode, BlendModeDesc& blendMode )
{
	return LoadBlendMode_Internal( blendModeNode, blendMode, LoadType::State );
}

Bool OMStateLoader::LoadOMState( const xml::Document::Node* omStateNode, SOMState& omState )
{
	return LoadOMState_Internal( omStateNode, omState, LoadType::State );
}

Bool OMStateLoader::LoadOMState_StencilRef( const xml::Document::Node* node, SOMState& omState )
{
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, node, Uint8, "ReadMask",	omState.m_stencilReadMask, 0 );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, node, Uint8, "WriteMask",	omState.m_stencilWriteMask, 0 );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, node, Uint8, "Ref",			omState.m_stencilRef, 0 );
	return true;
}

Bool OMStateLoader::LoadDepthStencilMode_Internal( const xml::Document::Node* depthStencilModeNode, DepthStencilModeDesc& depthStencilMode, LoadType loadType )
{
	const Bool loadingBase = (loadType == LoadType::Base || loadType == LoadType::UniqueBase);
	const Bool loadingUnique = (loadType == LoadType::UniqueBase);

	String name;
	DepthStencilModeDesc depthStencilBase;
	if ( !GetBaseDesc( m_xmlDoc, depthStencilModeNode, loadType == LoadType::State ? depthStencilMode : m_defaultDepthStencilDesc, m_depthStencilBases, depthStencilBase, name, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	Bool dt;
	Bool dw;
	Bool st;
	Bool stRM;
	Bool stWM;

	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, depthStencilModeNode, Bool, "DepthTest", dt, depthStencilBase.m_depthTestEnable );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, depthStencilModeNode, Bool, "DepthWrite", dw, depthStencilBase.m_depthWriteEnable );
	PARSE_OPTIONAL_PARAMETER( m_xmlDoc, depthStencilModeNode, DepthStencilMode::ComparisonMode, "DepthFunc", depthStencilMode.m_depthFunc, depthStencilBase.m_depthFunc );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, depthStencilModeNode, Bool, "Stencil", st, depthStencilBase.m_stencilEnable );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, depthStencilModeNode, Bool, "StencilReadMaskEnable", stRM, depthStencilBase.m_stencilReadMask );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, depthStencilModeNode, Bool, "StencilWriteMaskEnable", stWM, depthStencilBase.m_stencilWriteMask );

	depthStencilMode.m_depthTestEnable = dt;
	depthStencilMode.m_depthWriteEnable = dw;
	depthStencilMode.m_stencilEnable = st;
	depthStencilMode.m_stencilReadMask = stRM;
	depthStencilMode.m_stencilWriteMask = stWM;

	{
		const xml::Document::Node* frontFaceNode = m_xmlDoc->GetFirstNode( depthStencilModeNode, "FrontFace" );
		if ( loadingUnique && !frontFaceNode && depthStencilMode.m_stencilEnable )
		{
			m_log->Error( "DepthStencilMode, missing <FrontFace> tag." );
			return false;
		}
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, frontFaceNode, DepthStencilMode::ComparisonMode, "StencilFunc", depthStencilMode.m_frontFace.m_stencilFunc, depthStencilBase.m_frontFace.m_stencilFunc );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, frontFaceNode, DepthStencilMode::StencilOpMode, "StencilPassOp", depthStencilMode.m_frontFace.m_stencilPassOp, depthStencilBase.m_frontFace.m_stencilPassOp );
	}

	if ( !StoreDesc( depthStencilMode, name, m_depthStencilBases, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	return true;
}

Bool OMStateLoader::LoadRasterizerMode_Internal( const xml::Document::Node* rasterizerModeNode, RasterizerModeDesc& rasterizerMode, LoadType loadType )
{
	const Bool loadingBase = (loadType == LoadType::Base || loadType == LoadType::UniqueBase);
	const Bool loadingUnique = (loadType == LoadType::UniqueBase);

	String name;
	RasterizerModeDesc rasterizerModeBase;
	if ( !GetBaseDesc( m_xmlDoc, rasterizerModeNode, loadType == LoadType::State ? rasterizerMode : m_defaultRasterizerDesc, m_rasterizerBases, rasterizerModeBase, name, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	// mark as invalid
	rasterizerMode.m_valid = 0;

	Bool wireframe;
	Bool allowMSAA;
	Bool conservativeRasterization;
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, rasterizerModeNode, Bool, "Wireframe", wireframe, rasterizerModeBase.m_wireframe );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, rasterizerModeNode, Bool, "AllowMSAA", allowMSAA, rasterizerModeBase.m_allowMSAA );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, rasterizerModeNode, Bool, "ConservativeRasterization", conservativeRasterization, rasterizerModeBase.m_conservativeRasterization );
	PARSE_OPTIONAL_PARAMETER( m_xmlDoc, rasterizerModeNode, RasterizerMode::FrontFaceWinding, "FrontWinding", rasterizerMode.m_frontWinding, rasterizerModeBase.m_frontWinding );
	PARSE_OPTIONAL_PARAMETER( m_xmlDoc, rasterizerModeNode, RasterizerMode::CullMode, "CullMode", rasterizerMode.m_cullMode, rasterizerModeBase.m_cullMode );
	PARSE_OPTIONAL_PARAMETER( m_xmlDoc, rasterizerModeNode, RasterizerMode::OffsetMode, "OffsetMode", rasterizerMode.m_offsetMode, rasterizerModeBase.m_offsetMode );

	rasterizerMode.m_wireframe = wireframe;
	rasterizerMode.m_allowMSAA = allowMSAA;
	rasterizerMode.m_conservativeRasterization = conservativeRasterization;

	// everything went ok
	rasterizerMode.m_valid = 1;

	if ( !StoreDesc( rasterizerMode, name, m_rasterizerBases, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	return true;
}

Bool OMStateLoader::LoadBlendMode_Internal( const xml::Document::Node* blendModeNode, BlendModeDesc& blendMode, LoadType loadType )
{
	const Bool loadingBase = (loadType == LoadType::Base || loadType == LoadType::UniqueBase);
	const Bool loadingUnique = (loadType == LoadType::UniqueBase);

	String name;
	BlendModeDesc blendModeBase;
	if ( !GetBaseDesc( m_xmlDoc, blendModeNode, loadType == LoadType::State ? blendMode : m_defaultBlendModeDesc, m_blendBases, blendModeBase, name, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, blendModeNode, Bool, "Independent", blendMode.m_independent, blendModeBase.m_independent );
	PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, blendModeNode, Bool, "AlphaToCoverage", blendMode.m_alphaToCoverage, blendModeBase.m_alphaToCoverage );

	for ( Uint8 rt_i = 0; rt_i < MAX_RENDERTARGETS; ++rt_i )
	{
		blendMode.m_renderTarget[rt_i] = blendModeBase.m_renderTarget[rt_i];
	}

	// #TODO: Does this make sense?
	blendMode.m_numTargets = loadingBase ? 1 : blendModeBase.m_numTargets;

	for ( xml::NodeIterator it( *m_xmlDoc, blendModeNode, "RenderTarget" ); it; ++it )
	{
		Int8 index;
		PARSE_MANDATORY_ATTRIBUTE( m_xmlDoc, it.GetCurrent(), Int8, "Index", index, 0 );
		RED_FATAL_ASSERT( index < MAX_RENDERTARGETS, "Invalid rendertarget index" );
		RenderTarget& rt = blendMode.m_renderTarget[(Uint32)index];
		const RenderTarget& baseRT = blendModeBase.m_renderTarget[(Uint32)index];

		Bool blend;
		PARSE_OPTIONAL_ATTRIBUTE( m_xmlDoc, it.GetCurrent(), Bool, "Blend", blend, baseRT.m_blendEnable );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::WriteMask, "WriteMask", rt.m_writeMask, baseRT.m_writeMask );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Op, "ColorOp", rt.m_colorOp, baseRT.m_colorOp );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Op, "AlphaOp", rt.m_alphaOp, baseRT.m_alphaOp );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Factor, "SrcFactor", rt.m_srcFactor, baseRT.m_srcFactor );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Factor, "DestFactor", rt.m_destFactor, baseRT.m_destFactor );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Factor, "SrcAlphaFactor", rt.m_srcAlphaFactor, baseRT.m_srcAlphaFactor );
		PARSE_OPTIONAL_PARAMETER( m_xmlDoc, it.GetCurrent(), BlendMode::Factor, "DestAlphaFactor", rt.m_destAlphaFactor, baseRT.m_destAlphaFactor );

		rt.m_blendEnable = blend;

		blendMode.m_numTargets = Max( index + 1, blendMode.m_numTargets );
	}

	if ( !StoreDesc( blendMode, name, m_blendBases, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	return true;
}



template<typename T, typename F>
Bool OMStateLoader::LoadOMStatePart( const xml::Document& xmlDoc, const xml::Document::Node* omStateNode, const char* partName, F&& loaderFunc, red::HashMap<red::String, T>& bases, T& outPart )
{
	String baseName;
	if ( helpers::ReadAttributeT( m_xmlDoc, omStateNode, partName, baseName, String::EMPTY() ) )
	{
		if ( m_xmlDoc->GetFirstNode( omStateNode, partName ) != nullptr )
		{
			m_log->Error( "OMState cannot contain both attribute and child element for %hs", partName );
			return false;
		}

		if ( !bases.Find( baseName, outPart ) )
		{
			m_log->Error( "Could not find matching %hs base '%hs'", partName, baseName.AsChar() );
			return false;
		}
	}
	else if ( const xml::Document::Node* partNode = m_xmlDoc->GetFirstNode( omStateNode, partName ) )
	{
		if ( !(this->*loaderFunc)( partNode, outPart, LoadType::State ) )
		{
			return false;
		}
	}
	else
	{
		m_log->Error( "OMState is missing %hs (either as attribute or child element).", partName );
		return false;
	}

	return true;
}


Bool OMStateLoader::LoadOMState_Internal( const xml::Document::Node* omStateNode, SOMState& omState, LoadType loadType )
{
	const Bool loadingBase = (loadType == LoadType::Base || loadType == LoadType::UniqueBase);
	const Bool loadingUnique = true;

	String name;
	GetBaseResult baseResult = GetBaseDesc( m_xmlDoc, omStateNode, loadType == LoadType::State ? omState : m_defaultOMState, m_omStateBases, omState, name, loadingBase, loadingUnique, m_log );
	if ( baseResult == Base_Error )
	{
		return false;
	}
	if ( loadType == LoadType::State && baseResult == Base_Found )
	{
		if ( m_xmlDoc->GetFirstAttribute( omStateNode, "DepthStencilMode" ) || m_xmlDoc->GetFirstNode( omStateNode, "DepthStencilMode" ) )
		{
			m_log->Error( "OMState is using Base. Cannot override DepthStencilMode" );
			return false;
		}
		if ( m_xmlDoc->GetFirstAttribute( omStateNode, "RasterizerMode" ) || m_xmlDoc->GetFirstNode( omStateNode, "RasterizerMode" ) )
		{
			m_log->Error( "OMState is using Base. Cannot override RasterizerMode" );
			return false;
		}
		if ( m_xmlDoc->GetFirstAttribute( omStateNode, "BlendMode" ) || m_xmlDoc->GetFirstNode( omStateNode, "BlendMode" ) )
		{
			m_log->Error( "OMState is using Base. Cannot override BlendMode" );
			return false;
		}
		return true;
	}

	if ( !LoadOMStatePart( *m_xmlDoc, omStateNode, "DepthStencilMode", &OMStateLoader::LoadDepthStencilMode_Internal, m_depthStencilBases, omState.m_depthStencilModeDesc ) )
	{
		return false;
	}

	if ( !LoadOMStatePart( *m_xmlDoc, omStateNode, "RasterizerMode", &OMStateLoader::LoadRasterizerMode_Internal, m_rasterizerBases, omState.m_rasterizerModeDesc ) )
	{
		return false;
	}

	if ( !LoadOMStatePart( *m_xmlDoc, omStateNode, "BlendMode", &OMStateLoader::LoadBlendMode_Internal, m_blendBases, omState.m_blendModeDesc ) )
	{
		return false;
	}

	if ( !StoreDesc( omState, name, m_omStateBases, loadingBase, loadingUnique, m_log ) )
	{
		return false;
	}

	return true;
}
