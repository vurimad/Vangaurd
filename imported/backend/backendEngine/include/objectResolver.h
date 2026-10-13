/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "editableFwd.h"
#include "undoFwd.h"
#include "backendMemory.h"

#include "../../../common/redReflection/include/editorObjectIdPath.h"

namespace tools
{

	/// Every edition tools requires a way to identify objects that are edited
	/// we choose to use a GUID for this as it's the safest bet.
	/// Various internal tools like require ID -> object lookup to work and this interface is something that will deal with that
	class BACKEND_ENGINE_API IObjectResolver
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( IObjectResolver );
		RED_USE_MEMORY_POOL( red::PoolBackend );

		IObjectResolver( const IObjectResolver& ) = delete;
		IObjectResolver& operator = ( const IObjectResolver& ) = delete;

	public:

		// editable proxy resolving parameters
		struct ResolvingParams
		{
			bool m_ignoreLocked;	// don't resolve object that are locked for editing
			bool m_editInstance;	// edit object as instance
			bool m_resolveAsClone;	// is object supposed to be later cloned
			bool m_resolveAsCut;	// is object supposed to be cut
			bool m_isForSelection; // is object supposed to be selected

			ResolvingParams()
				: m_ignoreLocked( true )
				, m_editInstance( false )
				, m_resolveAsClone( false )
				, m_resolveAsCut( false )
				, m_isForSelection( false )
			{ }
		};

		// parameters for capturing objects
		struct CapturingParams
		{
			bool m_captureAsClone;	// is object supposed to be cloned instead of copied
			bool m_captureAsCut;	// is object supposed to be cut instead of copied

			CapturingParams()
				: m_captureAsClone( false )
				, m_captureAsCut( false )
			{ }
		};

		// output parameters for Action_CreateObjects method
		struct CreateObjectsOutParams
		{
			red::DynArray< EditorObjectIDPath >& actualCreatedPaths;	// created objects ID paths
			red::DynArray< EditorObjectIDPath >* objectsToSelect;		// objects that should be selected after creation
			Bool& selectAll;											// override outObjectsToSelect, select all created objects
		};

		// context for delete operation
		struct DeleteContext
		{
			red::ArraySpan< const EditorObjectIDPath > pathsToDelete;
			red::DynArray< EditorObjectIDPath > outObjectsToSelect;
		};

		IObjectResolver();
		virtual ~IObjectResolver();

		///---

		/// Get object handler we are cooperating with
		RED_FORCE_INLINE ObjectHandler& GetObjectHandler() { return *m_handler; }
		RED_FORCE_INLINE const ObjectHandler& GetObjectHandler() const { return *m_handler; }

		///---

		/// Called when we are attached to an object handler
		virtual void AttachedToHandler( ObjectHandler* objectHandler );

		/// Called when we are detached to an object handler
		virtual void DetachedFromHandler( ObjectHandler* objectHandler );

		/// If resolver is exclusive, the ObjectHandler will skip other resolvers during iteration
		virtual Bool IsExclusive() const;

		/// Get active object ID path (target for newly created objects)
		/// This usually will be empty (for editors that expect only single item in EditorObjectIDPath)
		virtual EditorObjectIDPath GetActiveObjectIDPath() const;
		
		/// Returns true if this object resolver manages object of given ID
		/// Usually this is checked using the Kind of the object
		/// If 'forEditing' is true then the function returns true only for objects can be edited using this object resolver
		/// TODO remove 'forEditing' parameter
		virtual Bool IsResolvable( const EditorObjectIDPath& path, Bool forEditing = true ) const = 0;

		/// Collect all the editor object IDs that are resolvable by this object resolver.
		/// This is useful for implementing "select all" feature.
		virtual void CollectResolvableObjects( red::DynArray< EditorObjectIDPath >& outPaths ) const;

