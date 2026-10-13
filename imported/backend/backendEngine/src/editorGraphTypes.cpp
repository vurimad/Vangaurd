/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphTypes.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( GraphComponentDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_componentType ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( VariantComponent, tools );
	RTTI_PARENT_TYPE( GraphComponentDescription );
	RTTI_PROPERTY( m_value ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NodeCommentDescription, tools );
	RTTI_PARENT_TYPE( GraphComponentDescription );
	RTTI_PROPERTY( m_comment ).editable();
	RTTI_PROPERTY( m_color ).editable();
	RTTI_PROPERTY( m_backgroundColor ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NodeDebuggerDescription, tools );
	RTTI_PARENT_TYPE( GraphComponentDescription );

	RTTI_PROPERTY( m_debuggerShowBreakpoint ).editable();
	RTTI_PROPERTY( m_debuggerBreakpoint ).editable();
	RTTI_PROPERTY( m_debuggerHit ).editable();

	RTTI_PROPERTY( m_tracerHit ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SocketStringValueDescription, tools )
	RTTI_PARENT_TYPE( GraphComponentDescription );

	RTTI_PROPERTY( m_value ).readOnly();
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( NodeDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_id ).readOnly();
	RTTI_PROPERTY( m_isSubGraphContainer ).readOnly();
	RTTI_PROPERTY( m_isGraphInclude ).readOnly().notSerialized();
	RTTI_PROPERTY( m_canRefresh ).readOnly();
	RTTI_PROPERTY( m_canBeCloned ).readOnly();
	RTTI_PROPERTY( m_blockType ).readOnly().notSerialized();
	RTTI_PROPERTY( m_tooltip ).readOnly().notSerialized();
	RTTI_PROPERTY( m_position ).editable();
	RTTI_PROPERTY( m_caption ).editable();
	RTTI_PROPERTY( m_subtitle ).editable();
	RTTI_PROPERTY( m_iconName ).editable();
	RTTI_PROPERTY( m_headerColor ).editable();
	RTTI_PROPERTY( m_captionColor ).editable();
	RTTI_PROPERTY( m_highlightFrame ).editable().notSerialized();
	RTTI_PROPERTY( m_highlightFrameColor ).editable().notSerialized();
	RTTI_PROPERTY( m_infoPanelText ).editable().notSerialized();
	RTTI_PROPERTY( m_infoPanelTextColor ).editable().notSerialized();
	RTTI_PROPERTY( m_infoPanelIconName).editable().notSerialized();
	RTTI_PROPERTY( m_linkedResourcePath ).readOnly().notSerialized();
	RTTI_PROPERTY( m_decorativeFrame ).editable().notSerialized();
	RTTI_PROPERTY( m_decorativeFrameColor ).editable().notSerialized();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SocketBinding, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_nodeId );
	RTTI_PROPERTY( m_socketId );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SocketDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_id ).readOnly();
	RTTI_PROPERTY( m_placement ).readOnly();

	RTTI_PROPERTY( m_caption ).editable();
	RTTI_PROPERTY( m_captionColor ).editable();
	RTTI_PROPERTY( m_isHidden ).editable();

	RTTI_PROPERTY( m_drawStyle ).editable();

	RTTI_PROPERTY( m_direction ).readOnly();
	RTTI_PROPERTY( m_linkColor ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConnectionDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_sourceNode ).readOnly();
	RTTI_PROPERTY( m_destinationNode ).readOnly();
	RTTI_PROPERTY( m_sourceSocketIndex ).readOnly();
	RTTI_PROPERTY( m_destinationSocketIndex ).readOnly();
	RTTI_PROPERTY( m_id ).readOnly();

	RTTI_PROPERTY( m_connectionType ).readOnly().notSerialized();
	RTTI_PROPERTY( m_isDisabled ).editable();
	RTTI_PROPERTY( m_isDirectional ).editable();
	RTTI_PROPERTY( m_highlight ).editable().notSerialized();;
	RTTI_PROPERTY( m_highlightColor ).editable().notSerialized();;
	RTTI_PROPERTY( m_thickness ).editable();
	RTTI_PROPERTY( m_isInactive ).readOnly();
	RTTI_PROPERTY( m_name ).readOnly().notSerialized();
	RTTI_PROPERTY( m_originVersion );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NodeCreationInfo, tools )
	RTTI_PROPERTY( m_data ).editable();
	RTTI_PROPERTY( m_initialPosition ).editable();
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConnectionCreationInfo, tools )
	RTTI_PROPERTY( m_sourceNodeId ).editable();
	RTTI_PROPERTY( m_sourceSocketId ).editable();
	RTTI_PROPERTY( m_destinationNodeId ).editable();
	RTTI_PROPERTY( m_destinationSocketId ).editable();
	RTTI_PROPERTY( m_data ).editable();
	RTTI_PROPERTY( m_isInactive ).editable();
	RTTI_PROPERTY( m_isDisabled ).editable();
