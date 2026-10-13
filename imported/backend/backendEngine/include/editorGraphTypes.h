/*
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/redReflection/include/variant.h"

namespace tools
{
	typedef Uint64 GraphId;
	typedef Uint64 GraphNodeId;
	typedef Uint64 GraphConnectionId;
	typedef Uint64 GraphVariableId;
	typedef Uint64 GraphComponentId;

	typedef Uint32 GraphSubGraphId;
	typedef Uint32 GraphSocketId;

	const GraphId InvalidGraphId = static_cast< GraphId >( -1 );
	const GraphSocketId InvalidSocketId = static_cast< GraphSocketId >( -1 );
	const GraphVariableId InvalidVariableId = static_cast< GraphVariableId >( -1 );
	const GraphSubGraphId InvalidSubGraphId = static_cast< GraphSubGraphId >( -1 );
	const GraphComponentId InvalidComponentIndex = static_cast< GraphComponentId >( -1 );

	class GraphComponentDescription;
	typedef red::HashMap< GraphComponentId, THandle< GraphComponentDescription > > Components;

	typedef std::pair< CName, class ConnectionDescription > SocketNameConnectionPair;

	enum class SocketPlacement
	{
		Left,		//!< Place socket at the left side of the block
		Right,		//!< Place socket at the right side of the block
		Bottom,		//!< Place socket at the bottom of the block
		Center,		//!< Place socket at the center
		Title,		//!< Place socket at the title bar of the block
	};

	enum class SocketDrawStyle
	{
		Default,
		Circle,
		Arrow,
		InArrow,
	};

	enum class SocketDirection
	{
		Input,
		Output,
		InputOutput,
		Variable,
	};

	class GraphBackendData : public IBackendData
	{
	public:
		virtual void ChangeSelection( Bool isSelected ) {}
	};

	class BACKEND_ENGINE_API GraphComponentDescription : public GraphBackendData
	{
		RTTI_DECLARE_TYPE( GraphComponentDescription )
	public:
		GraphComponentDescription( const red::String& typeName );

		red::String m_componentType;
		GraphComponentId m_id;
	};

	class BACKEND_ENGINE_API VariantComponent : public GraphComponentDescription
	{
		RTTI_DECLARE_TYPE( VariantComponent )

	public:
		VariantComponent();

		rtti::Variant m_value;
	};

	/// Shows additional text box on a block just below horizontal sockets. Can be used to print errors, status etc.
	class BACKEND_ENGINE_API NodeCommentDescription : public GraphComponentDescription
	{
		RTTI_DECLARE_TYPE( NodeCommentDescription )
	public:
		NodeCommentDescription();

		red::String m_comment;
		Color m_color;
		Color m_backgroundColor;
	};

	/// Shows debugger/tracer control
	class BACKEND_ENGINE_API NodeDebuggerDescription : public GraphComponentDescription
	{
		RTTI_DECLARE_TYPE( NodeDebuggerDescription )
	public:
		NodeDebuggerDescription();

		// Debugger
		Bool m_debuggerShowBreakpoint; // show/hide breakpoint on block
		Bool m_debuggerBreakpoint;     // enabled/disabled breakpoint
		Bool m_debuggerHit;            // Debugger stopped on this node

		Bool m_tracerHit;			   // Tracker for block
	};

	class BACKEND_ENGINE_API SocketStringValueDescription : public GraphComponentDescription
	{
		RTTI_DECLARE_TYPE( SocketStringValueDescription )
	public:
		SocketStringValueDescription();

		String m_value; // value of the socket
	};

	class BACKEND_ENGINE_API NodeDescription : public GraphBackendData
	{
		RTTI_DECLARE_TYPE( NodeDescription );

		static const char* s_highlightFramePropertyName;
		static const char* s_highlightFrameColorPropertyName;
		static const char* s_infoPanelTextPropertyName;
		static const char* s_infoPanelTextColorPropertyName;
		static const char* s_infoPanelIconPropertyName;
		static const char* s_decorativeFramePropertyName;
		static const char* s_decorativeFrameColorPropertyName;

	public:	
		NodeDescription();
		NodeDescription( GraphId id );
		NodeDescription(const NodeDescription& desc);

		virtual EditorObjectID GetEditorObjectID() const override;
		virtual Bool CanBeRemoved( Bool isCut ) const { return true; }

		GraphId	m_id;

		/// Block data
		String m_blockType;
		String m_tooltip;
		String m_subtitle;
		Point m_position;

		Bool m_isSubGraphContainer;
		Bool m_isGraphInclude;
		Bool m_canRefresh;
		Bool m_canBeCloned;

		// TODO: Do we really need this here?
		red::String m_iconName;
		red::String m_caption;
		Color m_headerColor;
		Color m_captionColor;

		Bool m_highlightFrame;	// enable/disable additional frame around a block
		Color m_highlightFrameColor;

		Bool m_decorativeFrame;	// enable/disable second additional frame around a block
		Color m_decorativeFrameColor;

		red::String m_infoPanelText;
		Color m_infoPanelTextColor;
		red::String m_infoPanelIconName;

		// Used for included behavior trees, but can be used for other resources
		// in other node types in theory.
		red::String m_linkedResourcePath;
	};

	class BACKEND_ENGINE_API SocketBinding : public GraphBackendData
	{
		RTTI_DECLARE_TYPE( SocketBinding )
	public:
		SocketBinding();
		SocketBinding( GraphId nodeId, GraphSocketId socketId );

		virtual EditorObjectID GetEditorObjectID() const override;

		static const CName sm_kind;

		GraphId m_nodeId;
		GraphSocketId m_socketId;
	};

	class BACKEND_ENGINE_API SocketDescription : public GraphBackendData
	{
		RTTI_DECLARE_TYPE( SocketDescription )
	public:
		SocketDescription();
		SocketDescription( const red::String& caption, GraphSocketId id, SocketDirection direction );
		SocketDescription( const SocketDescription& other );
		SocketDescription( SocketDescription&& other );

		SocketDescription& operator= ( const SocketDescription& other );
		SocketDescription& operator= ( SocketDescription&& other );

		void Swap( SocketDescription& other );

		GraphSocketId m_id;

		/// Socket parameters
		SocketPlacement m_placement;
		SocketDrawStyle	m_drawStyle;
		SocketDirection m_direction;

		Bool m_isHidden;

		/// Socket visual parameters
		red::String m_caption;
		Color m_captionColor;

		// Deprecated stuff
		// TODO: Do we need this stuff?
		Color m_linkColor;
	};

	class BACKEND_ENGINE_API ConnectionBindings : public GraphBackendData
	{
	public:
		ConnectionBindings();
		ConnectionBindings( const ConnectionBindings& other );
		ConnectionBindings( GraphNodeId sourceNodeId, GraphSocketId sourceSocketId, GraphNodeId destinationNodeId, GraphSocketId destinationSocketId );

		Bool operator==( const ConnectionBindings& c ) const;
		Bool operator<( const ConnectionBindings& c ) const;

		GraphSocketId GetSourceSocketId() const			{ return m_sourceSocketIndex; }
		GraphSocketId GetDestinationSocketId() const	{ return m_destinationSocketIndex; }

		GraphNodeId m_sourceNode;
		GraphNodeId m_destinationNode;
		GraphSocketId m_sourceSocketIndex;		// why it is named SocketIndex while it stores sourceSocketId ?
		GraphSocketId m_destinationSocketIndex; // why it is named SocketIndex while it stores destinationSocketId
		GraphId m_id;
	};

	class BACKEND_ENGINE_API ConnectionDescription : public ConnectionBindings
	{
		RTTI_DECLARE_TYPE( ConnectionDescription )
	public:
		ConnectionDescription();
		ConnectionDescription( const ConnectionDescription& other );
		ConnectionDescription( GraphNodeId sourceNodeId, GraphSocketId sourceSocketId, GraphNodeId destinationNodeId, GraphSocketId destinationSocketId );

		Bool operator==( const ConnectionBindings& c ) const;
		Bool operator==( const ConnectionDescription& c ) const;

		virtual EditorObjectID GetEditorObjectID() const override;

		static const CName sm_kind;

		// Visual properties
		String m_connectionType;
		Bool m_isDisabled;
		Bool m_isDirectional;
		Bool m_isInactive;

		// Can manipulate Connection color
		Bool m_highlight;
		Color m_highlightColor;

		// connection thickness
		Uint32 m_thickness;
		CName m_name;

		// additional data that specifies in which version of game or given resource this connection was created, it is used in patching system, to determine which connections can be removed without causing any blockers for players
		Uint32 m_originVersion;
	};

	struct BACKEND_ENGINE_API NodeCreationInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( NodeCreationInfo )

	public:
		NodeCreationInfo();
		NodeCreationInfo( const String& type, const Point initialPosition );

		String m_data;
		Point m_initialPosition;
	};

	struct BACKEND_ENGINE_API ConnectionCreationInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( ConnectionCreationInfo )

	public:
		ConnectionCreationInfo();
		ConnectionCreationInfo( GraphId sourceNodeId, GraphSocketId sourceSocketId, GraphId destNodeId, GraphSocketId destSocketId, const Bool isDisabled = false );

		GraphId m_sourceNodeId;
		GraphSocketId m_sourceSocketId;
		GraphId m_destinationNodeId;
		GraphSocketId m_destinationSocketId;
		String m_data;
		Bool m_isInactive;
		Bool m_isDisabled;
	};

	enum class BACKEND_ENGINE_API SearchProcessType : Uint32
	{
		Label		= RED_FLAG( 0 ),
		Type		= RED_FLAG( 1 ),
		Comment		= RED_FLAG( 2 ),
	};

} // namespace tools

RTTI_DECLARE_ENUM_IN_NAMESPACE( SearchProcessType, tools );
RTTI_DECLARE_ENUM_IN_NAMESPACE( SocketPlacement, tools );
RTTI_DECLARE_ENUM_IN_NAMESPACE( SocketDrawStyle, tools );
RTTI_DECLARE_ENUM_IN_NAMESPACE( SocketDirection, tools );
