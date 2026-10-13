/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "previewEditModeAdapter.h"

class GeometryShape;

namespace tools
{
	struct GeometryData
	{
	public:
		struct Edge
		{
			Uint32 m_vertexStart;
			Uint32 m_vertexEnd;

			Bool operator==( const Edge& other ) const;
		};

		struct Face
		{
			red::DynArray< Uint32 > m_vertices;
		};

		red::DynArray< Vector3 > m_vertices{ red::PoolTriggers() };

		// Array of subsequent vertices, creating a closed loop
		// When triangulating such shape results might be ambiguous,
		// do whatever your algorithm want to do with it.
		// User can create more edges to better control the result
		red::DynArray< Face > m_faces{ red::PoolTriggers() }; // Contains indices of vertices and not edges!

		red::DynArray< Edge > m_edges{ red::PoolTriggers() };
	};

	///
	///
	///

	struct GeoVertex : public ISerializable
	{
		RTTI_DECLARE_TYPE( GeoVertex )
	public:
		EditorObjectID m_id;
		Vector3 m_position;
	};

	///
	///
	///

	struct GeoEdge : public ISerializable
	{
		RTTI_DECLARE_TYPE( GeoEdge )
	public:
		EditorObjectID m_id;
		EditorObjectID m_vertexStart;
		EditorObjectID m_vertexEnd;

		Vector3 m_cachedPosition;
	};

	///
	///
	///

	struct GeoFace: public ISerializable
	{
		RTTI_DECLARE_TYPE( GeoFace )

	public:
		EditorObjectID m_id;
		red::DynArray< EditorObjectID > m_vertices;

		Vector3 m_cachedPosition;
	};

	///
	///
	///

	enum class GeoSelectionMode
	{
		All,
		Vertices,
		Edges,
		Faces
	};

	// Real edit mode
	class BACKEND_ENGINE_API GeometryShapeEditMode : public PreviewEditModeAdapter
	{
		friend class GeometryVertexObject;
		friend class GeometryEdgeObject;
		friend class GeometryFaceObject;

	public:
		GeometryShapeEditMode( const tools::EditableProxyPtr& proxy );
		virtual ~GeometryShapeEditMode() override;

		// PreviewEditModeAdapter
		virtual void CaptureDeleteData( const EditorObjectID& id, red::DynArray< PreviewObjectHolderPtr >& deleteData ) const override;
		virtual void RestoreObjects( const red::DynArray< PreviewObjectHolderPtr >& objects ) override;
		virtual void RemoveObjects( const red::DynArray< EditorObjectID >& objects ) override;

		// IHelperObject
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const override;
		virtual Bool CanBeResolved( const EditorObjectID& id ) const override;
		virtual EditorObjectID GetInsertLocation() const override;

		// IObjectResolver
		virtual Bool CanDeleteObjects( const red::ArraySpan<const EditorObjectIDPath>& paths, Bool isCut, String* outFailReason ) const override;
		virtual void RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel ) override;
		virtual void UnregisterShortcuts( ShortcutManager* manager ) override;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override;

		///
		/// Vertices
		///

		const GeoVertex& GetVertex( const EditorObjectID& vertexId ) const;
		const Vector3& GetVertexPosition( const EditorObjectID& vertexId ) const;
		void SetVertexPosition( const EditorObjectID& vertexId, const Vector3& postion );
		void GetVertexEdgesAndFaces( const EditorObjectID& id, red::DynArray< EditorObjectID >& edges, red::DynArray< EditorObjectID >& faces ) const;
		Bool ConnectVerticesByEdge( const EditorObjectID& newStartVertexId, const EditorObjectID& newEndVertexId, EditorObjectID& newEdgeId );

		///
		/// Edges
		///

		const GeoEdge& GetEdge( const EditorObjectID& edgeId ) const;
		const Vector3& GetEdgePosition( const EditorObjectID& edgeId ) const;
		void GetEdgeData( const EditorObjectID& edgeId, Vector3& direction, Float& length, Vector3& startPoint, Vector3& endPoint ) const;
		void SetEdgePosition( const EditorObjectID& edgeId, const Vector3& position );
		void GetEdgeVertices( const EditorObjectID& edgeId, GeoVertex& start, GeoVertex& end ) const;
		void SetEdgeVertices( const EditorObjectID& edgeId, const EditorObjectID& newStartVertexId, const EditorObjectID& newEndVertexId );
		void EdgeSubdivide( const EditorObjectID& edgeId, Float cutPosition, EditorObjectID& newVertexId, EditorObjectID& newEdgeId );
		EditorObjectID FindEdge( const EditorObjectID& vertexStart, const EditorObjectID& vertexEnd );

		///
		/// Faces
		///

		const GeoFace& GetFace( const EditorObjectID& faceId ) const;
		const Vector3& GetFacePosition( const EditorObjectID& id ) const;
		const red::DynArray< EditorObjectID >& GetFaceVertices( EditorObjectID faceId ) const;
		Vector3 GetFaceNormal( const EditorObjectID& id ) const;
		void GetFaceOrderedVertices( EditorObjectID faceId, red::DynArray< GeoVertex >& vertices ) const;
		void GetFaceVectors( const EditorObjectID& id, Vector3& forward, Vector3& right, Vector3& normal ) const;
		void SetFacePosition( const EditorObjectID& id, const Vector3& position );
		void SetFaceVertices( const EditorObjectID& faceId, const red::DynArray< EditorObjectID >& vertexIds );

