/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "selectionFwd.h"
#include "undoFwd.h"
#include "editableFwd.h"
#include "objectResolver.h"
#include "backendMemory.h"

namespace rtti
{
	class ValueHolder;
}

namespace tools
{
	/// Object creation info
	struct BACKEND_ENGINE_API ObjectCreationInfo
	{
		Transform							m_transform;
		Vector3								m_scale;
		red::SharedPtr< rtti::ValueHolder >	m_arguments;			// object initialization string, BOXED, understood only by the object handler
		THandle< ISerializable >			m_sourceObject;			// optional source object (if null, the object resolver will construct brand new object instead)
		EditorObjectIDPath					m_customTarget;
		Bool								m_clone;
		Bool								m_useCustomTarget;		// if true, the object will be created inside m_customTarget (instead of active path, if resolver supports this feature)

		// the IDs are allocated in the constructor
		// arguments are essential for this to work
		explicit ObjectCreationInfo( const String& arguments );
		ObjectCreationInfo( const red::SharedPtr< rtti::ValueHolder >& arguments );

		void ProcessSpecialArguments();

		static const CName c_specialArgumentName;              // Processed in ProcessSpecialArguments.
	};

	/// Object placement info
	struct BACKEND_ENGINE_API ObjectPlacementInfo
	{
		// absolute transform to apply to placed object(s) - NOTE, the first object will have exactly this transform, the next ones will be relative
		Transform m_transform;

		// enable the transform shift
		Bool m_useAbsoluteTransform;

		// If true, the pasted object will be placed at the same world space location.
		// Otherwise, parent-space location will be preserved.
		Bool m_preserveWorldSpaceLocation;

		ObjectPlacementInfo();
	};

	/// Object preview - helper for temporary object visualization, especially useful for Drag&Drop placement
	class BACKEND_ENGINE_API IObjectPreview
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( IObjectPreview );
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IObjectPreview() {};

		/// destroy visualization
		virtual void OnDestroy() = 0;;

		/// move to new location
		virtual void OnMove( const Transform& newTransform ) = 0;

