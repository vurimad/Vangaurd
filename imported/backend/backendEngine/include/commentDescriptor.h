/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "backendEngine.h"
#include "commentNode.h"

#include "../../backendEngine/include/graphEditorNode.h"

namespace tools
{
	class BACKEND_ENGINE_API TextDescriptor : public IGraphNodeDescriptor
	{
		RTTI_DECLARE_TYPE( TextDescriptor );

	public:
		TextDescriptor();

		virtual Bool OnCreate( const THandle< ISerializable >& context, IGraphDescriptor& graph, const String& typeName, Bool mappingOnly ) override;
		virtual Bool OnInitialize( IGraphDescriptor& graph, Bool mappingOnly ) override;
		virtual Bool OnPlace( IGraphDescriptor& graph ) override;
		virtual Bool OnRestore( IGraphDescriptor& graph ) override;
		virtual void OnNodeDataChanged( const rtti::AccessPath& path ) override;
		virtual SocketIndex GetSocketsCount() const override;
		virtual void GetSocketDescription( SocketIndex index, const THandle< SocketDescription >& socket ) const override;

		virtual const THandle< ISerializable > GetNodeData() const override;
		virtual String GetCaption() const override;
		virtual Color GetBlockColor() const override;
		virtual Color GetBlockTextColor() const override;

		virtual THandle< GraphComponentDescription > OnCreateComponent( const String& type ) override;
		virtual void OnGetNodeComponent( GraphComponentDescription* component ) const override;

	protected:
		THandle< CommentNode > m_comment;

		Uint32 m_width;
		Uint32 m_height;
	};

	///////////////////////////////////////////////////////////////////////////////////////
	//
	//
	//
	class BACKEND_ENGINE_API CommentDescriptor : public TextDescriptor
	{
		RTTI_DECLARE_TYPE(CommentDescriptor);

	public:
		virtual Bool OnInitialize( IGraphDescriptor& graph, Bool mappingOnly ) override;

		virtual String GetCaption() const override;
		virtual Color GetBlockColor() const override;
		virtual Color GetBlockTextColor() const override;
	};

	/////////////////////////////////////////////////////////////////////////////////////////

	class BACKEND_ENGINE_API NoteDescriptor : public TextDescriptor
	{
		RTTI_DECLARE_TYPE( NoteDescriptor );

	public:
		virtual Bool OnInitialize( IGraphDescriptor& graph, Bool mappingOnly ) override;

		virtual String GetCaption() const override;
		virtual Color GetBlockColor() const override;
		virtual Color GetBlockTextColor() const override;
	};

}