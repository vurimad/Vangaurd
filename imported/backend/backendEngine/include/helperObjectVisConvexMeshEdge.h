/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "helperObject.h"

// TEMPORARY
#if 0

namespace tools
{

	struct EditModeViewport;
	class HelperObjectVisConvexMesh;


	class HelperObjectVisConvexMeshEdge : public HelperObjectSubobject
	{
		RTTI_DECLARE_TYPE( HelperObjectVisConvexMeshEdge );

	public:
		HelperObjectVisConvexMesh*		m_meshObj;
		Uint16							m_faceIndex;
		Uint16							m_edgeIndex;
		Bool							m_isSelected;

		HelperObjectVisConvexMeshEdge( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 edgeIndex );

		void Move( const Vector3& offset );
		void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const;

		Uint16 GetStartVertexIndex() const;
		Uint16 GetEndVertexIndex() const;

		// get position of one end of the edge
		Vector3 GetStartPosition() const;
		Vector3 GetEndPosition() const;

		// HelperObjectSubobject interface
		virtual EditableProxyPtr Selectable_CreateProxy() const override;
		virtual tools::EditorObjectID Selectable_CreateEditorId() const override;
	};

	using HelperObjectVisConvexMeshEdgePtr = THandle< HelperObjectVisConvexMeshEdge >;

} // tools

#endif