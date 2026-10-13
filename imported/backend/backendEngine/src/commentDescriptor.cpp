/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "commentDescriptor.h"
#include "editorGraphTypes.h"
#include "graphDescriptor.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( TextDescriptor, tools );
	RTTI_PARENT_TYPE( IGraphNodeDescriptor );
	RTTI_DESCRIPTIVE_NAME( Help );
	RTTI_PROPERTY( m_comment );
	RTTI_PROPERTY( m_width ).editable();
	RTTI_PROPERTY( m_height ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CommentDescriptor, tools );
	RTTI_PARENT_TYPE( TextDescriptor );
	RTTI_DESCRIPTIVE_NAME( Comment );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NoteDescriptor, tools );
	RTTI_PARENT_TYPE( TextDescriptor );
	RTTI_DESCRIPTIVE_NAME( Note );
RTTI_END_TYPE();

namespace tools
{
	TextDescriptor::TextDescriptor()
		: m_width( 100 )
		, m_height( 100 )
	{
	}

	Bool TextDescriptor::OnCreate( const THandle< ISerializable >& context, IGraphDescriptor& graph, const String& typeName, Bool mappingOnly )
	{
		// create runtime node
		m_comment = ::CreateHandle<tools::CommentNode>();
		return true;
	}

	Bool TextDescriptor::OnInitialize( IGraphDescriptor& graph, Bool mappingOnly )
	{
		m_blockType = "commentframe";
		m_iconName = "";

		return true;
	}

	Bool TextDescriptor::OnPlace( IGraphDescriptor& graph )
	{
		return OnInitialize( graph, false );
	}

	Bool TextDescriptor::OnRestore( IGraphDescriptor& graph )
	{
		return OnInitialize( graph, false );
	}

	void TextDescriptor::OnNodeDataChanged( const rtti::AccessPath& path )
	{
		if ( path.ToString() == "title" )
		{
			UpdateCaption();
		}
		else if ( path.ToString() == "comment" )
		{
			GetGraphDescriptor()->InvalidateNodeComponent( m_id, red::GetHash( RED_NAME_CONSTEXPR( "comment" ) ) );
		}
	}

	tools::IGraphNodeDescriptor::SocketIndex TextDescriptor::GetSocketsCount() const
	{
		return 0;
	}

	void TextDescriptor::GetSocketDescription( SocketIndex index, const THandle< SocketDescription >& socket ) const
	{
	}

	const THandle< ISerializable > TextDescriptor::GetNodeData() const
	{
		return m_comment;
	}

	String TextDescriptor::GetCaption() const
	{
		const String& title = m_comment->GetTitle();
		return title.Empty() ? "Comment" : title;
	}

	Color TextDescriptor::GetBlockColor() const
	{
		return Color( 0, 205, 205, 60 );
	}

	Color TextDescriptor::GetBlockTextColor() const
	{
		return Color( 255, 255, 255 );
	}

	THandle< GraphComponentDescription > TextDescriptor::OnCreateComponent( const String& type )
	{
		if ( type == "comment" )
		{
			THandle< GraphComponentDescription > component( CreateHandle< NodeCommentDescription >() );
			component->m_id = red::GetHash( RED_NAME_CONSTEXPR( "comment" ) );
			return component;
		}

		return nullptr;
	}

	void TextDescriptor::OnGetNodeComponent( GraphComponentDescription* component ) const
	{
		if ( component->IsA<NodeCommentDescription>() )
		{
			NodeCommentDescription* stringComponent = Cast<NodeCommentDescription>( component );
			stringComponent->m_comment = m_comment->GetComment();
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	//
	//
	Bool CommentDescriptor::OnInitialize( IGraphDescriptor& graph, Bool mappingOnly )
	{
		m_blockType = "commentframe";
		m_iconName = "";

		return true;
	}

	String CommentDescriptor::GetCaption() const
	{
		const String& title = m_comment->GetTitle();
		return title.Empty() ? "Comment" : title;
	}

	Color CommentDescriptor::GetBlockColor() const
	{
		return Color( 0, 205, 205, 60 );
	}

	Color CommentDescriptor::GetBlockTextColor() const
	{
		return Color( 255, 255, 255 );
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	//
	//
	Bool NoteDescriptor::OnInitialize( IGraphDescriptor& graph, Bool mappingOnly )
	{
		m_blockType = "noteframe";
		m_iconName = "";

		return true;
	}

	String NoteDescriptor::GetCaption() const
	{
		const String& title = m_comment->GetTitle();
		return title.Empty() ? "Note" : title;
	}

	Color NoteDescriptor::GetBlockColor() const
	{
		return Color( 119, 133, 155 );
	}

	Color NoteDescriptor::GetBlockTextColor() const
	{
		return Color( 255, 255, 255 );
	}

}