RTTI_END_TYPE()

RTTI_BEGIN_ENUM_IN_NAMESPACE( SocketPlacement, tools );
	RTTI_ENUM_OPTION( Left );
	RTTI_ENUM_OPTION( Right );
	RTTI_ENUM_OPTION( Bottom );
	RTTI_ENUM_OPTION( Center );
	RTTI_ENUM_OPTION( Title );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( SocketDirection, tools );
	RTTI_ENUM_OPTION( Input );
	RTTI_ENUM_OPTION( Output );
	RTTI_ENUM_OPTION( InputOutput );
	RTTI_ENUM_OPTION( Variable );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( SocketDrawStyle, tools );
	RTTI_ENUM_OPTION( Default );
	RTTI_ENUM_OPTION( Arrow );
	RTTI_ENUM_OPTION( InArrow );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( SearchProcessType, tools );
	RTTI_ENUM_OPTION( Label );
	RTTI_ENUM_OPTION( Type );
	RTTI_ENUM_OPTION( Comment );
RTTI_END_ENUM();

namespace tools
{
	//////////////////////////////////////////////////////////////////////////

	GraphComponentDescription::GraphComponentDescription( const red::String& typeName )
		: m_componentType( typeName )
		, m_id( InvalidComponentIndex )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	VariantComponent::VariantComponent()
		: GraphComponentDescription( "VariantComponent" )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	NodeCommentDescription::NodeCommentDescription()
		: GraphComponentDescription( "BlockCommentWidget" )
		, m_color( Color::WHITE() )
		, m_backgroundColor( Color::RED() )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	NodeDebuggerDescription::NodeDebuggerDescription()
		: GraphComponentDescription( "BlockDebuggerWidget" )
		, m_debuggerShowBreakpoint( false )
		, m_debuggerBreakpoint( false )
		, m_debuggerHit( false )
		, m_tracerHit( false )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	SocketStringValueDescription::SocketStringValueDescription()
		: GraphComponentDescription( "SocketStringValueWidget" )
		, m_value( String::EMPTY() )
	{
	}

	//////////////////////////////////////////////////////////////////////////

	const char* NodeDescription::s_highlightFramePropertyName = "highlightFrame";
	const char* NodeDescription::s_highlightFrameColorPropertyName = "highlightFrameColor";
	const char* NodeDescription::s_infoPanelTextPropertyName = "infoPanelText";
	const char* NodeDescription::s_infoPanelTextColorPropertyName = "infoPanelTextColor";
	const char* NodeDescription::s_infoPanelIconPropertyName= "infoPanelIconName";
	const char* NodeDescription::s_decorativeFramePropertyName = "decorativeFrame";
	const char* NodeDescription::s_decorativeFrameColorPropertyName = "decorativeFrameColor";

	NodeDescription::NodeDescription()
		: m_id( InvalidGraphId )
		, m_isSubGraphContainer( false )
		, m_isGraphInclude( false )
		, m_canRefresh( false )
		, m_canBeCloned( false )
		, m_blockType( "default" )
		, m_tooltip( "" )
		, m_iconName( "default" )
		, m_captionColor( Color::WHITE() )
		, m_headerColor( Color::DARK_RED() )
		, m_highlightFrame( false )
		, m_highlightFrameColor( Color::DARK_MAGENTA() )
		, m_infoPanelTextColor( Color::RED() )
		, m_decorativeFrame( false )
		, m_decorativeFrameColor( Color::DARK_MAGENTA() )
	{
	}

