/*
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/redReflection/include/resourceMonitor.h"

#include "editorGraphEx.h"
#include "graphDescriptorNodesMap.h"
#include "graphEditorConnection.h"
#include "graphValidator.h"

class C2dArray;

namespace tools
{
	class GraphObjectTreeBuilder;
	class IGraphValidator;
	class IGraphValidationContext;
	class GraphValidatorOutput;
	class CNodeClassesDescriptorStructure;
	struct SocketInfo;

	class GraphImportTreeBuilder;

	class BACKEND_ENGINE_API IGraphDescriptorGlobalData : public IBackendData
	{
		RTTI_DECLARE_TYPE( IGraphDescriptorGlobalData )
	public:
		IGraphDescriptorGlobalData();

		Uint32 AllocateSubgraphId() { return m_id++; }

		Bool Initialize(const THandle< CResource >& graphRes);

		// Variables support
		virtual void ListVariableTypes( red::DynArray<String>& types ) {  }
		virtual void GetVariables( red::DynArray< GraphVariableId >& variableIds ) { }
		
		virtual THandle< ISerializable > GetVariableData( GraphVariableId variableId ) { return nullptr; }
		virtual GraphVariableId CreateVariable( const String& type ) { return InvalidGraphId; }
		virtual Bool RemoveVariable( GraphVariableId variableId ) { return false; }
	
		virtual void GetVariableType( GraphVariableId variableId, String& type ) { }
		virtual void GetVariableName( GraphVariableId variableId, String& name ) { }
		virtual Bool SetVariableName( GraphVariableId variableId, const String& name ) { return false; }

	protected:
		virtual Bool OnInitialize(const THandle< CResource >& graphRes) { return true; }

	private:
		Uint32 m_id;
	};

	class BACKEND_ENGINE_API IGraphDescriptor : public IBackendData
	{
		typedef IBackendData Super;
		RTTI_DECLARE_TYPE( IGraphDescriptor )

	protected:
		typedef std::function< void( const String& ) > TErrorFunc;

	public:
		struct NodeInspector
		{
			virtual void InspectNode( IGraphNodeDescriptor* node ) = 0;
		};

		IGraphDescriptor();
		virtual ~IGraphDescriptor();

		void										SetSubGraphId( GraphSubGraphId subGraphId );
		GraphSubGraphId								GetSubGraphId() const														{ return m_subGraphId; }
		GraphId										AllocateGraphId();
		GraphId										GetLastId() const;

		void										AttachListener( IEditorGraphListener* listener );
		void										DetachListener( IEditorGraphListener* listener );
		void										DetachRecursiveListener( IEditorGraphListener* listener );

		Bool										Initialize( const THandle< CResource >& graphRes, const THandle< IGraphNodeDescriptor > parentNode = THandle< IGraphNodeDescriptor >(), Uint64 highestPreallocatedGraphId = InvalidGraphId );	// Initialize graph for edition. Time to cache stuff, check if descriptors are valid and stuff
		// WCOTODO: remove global data from Create parameters, it should be runtime only
		// Create a descriptor for the first time. TODO: would be cool to support a case where are raw graph data, just descriptors are gone.
		Bool										Create( const THandle< CResource >& graphRes, THandle< IGraphDescriptorGlobalData > globalData, Bool mappingOnly );
		Bool										Validate( const IGraphValidator& validator, IGraphValidationContext& context, GraphValidatorOutput& output );
		void										Open();

		void										MarkModified();
		const res::ResourcePath						GetResourcePath() const;

		void										PropagateGlobalData( const THandle< IGraphDescriptorGlobalData >& globalData );
		THandle< IGraphDescriptorGlobalData >		GetGlobalData() const														{ return m_globalData; }
		const GraphDescriptorNodesMap&				GetNodesMap() const															{ RED_ASSERT( m_nodesMap ); return *m_nodesMap; }
		const GraphNodesList&						GetNodesList() const														{ return m_nodes; }
		GraphConnectionsList&						GetConnectionsMap()															{ return m_connections; }
		const GraphConnectionsList&					GetConnectionsMap() const													{ return m_connections; }
		const WeakHandle< CResource >&				GetParentResource() const													{ return m_parentResource;  }
		void										SetParentResource( const THandle< CResource >& graphResource );
		const WeakHandle< IGraphNodeDescriptor >&	GetGraphOwnerNode() const													{ return m_ownerNode; }
		Bool										IsInitialized() const														{ return m_isInitializedForEdition; }
		
		virtual void								DropData( Point position, const String& data );

		// Class filtering
		Bool										IsClassSupported( const rtti::ClassType* classType );

		virtual void								GatherRootNodesClasses( red::DynArray< const rtti::ClassType* >& nodeRootClasses ) const = 0;
		virtual void								GatherNodesClasses(red::DynArray< const rtti::ClassType* >& nodeClasses) const {};
		virtual Bool								IsRootSubClassSupported( const rtti::ClassType* classType ) const;
		virtual Bool								IsRuntimeNodeSupported( const THandle< IGraphNodeDescriptor >& defaultNode ) const;
		virtual Bool								IsValidatorClassSupported( const rtti::ClassType* validator ) const;

		virtual THandle< CNodeClassesDescriptorStructure >	GenerateSupportedNodesStructure() const;

		virtual void								FillClassTree( GraphObjectTreeBuilder& treeBuilder );
		virtual void								FillImportTree( GraphObjectTreeBuilder& treeBuilder ) const;

		// Graph iteration
		virtual void								InspectNodes( NodeInspector& nodeInspector );
		virtual void								IterateNodes( std::function<void(IGraphNodeDescriptor*)> preVisitor, std::function<void(IGraphNodeDescriptor*)> postVisitor );

		// Node creation
		virtual THandle< IGraphNodeDescriptor >		CreateNode( const THandle< ISerializable >& context, const rtti::ClassType* descriptorClass, const String& actualType, Bool forceOperation = false, Point position = Point(0, 0) );
		virtual Bool								CanRemoveNode( GraphNodeId nodeId, Bool isCut ) const;
		virtual Bool								RemoveNode( GraphNodeId nodeId, Bool forceOperation = false );
		virtual Bool								RestoreNode( const THandle< IGraphNodeDescriptor >& node );
		virtual Bool								PlaceNode( const THandle< IGraphNodeDescriptor >& clone, Bool isInPlace );

		// Connectivity
		Bool										CanCreateConnection( const ConnectionCreationInfo& info ) const;
		Bool										CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const THandle< IGraphNodeDescriptor >& defaultDescriptor ) const;
		Bool										CanCreateConnection( GraphNodeId outputNodeId, GraphSocketId outputSocketId, const THandle< IGraphNodeDescriptor >& defaultDescriptor, GraphSocketId inputSocketId ) const;
		Bool										CanCreateIntermediateNode( const ConnectionCreationInfo& info, const THandle< IGraphNodeDescriptor >& defaultDescriptor ) const;
		Bool										CanRemoveIntermediateNode( GraphNodeId nodeId, red::DynArray< ConnectionCreationInfo >& outConnectionInfos ) const;
		GraphId										CreateConnection( const ConnectionCreationInfo& info, Bool force = false );
		Bool										RemoveAllConnections( const THandle< IGraphNodeDescriptor >& node );
		Bool										RemoveAllConnectionsExcept( const THandle< IGraphNodeDescriptor >& node, GraphId exceptionConnectionId );
		Bool										CanRemoveConnection( GraphConnectionId connectionId ) const;
		Bool										RemoveConnection( GraphConnectionId connectionId );
		Bool										RemoveConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& desc );
		Bool										RestoreConnection( const THandle< ConnectionDescription >& connection, const THandle< ISerializable >& data );
		// WCTODO: this should be renamed (also not sure if its current use is correct, create->place)
		Bool										PlaceConnection( GraphConnectionId connectionId, const THandle< ConnectionDescription >& original, const THandle< ISerializable >& data );
		Bool										SetConnectionDisabled( GraphConnectionId connectionId, Bool disabled, TErrorFunc onError );
		Bool										SetConnectionHighlight( GraphConnectionId connectionId, Color highlightColor );
		Bool										ClearConnectionHighlight( GraphConnectionId connectionId );

		// Connection data
		THandle< ISerializable >					GetConnectionData( GraphConnectionId connectionId );
		virtual	THandle< ISerializable >			GetConnectionData( const SocketInfo& source, const SocketInfo& destination, GraphConnectionId connectionId );
		THandle< ConnectionDescription >			GetConnection( GraphConnectionId connectionId );
		const THandle< ConnectionDescription >		GetConnection( GraphConnectionId connectionId ) const;
		virtual void								OnConnectionDataChanged( GraphId connectionId, const rtti::AccessPath& path );
		
		virtual void								OnSerialize( IFile& file ) override;
		virtual void								OnPreSave( const PreSaveContext& context ) override;
		virtual void								OnPostLoad( const PostLoadContext& context ) override;

		// UI invalidation functions
		void										NotifyGraphReloaded();
		void										NotifyGraphIdChanged( GraphSubGraphId oldId, GraphSubGraphId newId );
		void										NotifyNodeCreated( GraphNodeId nodeId );
		void										NotifyNodeRemoved( GraphNodeId nodeId );
		void										NotifyConnectionCreated( GraphId connectionId );
		void										NotifyConnectionRemoved( GraphId connectionId );
		void										NotifyConnectionEnabled( GraphId connectionId );
		void										NotifyConnectionDisabled( GraphId connectionId );
		void										NotifyNodeSocketsCreated( GraphId nodeId );
		void										NotifyNodeSocketsChanged( GraphId nodeId );
		void										NotifyNodeSocketsRemoved( GraphId nodeId, const red::DynArray< GraphSocketId >& socketIds );
		void										NotifySelectionChanged( const red::DynArray< GraphId >& nodeIds );
		void										InvalidateNodeDescription( GraphNodeId nodeId );
		void										InvalidateNodeDescriptionProperty( GraphNodeId nodeId, const red::String& propertyName );
		void										InvalidateNodeData( GraphNodeId nodeId );
		void										InvalidateNodeComponent( GraphNodeId nodeId, GraphComponentId componentId );
		void										InvalidateSocketDescription( GraphNodeId nodeId, GraphSocketId socketId );
		void										InvalidateSocketData( GraphNodeId nodeId, GraphSocketId socketId );
		void										InvalidateSocketComponent( GraphNodeId nodeId, GraphSocketId socketId, GraphComponentId componentId );
		void										InvalidateConnectionDescription( GraphConnectionId connectionId );
		void										InvalidateConnectionDescriptionProperty( GraphConnectionId connectionId, const String& propertyName );
		void										InvalidateConnectionData( GraphConnectionId connectionId );
		void										InvalidateConnectionComponent( GraphConnectionId connectionId, GraphComponentId componentId );
		void										CloseSubgraph( GraphSubGraphId subgraphId );

		// Variables
		Bool										IsSupportVariable() const;
		void										ListVariableTypes( red::DynArray<String>& types );
		void										GetVariables( red::DynArray< GraphVariableId >& variableIds );
		
		THandle< ISerializable >					GetVariableData( GraphVariableId variableId );
		GraphVariableId								CreateVariable( const String& type );
		Bool										RemoveVariable( GraphVariableId variableId );
	
		void										GetVariableType( GraphVariableId variableId, String& type );
		void										GetVariableName( GraphVariableId variableId, String& name );
		Bool										SetVariableName( GraphVariableId variableId, const String& name );

		// Components
		GraphComponentId							CreateNodeComponent( GraphId nodeId, const String& type );
		GraphComponentId							CreateSocketComponent( GraphId nodeId, GraphSocketId socketId, const String& type );
		GraphComponentId							CreateConnectionComponent( GraphId connectionId, const String& type );

		void										GetNodeComponents( GraphId nodeId, red::DynArray< GraphComponentId >& outComponents ) const;
		void										GetSocketComponents( GraphId nodeId, GraphSocketId socketId, red::DynArray< GraphComponentId >& outComponents ) const;
		void										GetConnectionComponents( GraphConnectionId connectionId, red::DynArray< GraphComponentId >& outComponents ) const;

		THandle< GraphComponentDescription >		GetNodeComponent( GraphId nodeId, GraphComponentId componentId );
		THandle< GraphComponentDescription >		GetSocketComponent( GraphId nodeId, GraphSocketId socketId, GraphComponentId componentId );
		THandle< GraphComponentDescription >		GetConnectionComponent( GraphConnectionId connectionId, GraphComponentId componentId );

		virtual void								OnGetConnectionComponent( GraphConnectionId connectionId, GraphComponentDescription* component ) { }

		// Custom actions
		virtual ISerializable*						GetData() const;
		virtual const String&						GetName() const;
		virtual Bool								IsTopDownTree() const;
		virtual Bool								HasClipboardSupport() const;
		virtual Bool								HasCloningSupport() const;

	protected:
		// custom editor initialization stuff
		virtual Bool								OnInitialized( const THandle< CResource >& graphRes );
		virtual Bool								OnCreate( const THandle< CResource >& graphRes, Bool mappingOnly );
		virtual void								OnPropagateGlobalData( const THandle< IGraphDescriptorGlobalData >& globalData );
		virtual Bool								OnValidate( const IGraphValidator& validator, const IGraphValidationContext& context, GraphValidatorOutput& output );
		virtual void								OnOpened();

		virtual void								OnNodeCreated( const THandle< IGraphNodeDescriptor >& node );
		virtual void								OnNodeRemoved( const THandle< IGraphNodeDescriptor >& node );

		virtual void								OnConnectionCreated( const THandle< ConnectionDescription >& description );
		virtual Bool								OnRestoreConnection( const SocketInfo& sourceInfo, const SocketInfo& destinationInfo, GraphId connectionId, const THandle< ISerializable >& data );
		virtual Bool								OnPlaceConnection( const SocketInfo& sourceInfo, const SocketInfo& destinationInfo, GraphId connectionId, const THandle< ISerializable >& data );

		virtual THandle< GraphComponentDescription > OnCreateConnectionComponent( GraphId connectionId, const String& type );

		void										AttachNode( const THandle< IGraphNodeDescriptor >& node );
		void										DetachNode( const THandle< IGraphNodeDescriptor >& node );
		void										DistributeNode( const THandle< IGraphNodeDescriptor >& node );
		void										ReadImportsFile( const THandle< C2dArray >& itemsArray, GraphObjectTreeBuilder& treeBuilder ) const;

		virtual void								OnConnectionDisabled(const SocketInfo& source, const SocketInfo& destination, GraphId connectionId) {}
		virtual void								OnConnectionEnabled(const SocketInfo& source, const SocketInfo& destination, GraphId connectionId) {}

		THandle< IGraphNodeDescriptor >				GetNode( GraphNodeId id );
		const THandle< IGraphNodeDescriptor >		GetNode( GraphNodeId id ) const;

		// static data
		GraphConnectionsList						m_connections;
		GraphNodesList								m_nodes;

		// cached editor-only data
		GraphDescriptorNodesMap*					m_nodesMap;
		Bool										m_isInitializedForEdition;

		red::DynArray< IEditorGraphListener* >      m_graphListner;

		WeakHandle< IGraphDescriptorGlobalData >	m_globalData;

		WeakHandle< CResource >						m_parentResource;													// possibly null
		WeakHandle< IGraphNodeDescriptor >			m_ownerNode;														// mostly null - subgraph feature
		
	private:
		Bool										CanRemoveNode( const THandle< IGraphNodeDescriptor >& node, Bool isCut ) const;

		void										InitializeIdAllocator( Uint64 highestPreallocatedGraphId );
		
		GraphSubGraphId								m_subGraphId; // id of subgraph, generated during runtime

		Uint64										m_idFactory;

		String										m_name;

		red::HashMap< GraphConnectionId, Components	> m_connectionsComponents;

		red::UniquePtr< res::Monitor >				m_resourceMonitor;
	};

	class BACKEND_ENGINE_API IGraphImporter
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IGraphImporter() = default;
		virtual Bool CopyGraphDescriptor( const res::ResourcePath& path, const Point& position ) = 0;

	protected:
		void GatherPaths( const tools::IGraphDescriptor& graphDescriptor, red::DynArray< tools::EditorObjectIDPath >& outPaths ) const;
		void UpdateNodePositions( tools::IGraphDescriptor& graphDescriptor, const Point& offset );
	};

	class BACKEND_ENGINE_API GraphValidatorOutput : public IGraphValidatorOutput
	{
	public:
		GraphValidatorOutput( IGraphDescriptor* graphDescriptor = nullptr );

		virtual void LogWarning( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );
		virtual void LogError( STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );

		void LogWarning( const IGraphNodeDescriptor* node, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );
		void LogError( const IGraphNodeDescriptor* node, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );

		void LogWarning( ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );
		void LogError( ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, ... );

		virtual Uint32 GetWarningsCount() const override { return m_warningsCount; }
		virtual Uint32 GetErrorsCount() const override { return m_errorsCount; }
		virtual GraphValidationState GetState() const override { return m_state; }
		virtual const red::DynArray< GraphValidatorOutputEntry >& GetEntries() const override { return m_entries; }

	private:
		void Log( GraphValidatorOutputEntryType type, const red::DynArray< GraphId >& graphIdChain, ValidatorEntryAction fixAction, STATIC_CHECK_PRINTF_MSC const AnsiChar* message, va_list args );
	
		void UpdateState();
		void GenerateGraphIdChainPerNodeDictionary( IGraphDescriptor* graphDescriptor );

		Uint32 m_warningsCount;
		Uint32 m_errorsCount;
		GraphValidationState m_state;
		red::SortedArray< GraphValidatorOutputEntry, GraphValidatorOutputEntry::SortFunction > m_entries;

		red::HashMap< const IGraphNodeDescriptor*, red::DynArray< GraphId > > m_graphIdChainPerNode;
	};

	class BACKEND_ENGINE_API IGraphValidationContext
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IGraphValidationContext() {}

		virtual void OnStartedNodeValidation( const IGraphNodeDescriptor* nodeDescriptor ) = 0;
		virtual void OnEndedNodeValidation( const IGraphNodeDescriptor* nodeDescriptor ) = 0;
		virtual String GetPath() const = 0;
	};

	class BACKEND_ENGINE_API IGraphValidator
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( IGraphValidator )

	public:
		virtual red::UniquePtr< IGraphValidationContext > CreateContext() const = 0;
		virtual void ProcessDescriptor( const IGraphNodeDescriptor* descriptor, const IGraphValidationContext& context, GraphValidatorOutput& output ) const = 0;
		virtual void ProcessDescriptor( const IGraphDescriptor* descriptor, const IGraphValidationContext& context, GraphValidatorOutput& output ) const = 0;
	};

} // namespace tools