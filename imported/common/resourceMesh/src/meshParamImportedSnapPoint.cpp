/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../include/meshParamImportedSnapPoint.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(ImportedSnapTags, mesh);
	RTTI_PROPERTY(m_includeTags);
	RTTI_PROPERTY(m_excludeTags);
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamImportedSnapPoint, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY(m_snapFeatureData);
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshImportedSnapPoint, mesh);
	RTTI_PARENT_TYPE(ISerializable);
	RTTI_PROPERTY(m_localToCloud);
	RTTI_PROPERTY(m_range);
	RTTI_PROPERTY(m_rotationAlignmentSteps);
	RTTI_PROPERTY(m_snapTags);
RTTI_END_TYPE();


namespace mesh
{
	MeshParamImportedSnapPoint::MeshParamImportedSnapPoint()
	{}

	void MeshParamImportedSnapPoint::OnMeshChanged(const CMesh& mesh)
	{
	}	

	void MeshParamImportedSnapPoint::GetSnapPoints(red::DynArray<MeshImportedSnapPointPtr>& snapFeatures)
	{
		for (auto &el : m_snapFeatureData)
		{
			snapFeatures.PushBack(el);
		}
	}

	void MeshParamImportedSnapPoint::SetSnapPoints(const red::DynArray<MeshImportedSnapPointPtr>& snapFeatures)
	{
		m_snapFeatureData = snapFeatures;
	}

	const red::DynArray< mesh::MeshImportedSnapPointPtr >& MeshParamImportedSnapPoint::GetSnapPoints()
	{
		return m_snapFeatureData;
	}

	MeshImportedSnapPoint::MeshImportedSnapPoint()
		: m_localToCloud(Matrix::IDENTITY())
		, m_range(0.0f)
		, m_rotationAlignmentSteps(0)
	{
	}

	MeshImportedSnapPoint::MeshImportedSnapPoint(const Matrix& transform, const Float range, const Uint8 rotationSteps, const ImportedSnapTags& tags)
		: m_localToCloud(transform)
		, m_range(range)
		, m_rotationAlignmentSteps(rotationSteps)
		, m_snapTags(tags)
	{
	}

} // mesh
