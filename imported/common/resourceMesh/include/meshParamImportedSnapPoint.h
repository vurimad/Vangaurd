/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace mesh
{
	class RESOURCEMESH_API ImportedSnapTags
	{
		RTTI_DECLARE_TYPE(ImportedSnapTags);

	public:
		red::DynArray<CName> m_includeTags{ red::PoolBackend() }; // tags of the snap point, we will snap to anything that also has a tag
		red::DynArray<CName> m_excludeTags{ red::PoolBackend() }; // tags we will definitely NOT glue with
	};

	class MeshImportedSnapPoint;
	typedef THandle< MeshImportedSnapPoint > MeshImportedSnapPointPtr;

	class RESOURCEMESH_API MeshImportedSnapPoint : public ISerializable
	{
		RTTI_DECLARE_TYPE(MeshImportedSnapPoint);
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MeshImportedSnapPoint();
		MeshImportedSnapPoint(const Matrix& transform, const Float range, const Uint8 rotationSteps, const ImportedSnapTags& tags = ImportedSnapTags());

		RED_FORCE_INLINE const Matrix GetTransform() const { return m_localToCloud; }
		RED_FORCE_INLINE const Float GetRange() const { return m_range; }
		RED_FORCE_INLINE const Uint8 GetRotationSteps() const { return m_rotationAlignmentSteps; }
		RED_FORCE_INLINE const ImportedSnapTags& GetSnapTags() const { return m_snapTags; }

	private:
		Matrix		m_localToCloud; // placement
		Float		m_range; // snap range
		Uint8		m_rotationAlignmentSteps;
		ImportedSnapTags m_snapTags;
	};

	class RESOURCEMESH_API MeshParamImportedSnapPoint : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamImportedSnapPoint );

	public:
		MeshParamImportedSnapPoint();

		virtual void	OnMeshChanged(const CMesh& mesh) override final;
		void			GetSnapPoints(red::DynArray<MeshImportedSnapPointPtr>& snapFeatures);
		void			SetSnapPoints(const red::DynArray<MeshImportedSnapPointPtr>& snapFeatures);
		const red::DynArray< MeshImportedSnapPointPtr >& GetSnapPoints();

	private:
		red::DynArray< MeshImportedSnapPointPtr > m_snapFeatureData{ red::PoolBackend() };
	};

} // mesh