		/// Check if a content type is serviced by the resolver.
		/// This is used to deliver the copy/paste data to proper places.
		virtual Bool SupportsContentType( const CName& contentTypeName ) const = 0;

		/// Find object by given global ID path, will create editable proxy for it if found.
		virtual EditableProxyPtr ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const = 0;

		/// Find backing serializable object with given ID path
		/// NOTE: use with care as you don't have the proper context like in the editable proxy
		/// NOTE: should return the same thing as ResolveProxy(path)->GetSerializable()
		virtual THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const = 0;

		/// Can we create object from given creation info?
		/// If not, the string describing the reason will be returned
		virtual Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const = 0;

		/// Can we delete given object?
		/// If not, the string describing the reason will be returned
		virtual Bool CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason = nullptr ) const = 0;

		/// Find set of similar objects
		virtual void FindSimilarObjects( const EditorObjectIDPath& templateObjectPath, const String& settings, red::DynArray< EditorObjectIDPath >& outSimilarObjectIds ) const;

		/// Find previous/next object in the current objects container (e.g. next point on a spline).
		virtual EditorObjectIDPath FindNeighboringObject( const EditorObjectIDPath& currentObjectPath, Bool forward ) const;

		/// Create preview object without creating the actual object
		/// THIS SHOULD NOT MODIFY ANYTHING
		virtual red::SharedPtr< IObjectPreview > CreateObjectPreview( const ObjectCreationInfo& initInfo ) = 0;

		/// Capture objects into a persistent collection (aka. clipboard)
		/// The collection class must be implemented to handle all internal details of object copy/pasting in a a sealed way
		/// NOTE: the persistent collection MUST be machine-independent so it can be sent through the mail/hipchat etc and/or between different instances of the editor
		virtual red::SharedPtr< IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) = 0;

		/// Create objects from given creation info
		/// Always done from inside the action as this is wrapped in the undo/redo step
		/// Top level interface is written in the ObjectHandler
		/// Returns captured objects just after their creation, the object capture is used to implement
		virtual red::SharedPtr< IObjectPersistentCollection > Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out ) = 0;

		/// Ability to inject custom delete actions with extra functionality
		virtual red::UniquePtr< IEditAction > CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const;

		/// Create pre-delete action that must be performed before Action_DeleteObjects action
		/// Used by ObjectHandler
		virtual red::UniquePtr< IEditAction > GetPreDeleteAction( const red::ArraySpan< const EditorObjectIDPath >& paths ) const;

		/// Delete objects with given IDs
		/// Always done from inside the action as this is wrapped in the undo/redo step
		/// Top level interface is written in the ObjectHandler
		/// NOTE: this may be a standalone delete or a cut operation
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) = 0;

		/// Restore object from persistent storage
		/// UNDO STEP ONLY, objects cannot be additional modified
		virtual Bool Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) = 0;

		/// Place object from persistent storage
		/// Always done from inside the action as this is wrapped in the undo/redo step
		/// Top level interface is written in the ObjectHandler
		virtual Bool Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) = 0;

		/// Replace existing objects with a copy of stuff in the persistent storage
		/// NOTE: the storage may contain more than one object so a multiple IDs can be returned of every incoming one
		/// NOTE: this is a very complicated action and you don't have to implement it but it's nice if you do :)
		virtual red::SharedPtr< IObjectPersistentCollection > Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) = 0;

		/// Can we ungroup given objects?
		/// If not, the string describing the reason will be returned
		virtual Bool CanUngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, String* outReason = nullptr ) const;

		/// Ungroup objects from given paths
		/// Always done from inside the action as this is wrapped in the undo/redo step
		/// Top level interface is written in the ObjectHandler
		virtual Bool Action_UngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths );

		/// Group objects from given paths
		/// Always done from inside the action as this is wrapped in the undo/redo step
		/// Top level interface is written in the ObjectHandler
		virtual Bool Action_GroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds );

	private:
		ObjectHandler*		m_handler;
	};

} // tools