	NodeDescription::NodeDescription( GraphNodeId id )
		: m_id( id )
		, m_isSubGraphContainer( false )
		, m_isGraphInclude( false )
		, m_canRefresh( false )
		, m_canBeCloned( false )
		, m_blockType( "default" )
		, m_tooltip( "" )
		, m_iconName( "default" )
		, m_captionColor( Color::WHITE() )
		, m_headerColor( Color::DARK_RED() )
		, m_highlightFrame( false )
		, m_highlightFrameColor( Color::DARK_MAGENTA() )
		, m_infoPanelTextColor( Color::RED() )
		, m_decorativeFrame( false )
		, m_decorativeFrameColor( Color::DARK_MAGENTA() )
	{
	}

	NodeDescription::NodeDescription( const NodeDescription& desc ) = default;

	EditorObjectID NodeDescription::GetEditorObjectID() const
	{
		static CName kind = RED_NAME_CONSTEXPR( "NodeDescription" );
		return EditorObjectID::Build( m_id, kind );
	}

	//////////////////////////////////////////////////////////////////////////

	const CName SocketBinding::sm_kind = RED_NAME_CONSTEXPR( "SocketBinding" );

	SocketBinding::SocketBinding()
		: m_nodeId( InvalidGraphId )
		, m_socketId( InvalidSocketId )
	{
	}

	SocketBinding::SocketBinding( GraphId nodeId, GraphSocketId socketId )
		: m_nodeId( nodeId )
		, m_socketId( socketId )
	{
	}

	EditorObjectID SocketBinding::GetEditorObjectID() const
	{
		return EditorObjectID::Build( m_nodeId, sm_kind );
	}

	//////////////////////////////////////////////////////////////////////////

	SocketDescription::SocketDescription()
		: m_id( InvalidSocketId )
		, m_placement( SocketPlacement::Left )
		, m_direction( SocketDirection::Input )
		, m_drawStyle( SocketDrawStyle::Default )
		, m_captionColor( Color::WHITE() )
		, m_linkColor( Color::BLACK() )
		, m_isHidden( false )
	{
	}

	SocketDescription::SocketDescription( const String& caption, GraphSocketId id, SocketDirection direction )
		: m_id( id )
		, m_caption( caption )
		, m_placement( SocketPlacement::Left )
		, m_direction( SocketDirection::Input )
		, m_drawStyle( SocketDrawStyle::Default )
		, m_linkColor( Color::BLACK() )
		, m_isHidden( false )
	{
	}

	SocketDescription::SocketDescription( const SocketDescription& other )
		: m_id( other.m_id )
		, m_caption( other.m_caption )
		, m_placement( other.m_placement )
		, m_direction( other.m_direction )
		, m_drawStyle( other.m_drawStyle )
		, m_linkColor( other.m_linkColor )
		, m_isHidden( other.m_isHidden )
	{
	}

	SocketDescription::SocketDescription( SocketDescription&& other )
		: m_id( std::move( other.m_id ) )
		, m_caption( std::move( other.m_caption ) )
		, m_placement( std::move( other.m_placement ) )
		, m_direction( std::move( other.m_direction ) )
		, m_drawStyle( std::move( other.m_drawStyle ) )
		, m_linkColor( std::move( other.m_linkColor ) )
		, m_isHidden( std::move( other.m_isHidden ) )
	{
	}

	SocketDescription& SocketDescription::operator=( const SocketDescription& other )
	{
		m_id = other.m_id;
		m_caption = other.m_caption;
		m_placement = other.m_placement;
		m_direction = other.m_direction;
		m_drawStyle = other.m_drawStyle;
		m_linkColor = other.m_linkColor;
		m_isHidden = other.m_isHidden;
		return *this;
	}

	SocketDescription& SocketDescription::operator=( SocketDescription&& other )
	{
		SocketDescription( std::move( other ) ).Swap( *this );
		return *this;
	}

	void SocketDescription::Swap( SocketDescription& other )
	{
		std::swap( m_id, other.m_id );
		std::swap( m_caption, other.m_caption );
		std::swap( m_placement, other.m_placement );
		std::swap( m_direction, other.m_direction );
		std::swap( m_drawStyle, other.m_drawStyle );
		std::swap( m_linkColor, other.m_linkColor );
		std::swap( m_isHidden, other.m_isHidden );
	}

