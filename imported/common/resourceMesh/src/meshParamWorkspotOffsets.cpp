/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshParamWorkspotOffsets.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamWorkspotOffsets, mesh);
RTTI_PARENT_TYPE(mesh::MeshParameter);
RTTI_PROPERTY(m_names).readOnly();
RTTI_PROPERTY(m_offsets).readOnly();
RTTI_END_TYPE();


namespace mesh
{
	const Matrix MeshParamWorkspotOffsets::m_unknownOffset = Matrix::IDENTITY();

	const Matrix& MeshParamWorkspotOffsets::GetOffset(const CName& name) const
	{
		for (Uint32 index = 0; index < m_names.Size(); index++)
		{
			if (m_names[index] == name)
			{
				return m_offsets[index];
			}
		}
		return m_unknownOffset;
	}

	void MeshParamWorkspotOffsets::SetWorkspotOffsets(const red::DynArray<CName>& names, const red::DynArray<Matrix>& offsets)
	{
		RED_ASSERT(names.Size() == offsets.Size(), "You need to provide equal number of names and offsets.");

		m_names = names;
		m_offsets = offsets;
	}

	void MeshParamWorkspotOffsets::OnMeshChanged(const CMesh& mesh)
	{
	}
} // mesh