		ObjectPersistentCollectionPtr CaptureAll();
		Bool RestoreAll( const ObjectPersistentCollectionPtr& capturedData );

		void MarkModified() { m_hasPendingChanges = true; }
		void ProcessChanges();

	protected:
		void UpdateEdgePostion( const EditorObjectID& edgeId );
		void UpdateFacePosition( const EditorObjectID& faceId );

		EditorObjectID FindVerticesFace( const EditorObjectID& vertex, const EditorObjectID& other, Bool& outIsOneEdge ) const;

		red::DynArray< EditorObjectID > FindFacesSharingEdge( const EditorObjectID& edgeId );		
		Bool MergeFaces( const EditorObjectID& faceOne, const EditorObjectID& faceTwo, red::Set< EditorObjectID >& outRemovedObjects );

		void SubdivideSelectedEdges();
		Bool SubdivideSelectedEdgesAndConnectVertices();

		Bool ConnectSelectedVerticesByEdge();
		void DivideFace( EditorObjectID face, EditorObjectID newEdge );

		Bool ExtrudeFace();
		EditorObjectID ExtrudeFace( const EditorObjectID& id );

		Bool LoopCut();
		Bool DissolveSelection();

		GeoSelectionMode GetSelectionMode() const;
		void SetSelectionMode( GeoSelectionMode mode );
		Bool IsSelectionModeEnabled( GeoSelectionMode mode ) const;

	private:
		static const CName s_vertexTypeName;
		static const CName s_edgeTypeName;
		static const CName s_faceTypeName;

		void Intialize( const GeometryData& data );

		void Intialize( const THandle< GeometryShape >& shape );
		void UpdateGeometryShape();
		void CreateVerticesObjects( const GeometryData& data );
		void CreateEdgesObjects( const GeometryData& data );
		void CreateFacesObjects( const GeometryData& data );

		EditorObjectID CreateEdge( const EditorObjectID& vertex1, const EditorObjectID& vertex2 );

		EditorObjectID CreateVertex();
		void CreateVertex( EditorObjectID id );

		EditorObjectID CreateEdge();
		void CreateEdge( EditorObjectID id );

		EditorObjectID CreateFace();
		void CreateFace( EditorObjectID id );

		void RemoveVertex( const EditorObjectID& id );
		void RemoveEdge( const EditorObjectID& id );
		void RemoveFace( const EditorObjectID& id );

		void RemovePreviewObject( const EditorObjectID& id );

		void DissolveVertex( const EditorObjectID& id, red::Set<EditorObjectID>& outRemovedObjects );
		void DissolveEdge( const EditorObjectID& id, red::Set<EditorObjectID>& outRemovedObjects );

		EditorObjectID CreateVertexId( Uint64 id = -1 );
		EditorObjectID CreateEdgeId( Uint64 id = -1 );
		EditorObjectID CreateFaceId( Uint64 id = -1 );

		EditorObjectID DuplicateVertex( const EditorObjectID& id );
		void DuplicateEdge( const EditorObjectID& id, EditorObjectID& newEdge, EditorObjectID& newVertex1, EditorObjectID& newVertex2 );

		void InvalidateCachedPosition( const EditorObjectID& id  );
		void RenderInfo( rend::FragmentBuilder& builder ) const;

		red::HashMap< EditorObjectID, GeoVertex > m_vertices;
		red::HashMap< EditorObjectID, GeoEdge > m_edges;
		red::HashMap< EditorObjectID, GeoFace > m_faces;

		Uint64 m_vertexGeneratorId{ 0 };
		Uint64 m_edgeGeneratorId{ 0 };
		Uint64 m_faceGeneratorId{ 0 };

		GeoSelectionMode m_selectionMode{ GeoSelectionMode::All };

		Bool m_hasPendingChanges = false;
	};

	///
	///
	///

	class GeometryVertexObject : public PreviewObject
	{
	public:
		GeometryVertexObject();
		GeometryVertexObject( PreviewEditModeAdapter* mode, EditorObjectID editorId );
		virtual ~GeometryVertexObject() override;

		virtual void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const;

		virtual Vector3 GetPosition() const override;
		virtual Bool Move( const Vector3& postion ) override;

	private:
		GeometryShapeEditMode* GetMesh() const;
	};

	///
	///
	///

	class GeometryEdgeObject : public PreviewObject
	{
	public:
		GeometryEdgeObject();
		GeometryEdgeObject( PreviewEditModeAdapter* mode, EditorObjectID editorId );
		virtual ~GeometryEdgeObject() override;

		virtual void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool overlay ) const;

		virtual Vector3 GetPosition() const override;
		virtual Bool Move( const Vector3& postion ) override;

	private:
		GeometryShapeEditMode* GetMesh() const;
	};

	///
	///
	///

	class GeometryFaceObject : public PreviewObject
	{
	public:
		GeometryFaceObject();
		GeometryFaceObject( PreviewEditModeAdapter* mode, EditorObjectID editorId );
		virtual ~GeometryFaceObject() override;

		virtual void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder, Bool front ) const;

		virtual Vector3 GetPosition() const override;
		virtual Bool Move( const Vector3& postion ) override;

		void Invalidate();

	private:
		Bool TriangulateVertices( const red::DynArray< Vector3 >& vertices, red::DynArray< Uint16 >& outTriangles ) const;

		GeometryShapeEditMode* GetMesh() const;

		red::DynArray< Vector3 > m_vertices;
		red::DynArray< Uint16 > m_indices;
	};
}