	//////////////////////////////////////////////////////////////////////////

	ConnectionBindings::ConnectionBindings()
		: m_sourceNode( InvalidGraphId )
		, m_destinationNode( InvalidGraphId )
		, m_sourceSocketIndex( InvalidSocketId )
		, m_destinationSocketIndex( InvalidSocketId )
		, m_id( InvalidGraphId )
	{
	}

	ConnectionBindings::ConnectionBindings( const ConnectionBindings& other )
		: m_sourceNode( other.m_sourceNode )
		, m_destinationNode( other.m_destinationNode )
		, m_sourceSocketIndex( other.m_sourceSocketIndex )
		, m_destinationSocketIndex( other.m_destinationSocketIndex )
		, m_id( other.m_id )
	{
	}

	ConnectionBindings::ConnectionBindings( GraphNodeId sourceNodeId, GraphSocketId sourceSocketId, GraphNodeId destinationNodeId, GraphSocketId destinationSocketId )
		: m_sourceNode( sourceNodeId )
		, m_destinationNode( destinationNodeId )
		, m_sourceSocketIndex( sourceSocketId )
		, m_destinationSocketIndex( destinationSocketId )
		, m_id( InvalidGraphId )
	{
	}

	Bool ConnectionBindings::operator==( const ConnectionBindings& c ) const
	{
		return m_id == c.m_id && // most comparisons will end here
			m_sourceNode == c.m_sourceNode &&
			m_destinationNode == c.m_destinationNode &&
			m_sourceSocketIndex == c.m_sourceSocketIndex &&
			m_destinationSocketIndex == c.m_destinationSocketIndex;
	}

	Bool ConnectionBindings::operator<( const ConnectionBindings& c ) const
	{
		return m_id != c.m_id
			? m_id < c.m_id
			: m_sourceNode != c.m_sourceNode
			? m_sourceNode < c.m_sourceNode
			: m_sourceSocketIndex != c.m_sourceSocketIndex
			? m_sourceSocketIndex < c.m_sourceSocketIndex
			: m_destinationNode != c.m_destinationNode
			? m_destinationNode < c.m_destinationNode
			: m_destinationSocketIndex < c.m_destinationSocketIndex;
	}

	//////////////////////////////////////////////////////////////////////////

	const CName ConnectionDescription::sm_kind = RED_NAME_CONSTEXPR( "ConnectionDescription" );

	ConnectionDescription::ConnectionDescription()
		: m_connectionType( "bezier" )
		, m_isDisabled( false )
		, m_isDirectional( false )
		, m_isInactive( false )
		, m_highlight( false )
		, m_highlightColor( Color::RED() )
		, m_thickness( 3 )
	{
	}

	ConnectionDescription::ConnectionDescription( const ConnectionDescription& other )
		: ConnectionBindings( other )
		, m_connectionType( other.m_connectionType )
		, m_isDisabled( other.m_isDisabled )
		, m_isDirectional( other.m_isDirectional )
		, m_isInactive( other.m_isInactive )
		, m_highlight( other.m_highlight )
		, m_highlightColor( other.m_highlightColor )
		, m_thickness( other.m_thickness )
	{
	}

	ConnectionDescription::ConnectionDescription( GraphNodeId sourceNodeId, GraphSocketId sourceSocketId, GraphNodeId destinationNodeId, GraphSocketId destinationSocketId )
		: ConnectionBindings( sourceNodeId, sourceSocketId, destinationNodeId, destinationSocketId )
		, m_connectionType( "bezier" )
		, m_isDisabled( false )
		, m_isDirectional( false )
		, m_isInactive( false )
		, m_highlight( false )
		, m_highlightColor( Color::RED() )
		, m_thickness( 3 )
	{
	}

	Bool ConnectionDescription::operator==( const ConnectionBindings& c ) const
	{
		return ConnectionBindings::operator==( c );
	}

	Bool ConnectionDescription::operator==( const ConnectionDescription& c ) const
	{
		return ConnectionBindings::operator==( c ) && m_isDisabled == c.m_isDisabled;
	}

	EditorObjectID ConnectionDescription::GetEditorObjectID() const
	{
		return EditorObjectID::Build( m_id, sm_kind );
	}
}
