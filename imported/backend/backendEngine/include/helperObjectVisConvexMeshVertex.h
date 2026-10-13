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


	class HelperObjectVisConvexMeshVertex : public HelperObjectSubobject
	{
		RTTI_DECLARE_TYPE( HelperObjectVisConvexMeshVertex );

	public:
		HelperObjectVisConvexMesh*	m_meshObj;
		Uint16						m_faceIndex;
		Uint16						m_vertIndex;
		Bool						m_isSelected;

		HelperObjectVisConvexMeshVertex( HelperObjectVisConvexMesh* meshObj, Uint16 faceIndex, Uint16 vertIndex );

		Vector3 GetPosition() const;
		void SetPosition( const Vector3& position );


		Uint16 GetNextEdgeIndex() const;
		Uint16 GetPrevEdgeIndex() const;

		void OnRender( const EditModeViewport& viewport, rend::FragmentBuilder& builder ) const;

		// HelperObjectSubobject interface
		virtual EditableProxyPtr Selectable_CreateProxy() const override;
		virtual tools::EditorObjectID Selectable_CreateEditorId() const override;

		// ISerializable interface
		virtual void GetCustomEditablePropeties( red::DynArray< rtti::ClassEditablePropertyInfo >& outRootProperties ) const override;
		virtual const Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;
		virtual const Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue ) override;
	};

	using HelperObjectVisConvexMeshVertexPtr = THandle< HelperObjectVisConvexMeshVertex >;

} // tools

#endif