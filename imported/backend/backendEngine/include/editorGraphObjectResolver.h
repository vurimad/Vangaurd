/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editorGraphTypes.h"
#include "objectResolver.h"
#include "objectPersistentCollection.h"

class IBackendData;

namespace tools
{
	class IEditorGraphEx;

	struct BACKEND_ENGINE_API GraphObjectPlacementInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		GraphObjectPlacementInfo( const IObjectPersistentCollection::ObjectInfo& info, red::Map< EditorObjectID, THandle< IBackendData > >& outObjects )
			: m_info( info )
			, m_outObjects( outObjects )
		{
		}

		const IObjectPersistentCollection::ObjectInfo& m_info;
		red::Map< EditorObjectID, THandle< IBackendData > >& m_outObjects;
		Point m_placementOffset;
	};

	class BACKEND_ENGINE_API ConnectionDataHolder : public GraphBackendData
	{
		RTTI_DECLARE_TYPE( ConnectionDataHolder )

	public:
		ConnectionDataHolder() = default;
		ConnectionDataHolder( const THandle< ConnectionDescription >& desc, const THandle< ISerializable >& data )
			: m_desc( desc )
			, m_data( data )
		{
		}

		THandle< ConnectionDescription > m_desc;
		THandle< ISerializable > m_data;

		virtual tools::EditorObjectID GetEditorObjectID() const;

		static CName sm_type;
	};

	class BACKEND_ENGINE_API IGraphObjectConstructor
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( IGraphObjectConstructor )

	public:
		virtual ~IGraphObjectConstructor() {}

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const = 0;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, THandle< IBackendData >& outObject ) const = 0;

		virtual const Bool CanDeleteObject( const EditorObjectID& id, Bool isCut ) const = 0;
		virtual const Bool DeleteObject( const EditorObjectID& id, Bool isCut ) const = 0;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;
		virtual const Bool RestoreObject( const GraphObjectPlacementInfo& info ) const = 0;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;
		virtual const Bool PlaceObject( const GraphObjectPlacementInfo& info, Bool isInPlace ) const = 0;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const = 0;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectID& id, const IObjectResolver::ResolvingParams& params = IObjectResolver::ResolvingParams() ) const = 0;

		virtual const Bool CanResolveProxy( const THandle< ISerializable >& object ) const = 0;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const THandle< ISerializable >& object ) const = 0;

		virtual THandle< ISerializable > GetNodeDesc( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const = 0;
	};

	class BACKEND_ENGINE_API BaseGraphObjectConstructor : public IGraphObjectConstructor
	{
		RTTI_DECLARE_TYPE( BaseGraphObjectConstructor )

	public:
		BaseGraphObjectConstructor() = default;
		BaseGraphObjectConstructor( IEditorGraphEx* editor );

		virtual THandle< ISerializable > ResolveObject( const EditorObjectID& id, const IObjectResolver::ResolvingParams& params = IObjectResolver::ResolvingParams() ) const override;

		virtual const Bool CanResolveProxy( const THandle< ISerializable >& object ) const override;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const THandle< ISerializable >& object ) const override;

		virtual THandle< ISerializable > GetNodeDesc( const IObjectPersistentCollection::ObjectInfo& info ) const override { return nullptr; }

	protected:
		IEditorGraphEx* m_graphDescriptorEditor;
	};

	class BACKEND_ENGINE_API GraphConnectionObjectConstructor : public BaseGraphObjectConstructor
	{
		RTTI_DECLARE_TYPE( GraphConnectionObjectConstructor )

	public:
		GraphConnectionObjectConstructor() = default;
		GraphConnectionObjectConstructor( IEditorGraphEx* editor );

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, THandle< IBackendData >& outObject ) const override;

		virtual const Bool CanDeleteObject( const EditorObjectID& id, Bool isCut ) const override;
		virtual const Bool DeleteObject( const EditorObjectID& id, Bool isCut ) const override;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool RestoreObject( const GraphObjectPlacementInfo& info ) const override;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool PlaceObject( const GraphObjectPlacementInfo& info, Bool isInPlace ) const override;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const override;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectID& id, const IObjectResolver::ResolvingParams& params = IObjectResolver::ResolvingParams() ) const override;
	};

	class BACKEND_ENGINE_API GraphNodeObjectConstructor : public BaseGraphObjectConstructor
	{
		RTTI_DECLARE_TYPE( GraphNodeObjectConstructor )

	public:
		GraphNodeObjectConstructor() = default;
		GraphNodeObjectConstructor( IEditorGraphEx* editor );

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, THandle< IBackendData >& outObject ) const override;

		virtual const Bool CanDeleteObject( const EditorObjectID& id, Bool isCut ) const override;
		virtual const Bool DeleteObject( const EditorObjectID& id, Bool isCut ) const override;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool RestoreObject( const GraphObjectPlacementInfo& info ) const override;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool PlaceObject( const GraphObjectPlacementInfo& info, Bool isInPlace ) const override;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const override;

		virtual THandle< ISerializable > GetNodeDesc( const IObjectPersistentCollection::ObjectInfo& object ) const override;

		static CName sm_type;
	};

	class BACKEND_ENGINE_API EditorGraphObjectResolver : public IObjectResolver
	{
		RTTI_DECLARE_TYPE( EditorGraphObjectResolver );

	public:
		EditorGraphObjectResolver( IEditorGraphEx* graphDescriptorEditor );

		/// IObjectResolver interface
		virtual Bool IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const override final;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override final;
		virtual Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override final;
		virtual Bool CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason = nullptr ) const override;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual red::SharedPtr< IObjectPreview > CreateObjectPreview( const ObjectCreationInfo& initInfo ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) override;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out ) override final;
		virtual red::UniquePtr< IEditAction > CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const override;
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) override final;
		virtual Bool Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual Bool Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;

	private:
		typedef red::DynArray< IGraphObjectConstructor* > TGraphObjectConstructors;

		void SupplementAdditionalObjectIDPaths( red::DynArray< EditorObjectIDPath >& outPaths ) const;
		void RemoveInvalidObjects( red::DynArray< IObjectPersistentCollection::ObjectInfo >& objects );
		Bool DeleteObjectsOfKind( const red::ArraySpan< const EditorObjectIDPath >& paths, const CName kind );

		IEditorGraphEx* m_graphDescriptorEditor;
		
		TGraphObjectConstructors m_constructors;
	};
}