		/// handle preview rendering (sometimes it's not needed)
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) = 0;
	};

	typedef red::SharedPtr< IObjectPreview > ObjectPreviewPtr;

	/// Manages abstract "objects" inside the editor (or particular edit mode)
	/// Usually this is done via the IObjectResolver
	/// The handler provides the undo/redo and copy/paste functionalities
	/// NOTE: If you want to have multiple TYPES of object in your editor you need to create many handlers as they are NOT compatible 
	/// NOTE: This is an architectural choices for the sake of simplicity of design
	/// NOTE: You will still have ONE selection manager though
	/// NOTE2: This system DOES NOT provide notifications directly, if you want to listen for object changes you need to implement this in your object resolver directly
	class BACKEND_ENGINE_API ObjectHandler
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ObjectHandler );

	public:
		using TObjectResolvers = red::DynArray< IObjectResolver* >;

		ObjectHandler( IEditActionHistory& actionHistory, IObjectClipboard* clipboard, ISelectionHandler* selectionHandler );
		virtual ~ObjectHandler();

		/// Directly find backing SERIALIZABLE object (without the wrapping proxy) for object with given ID path
		THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const IObjectResolver::ResolvingParams& params = IObjectResolver::ResolvingParams() ) const;
		THandle< ISerializable > ResolveObject( const EditorObjectID& id ) const;

		/// Find backing editable proxy for object with given ID path
		EditableProxyPtr ResolveProxy( const EditorObjectIDPath& path, const IObjectResolver::ResolvingParams& params = IObjectResolver::ResolvingParams() ) const;
		EditableProxyPtr ResolveProxy( const EditorObjectID& id ) const;

		/// Path to the first found active object among all ObjectResolvers
		EditorObjectIDPath GetActiveObjectIDPath() const;

		/// Create object from given list of object informations
		/// Creates and undo step
		/// NOTE: the object is NOT returned here as in most cases we have no use for it directly (all the communications is going through the callbacks in the resolver anyway)
		/// Created object(s) will be selected
		Bool CreateObject( const ObjectCreationInfo& initInfo, Bool refreshSelection = false );
		Bool CreateObjects( const red::DynArray<ObjectCreationInfo>& initInfo );

		/// Check if we can create object
		Bool CanCreateObject( const ObjectCreationInfo& initInfo, String* outReason = nullptr );

		/// Delete object(s) with given GlobalIDs
		/// Creates and undo step, the deleted object will be deselected
		Bool DeleteObjects( const red::DynArray< EditorObjectIDPath >& paths, const Bool isShiftModifier = false, Bool isCut = false );

		/// Delete current selection
		/// Creates and undo step, the deleted object will be deselected - selection will be cleared
		Bool DeleteSelection( const Bool isShiftModifier, Bool isCut = false );

		/// Copy selection into the clipboard interface
		Bool CopyObjects( Bool isCut = false );

		/// Copy specified objects into the clipboard
		Bool CopyObjects( const red::DynArray< EditorObjectIDPath >& paths, Bool isCut = false );

		/// Cut selection into the clipboard interface
		Bool CutObjects();

		/// Clone selection into the clipboard interface
		Bool CloneObjects();

		/// Select all the objects resolvable by the current object resolver
		Bool SelectAll();

		/// Clear content of clipboard
		void ClearClipboard();

		/// Paste objects
		Bool PasteObjects();

		/// Ungroup objects
		Bool UngroupObjects( const red::DynArray< EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds );

		/// Copy and paste given objects
		/// If a placement transform is specified it's used to place the restored objects at specified place
		/// If it's not specified the objects are restored at their original positions
		/// Pasted objects will be selected
		/// Doesn't use copy-paste functionality so it doesn;t overwitre clipboard!
		Bool MoveObjects( const red::DynArray< EditorObjectIDPath >& paths, const ObjectPlacementInfo& placementInfo = ObjectPlacementInfo() );

		/// Restore (paste) object from the clipboard
		/// If a placement transform is specified it's used to place the restored objects at specified place
		/// If it's not specified the objects are restored at their original positions
		/// Pasted objects will be selected
		Bool PasteObjects( const ObjectPlacementInfo& placementInfo, const Bool selectPastedObjects = true );

		/// Replace selected objects with copies of the stuff in the clipboard
		/// The position of the 0th object in the clipboard buffer is taken as the reference
		Bool ReplaceSelectedObjects( const Bool selectPastedObjects = true );

		/// Select objects similar to given object template
		Bool SelectSimilarObjects( const EditorObjectIDPath& objectTemplate, const String& settings );

		/// set new selection based on given IDs
		void SetSelectionFromObjectIDs(const red::DynArray< EditorObjectID >& ids );
		void SetSelectionFromObjectIDPaths( const red::DynArray< EditorObjectIDPath >& paths );
		//---

		/// Create preview object without creating the actual object
		/// THIS SHOULD NOT MODIFY ANYTHING
		ObjectPreviewPtr CreateObjectPreview( const ObjectCreationInfo& initInfo );
		Bool CreateObjectsPreview( const red::DynArray< ObjectCreationInfo >& initInfo, red::DynArray< ObjectPreviewPtr >& outPreviews );

		//----

		/// Attach or detach object resolver
		/// NOTE: the resolvers MUST work with disconnected spaces of EditorObjectIDs
		/// NOTE: the object handler does not take ownership of the object resolvers
		void AttachObjectResolver( IObjectResolver* resolver );
		void DetachObjectResolver( IObjectResolver* resolver );
		
		/// Get all attached object resolver
		const TObjectResolvers& GetObjectResolvers() const { return m_resolvers; }

		// #active_object_resolver TODO: Change names of GetUnifiedResolver to FindUnifiedResolver
		/// Get a single matching object resolver for given objects
		IObjectResolver* GetUnifiedResolver( const red::ArraySpan< const EditorObjectID > ids ) const;
		IObjectResolver* GetUnifiedResolver( const red::ArraySpan< const EditorObjectIDPath > paths, Bool mustBeEditable = true ) const;
		IObjectResolver* GetUnifiedResolver( const SelectionStatePtr& selection, Bool forEditing = true ) const;
		IObjectResolver* GetUnifiedResolver( const red::ArraySpan< const ObjectCreationInfo >& creationInfos ) const;

		/// Get a single matching object resolver for serialized objects
		IObjectResolver* GetUnifiedResolver( const red::SharedPtr< IObjectPersistentCollection >& objects ) const;

		IObjectResolver* FindObjectResolver( const EditorObjectIDPath& path, Bool forEditing = true ) const;
		IObjectResolver* FindObjectResolver( const ObjectCreationInfo& creationInfo ) const;

		template < typename T >
		T* FindObjectResolver() const
		{
			static_assert(std::is_base_of< IObjectResolver, T >::value, "Requested type is not an Object Resolver");
			IObjectResolver* const resolver = InternalFindObjectResolver( ClassID< T >() );
			return Cast< T >( resolver );
		}
		
		//----

		RED_FORCE_INLINE ISelectionHandler* GetSelectionHandler() const { return m_selectionHandler; }

		/// collect resources used by given objects
		void CollectUsedResources(const red::DynArray< EditorObjectIDPath >& paths, red::DynArray<String>& outPaths) const;

		const IEditActionHistory* GetActionHistory() const { return m_actionHistory; }

	private:
		IObjectResolver* InternalFindObjectResolver( const rtti::ClassType* classType ) const;

		TObjectResolvers m_resolvers;

		// external clipboard implementation
		IObjectClipboard*	m_clipboard;

		// parent undo/redo interface
		IEditActionHistory*	m_actionHistory;

		ISelectionHandler* m_selectionHandler;

		//----

		/// get IDs of the selected objects (current selection)
		void GetSelectedObjectIDs( red::DynArray< EditorObjectID >& outIDs ) const;

		/// get ID paths of the selected objects
		void GetSelectedObjectIDPaths(red::DynArray< EditorObjectIDPath >& outIDPaths) const;
	};

} // tools
