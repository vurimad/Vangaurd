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


	class HelperObjectVisConvexMeshFace : public HelperObjectSubobject
	{
		RTTI_DECLARE_TYPE( HelperObjectVisConvexMeshFace );

	public:
		HelperObjectVisConvexMesh*		m_meshObj;
		Uint16							m_faceIndex;
		Bool							m_isSelected;

		HelperObjectVisConvexMeshFace( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex );

		void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const;

		// HelperObjectSubobject interface
		virtual EditableProxyPtr Selectable_CreateProxy() const override;
		virtual tools::EditorObjectID Selectable_CreateEditorId() const override;

		// ISerializable
		virtual void GetCustomEditablePropeties( red::DynArray< rtti::ClassEditablePropertyInfo >& outRootProperties ) const override;
		virtual const Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;
		virtual const Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) override;

		// Called by EditableProxy when editing begins on this face.
		void Begin();

		// Update edited transform. Pushes new vertex positions to the node.
		void SetEditedTransform( const Matrix& transform );
		// Get the current edited transform.
		const Matrix& GetEditedTransform() const { return m_editedTransform; }

		// Call when a vertex in the face is changed, so that the face can update some internally cached information.
		void RefreshCachedData();

	private:
		red::DynArray<Vector3>	m_originalVertices;
		Matrix					m_editedTransform;
	};

	using HelperObjectVisConvexMeshFacePtr = THandle< HelperObjectVisConvexMeshFace >;

} // tools

#endif