/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "previewInputListener.h"
#include "helperObject.h"
#include "helperObjectVisConvexMeshVertex.h"
#include "helperObjectVisConvexMeshEdge.h"
#include "helperObjectVisConvexMeshFace.h"
#include "../../../common/worldStatic/include/staticOccluderConvexMeshNode.h"

// TEMPORARY
#if 0

namespace tools
{
	class IEditActionHistory;
	class SelectionHandler;


	class HelperObjectVisConvexMesh
		: public IHelperObject
		, public world::StaticOccluderConvexMeshNode::IShapeListener
	{
	public:
		typedef red::DynArray<Vector3> TConvexFace;

		typedef red::DynArray< HelperObjectVisConvexMeshVertexPtr > TVertexArray;
		typedef red::DynArray< HelperObjectVisConvexMeshEdgePtr > TEdgeArray;
		typedef red::DynArray< HelperObjectVisConvexMeshFacePtr > TFaceArray;

		struct AddVertexPreview
		{
			Bool m_enable;
			Uint32 m_faceIndex;			// The face we're going to add a vertex to
			Uint16 m_index;				// Where in the face to insert the new vertex
			Float m_position;			// Position along the edge, in range 0-1
			AddVertexPreview()
				: m_enable( false )
				, m_index( 0 )
				, m_position( 0.0f )
			{}
		};

		struct AddFacePreview
		{
			Bool m_enable;
			Vector3 m_center;			// Center of the new face to add.
			Vector3 m_normal;			// Normal for the new face's plane. Based on surface normal under mouse.
			AddFacePreview()
				: m_enable( false )
				, m_center( Vector3::ZEROS )
				, m_normal( 0.0f, 0.0f, 1.0f )
			{}
		};


	public:
		HelperObjectVisConvexMesh( const world::HelperObjectDesc& desc, EditableProxyPtr editableProxy, IEditActionHistory* actionHistory );
		~HelperObjectVisConvexMesh();

		RED_INLINE world::StaticOccluderConvexMeshNode* GetOccluderNode() const { return Cast< world::StaticOccluderConvexMeshNode >( m_desc.m_source.Get() ); }

		HelperObjectVisConvexMeshVertexPtr GetVertexSubobject( Uint16 faceIdx, Uint16 vertIdx ) const;
		HelperObjectVisConvexMeshEdgePtr GetEdgeSubobject( Uint16 faceIdx, Uint16 edgeIdx ) const;
		HelperObjectVisConvexMeshFacePtr GetFaceSubobject( Uint16 faceIdx ) const;


		// world::StaticOccluderConvexMeshNode::IShapeListener interface
		virtual void ConvexMeshNode_OnVertexAdded( Uint16 faceIdx, Uint16 vertIdx ) override;
		virtual void ConvexMeshNode_OnVertexChanged( Uint16 faceIdx, Uint16 vertIdx ) override;
		virtual void ConvexMeshNode_OnVertexRemoved( Uint16 faceIdx, Uint16 vertIdx ) override;
		virtual void ConvexMeshNode_OnFaceAdded( Uint16 faceIdx ) override;
		virtual void ConvexMeshNode_OnFaceRemoved( Uint16 faceIdx ) override;
		virtual void ConvexMeshNode_OnShapeChanged() override;

		// IHelperObject interface
		virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt ) override;
		virtual Bool OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt, SelectionHandler* selectionHandler ) override;
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& debugDrawer ) const override;
		virtual Bool DeleteSelected( SelectionHandler* selectionHandler ) override;


		// Get a color for drawing a subobject. Color is chosen based on whether the subobject itself is selected, as
		// well as whether some other related subobject is selected. For example, for a vertex, the related objects
		// might be the adjacent edges, and the face.
		static Color GetSubobjectColor( Bool isSelected, Bool isRelatedSelected );


	private:
		Bool GetSelectedVertices( red::DynArray< HelperObjectVisConvexMeshVertex* >& outSelectedVertices ) const;
		Bool GetSelectedEdges( red::DynArray< HelperObjectVisConvexMeshEdge* >& outSelectedEdges ) const;
		Bool GetSelectedFaces( red::DynArray< HelperObjectVisConvexMeshFace* >& outSelectedFaces ) const;

		// Refresh all vertex, edge, face helper subobjects.
		void RefreshSubobjects();

		// Refresh all vertex, edge helper subobjects for the given face.
		void RefreshEdgeVertSubobjects( Uint16 faceIdx );


	private:
		red::DynArray<TConvexFace>	m_originalFaces;			// Faces before any editing happened

		red::DynArray<TVertexArray>	m_vertices;
		red::DynArray<TEdgeArray>	m_edges;
		TFaceArray					m_faces;

		AddVertexPreview			m_addVertexPreview;
		AddFacePreview				m_addFacePreview;

		IEditActionHistory*			m_actionHistory;
	};

} // tools

#endif