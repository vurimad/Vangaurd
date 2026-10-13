#include "debugDataVisualization.h"

class CMesh;

class MeshDebugGeometry : public IDebugVisualizationGeometry
{
public:
	MeshDebugGeometry( const THandle< CMesh > mesh )
		: m_mesh( mesh )
	{}

	virtual Bool HasCollision() const override;
	virtual void GetCollisionShapes( red::DynArray< MeshDesc >& collisions ) const override;
	virtual const Box& GetBoundingBox() const override;
	virtual void GetNormals( LinesDesc& normalsDesc ) const override;
	virtual void GetTangents( LinesDesc& tangentsDesc ) const override;
	virtual void GetBitangents( LinesDesc& bitangentsDesc ) const override;
	virtual void GetBonesPositions( red::DynArray< Vector >& bonesPos ) const override;

private:
	THandle< CMesh > m_mesh;
};

class MeshVisualizationContentSupplier : public IDebugVisualizationContentSupplier
{
public:
	MeshVisualizationContentSupplier( const THandle< CMesh > mesh );
};