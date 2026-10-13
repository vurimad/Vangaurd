/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/renderData/include/pipelineStateObject.h"
#include "../../../common/renderData/include/shaderCompilerLogger.h"
#include "../../../common/resource/include/xmlDocument.h"


class RENDERBACKEND_API OMStateLoader
{
public:
	OMStateLoader();

	void Initialize( const xml::Document* doc, IShaderCompilerLogger& log );

	// Load a OMStates / PSOStates node, which contains DepthStencilModes/RasterizerModes/BlendModes children, each of
	// which can contain a number of "base" mode descriptors.
	// If isUniqueInclude==true, then all bases must be new (cannot modify an existing base).
	Bool LoadBases( const xml::Document::Node* omStatesNode, Bool isUniqueInclude );


	// Load a particular state from a node, fetching from a previously loaded Base, if there is one. Members not
	// specified (by the node, or by any matching Base), will not be modified.
	Bool LoadDepthStencilMode( const xml::Document::Node* depthStencilModeNode, DepthStencilModeDesc& depthStencilMode );
	Bool LoadRasterizerMode( const xml::Document::Node* rasterizerModeNode, RasterizerModeDesc& rasterizerMode );
	Bool LoadBlendMode( const xml::Document::Node* blendModeNode, BlendModeDesc& blendMode );
	Bool LoadOMState( const xml::Document::Node* omStateNode, SOMState& omState );
	Bool LoadOMState_StencilRef( const xml::Document::Node* omStateNode, SOMState& omState );

private:

	enum class LoadType
	{
		UniqueBase,
		Base,
		State,
	};

	Bool LoadDepthStencilMode_Internal( const xml::Document::Node* depthStencilModeNode, DepthStencilModeDesc& depthStencilMode, LoadType loadType );
	Bool LoadRasterizerMode_Internal( const xml::Document::Node* rasterizerModeNode, RasterizerModeDesc& rasterizerMode, LoadType loadType );
	Bool LoadBlendMode_Internal( const xml::Document::Node* blendModeNode, BlendModeDesc& blendMode, LoadType loadType );
	Bool LoadOMState_Internal( const xml::Document::Node* omStateNode, SOMState& omState, LoadType loadType );

	template<typename T, typename F>
	Bool LoadOMStatePart( const xml::Document& xmlDoc, const xml::Document::Node* omStateNode, const char* partName, F&& loaderFunc, red::HashMap<red::String, T>& bases, T& outPart );


	const xml::Document* m_xmlDoc;
	IShaderCompilerLogger* m_log;

	DepthStencilModeDesc		m_defaultDepthStencilDesc;
	RasterizerModeDesc			m_defaultRasterizerDesc;
	BlendModeDesc				m_defaultBlendModeDesc;
	SOMState					m_defaultOMState;

	red::HashMap< red::String, DepthStencilModeDesc >	m_depthStencilBases;
	red::HashMap< red::String, RasterizerModeDesc >		m_rasterizerBases;
	red::HashMap< red::String, BlendModeDesc >			m_blendBases;
	red::HashMap< red::String, SOMState >				m_omStateBases;
};
