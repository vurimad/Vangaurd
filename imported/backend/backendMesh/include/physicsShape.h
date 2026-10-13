/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "physicsBase.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/physics/include/physicsMaterial.h"

namespace physics
{
	//////////////////////////////////////////////////////////////////////////	 

	class PhysicsActor;
	using PhysicsActorPtr = THandle< PhysicsActor >;

	class FilterData;

	//////////////////////////////////////////////////////////////////////////

	enum EPhysicsShapeGeometryType : Uint8
	{
		EPST_Box,
		EPST_Sphere,
		EPST_Capsule,
		EPST_Convex,
		EPST_TriMesh,
		EPST_BoxAligned,
		EPST_SphereAligned,
		EPST_CapsuleAligned,
		EPST_Invalid,
		EPST_Max
	};

	enum EPhysicsShapeFlag : Uint32
	{
		EPSF_RequestGeometryRebuild		= EPBF_Reserved_1, // flag for simulation to know that shapes geometry was modified
		EPSF_IsImported					= EPBF_Reserved_2,  
		EPSF_IsMaterialRenderingEnabled = EPBF_Reserved_3, 

	};

	class BACKEND_MESH_API ShapeGeometryBase : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( ShapeGeometryBase )

	public:
		EPhysicsShapeGeometryType			m_shapeType;
		Bool m_wasModified;

		ShapeGeometryBase()
			: m_shapeType( EPST_Invalid )
			, m_wasModified( false )
		{}

		ShapeGeometryBase( EPhysicsShapeGeometryType geomType )
			: m_shapeType( geomType )
		{}

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;
	};

	using ShapeGeometryPtr = THandle< ShapeGeometryBase >;

	class BACKEND_MESH_API ShapeGeometryBox : public ShapeGeometryBase
	{
		RTTI_DECLARE_TYPE( ShapeGeometryBox );

	public:
		Vector4								m_halfExtents;

		ShapeGeometryBox()
			: ShapeGeometryBase( EPST_Box )
		{}

		ShapeGeometryBox( Vector4 halfExtents )
			: ShapeGeometryBase( EPST_Box )
			, m_halfExtents( halfExtents )
		{};
	};

	class BACKEND_MESH_API ShapeGeometrySphere : public ShapeGeometryBase
	{
		RTTI_DECLARE_TYPE( ShapeGeometrySphere );

	public:
		Float								m_radius;

		ShapeGeometrySphere()
			: ShapeGeometryBase( EPST_Sphere )
		{}

		ShapeGeometrySphere( Float radius )
			: ShapeGeometryBase( EPST_Sphere )
			, m_radius( radius )
		{};
	};

	class BACKEND_MESH_API ShapeGeometryCapsule : public ShapeGeometryBase
	{
		RTTI_DECLARE_TYPE( ShapeGeometryCapsule );

	public:
		Float								m_height;
		Float								m_radius;

		ShapeGeometryCapsule()
			: ShapeGeometryBase( EPST_Capsule )
		{}

		ShapeGeometryCapsule( Float radius, Float height )
			: ShapeGeometryBase( EPST_Capsule )
			, m_radius( radius )
			, m_height( height )
		{};
	};

	struct BACKEND_MESH_API SConvexGeometryDescription
	{
		Uint32				m_vertexCount;

		SConvexGeometryDescription()
			: m_vertexCount( 0 )
		{};
	};

	class BACKEND_MESH_API ShapeGeometryConvex : public ShapeGeometryBase
	{
		RTTI_DECLARE_TYPE( ShapeGeometryConvex );

	public:
		red::DynArray< Vector3 >			m_vertexPositions{ red::PoolBackend() };
		red::DynArray< Uint32 >				m_indices{ red::PoolBackend() };
		Uint32								m_vertexCount = 0;

		ShapeGeometryConvex()
			: ShapeGeometryBase( EPST_Convex )
		{};
	};

	class BACKEND_MESH_API ShapeGeometryTriMesh : public ShapeGeometryBase
	{
		RTTI_DECLARE_TYPE( ShapeGeometryTriMesh );

	public:
		red::DynArray< Vector3 >		m_vertices;
		red::DynArray< CName >			m_physicalMaterials;
		red::DynArray< Uint16 >			m_indices;
		red::DynArray< Uint16 >			m_physicalMaterialIndices;
		Uint32							m_numTriangles;

		ShapeGeometryTriMesh()
			: ShapeGeometryBase( EPST_TriMesh )
		{}

		ShapeGeometryTriMesh( red::DynArray< Vector3 >&& sourceVertices, red::DynArray< Uint16 >&& sourceIndices, red::DynArray< Uint16 >&& physicalMaterialIndexes, red::DynArray< CName >&& physicalMaterials )
			: ShapeGeometryBase( EPST_TriMesh )
			, m_vertices( std::move( sourceVertices ) )
			, m_indices( std::move( sourceIndices ) )
			, m_physicalMaterialIndices( std::move( physicalMaterialIndexes ) )
			, m_physicalMaterials( std::move( physicalMaterials ) )
		{};

		void GetRenderingData( red::DynArray< DebugVertex >& vertices, red::DynArray< Uint16 >& indices );
	};

	struct BACKEND_MESH_API PhysicsShapeParams
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		Matrix								m_localTransform;
		Matrix								m_parentL2W;
		PhysicsBaseID						m_id;
		PhysicsBaseID						m_parentId;
		PhysicsEditorScene*					m_editorParent;
	};

	class BACKEND_MESH_API PhysicsShape : public PhysicsBase
	{
		RTTI_DECLARE_TYPE( PhysicsShape );
		DECLARE_INTEROP_DERIVED( PhysicsShape, PhysicsBase );

	public:
		PhysicsShape();
		PhysicsShape( PhysicsShapeParams params );
		PhysicsShape( const THandle<PhysicsShape>& copyFrom );

		virtual ~PhysicsShape();

		// Physics shape ID is made of parent actor's IDd and a (index + 1) in parent's shape array 
		// (stored in shape's m_physicsBaseId, shifted to guarantee uniqueness)
		virtual const PhysicsBaseID			GetPhysicsObjectID() const override
		{
			return ( m_physicsBaseId << 16 ) + m_parentId;
		}

		virtual void						SetLocalToWorld( const Matrix& localToWorld ) override;
		virtual Bool SetScale( const Vector3& newScale ) override final;
		virtual Vector3 GetScale() const override final;

		void								OnParentLocalToWorldChanged( Matrix parentL2W );
		void								UpdateLocalTransform( Matrix localTransform );
		RED_INLINE Matrix					GetLocalTransform()
		{
			return m_localTransform;
		}

		virtual void						OnUpdate() override;
		virtual void						OnRender( rend::IDebugDrawer& debugDrawer, PhysicsRendererOptionsPtr options ) override;
		virtual void OnPostLoad( const PostLoadContext& context ) override;
		virtual void						OnRestore( PhysicsEditorScene* scene ) override;

		virtual void						PrepareForRendering( PhysicsRendererOptionsPtr options ) override;

		// toggle the selection for this PhysicsDecorationBase
		virtual void						ToggleSelectionVisualization( const Bool drawAsSelected ) override;

		virtual red::SharedPtr< tools::IEditableProxy > CreateEditableProxy( Bool getParentProxy = true ) const override;

		PhysicsActorPtr						GetActorParent() const;

		void								SetParentId( PhysicsBaseID id );

		RED_INLINE PhysicsBaseID			GetParentId() const
		{
			return m_parentId;
		}

		void							SetShapeGeometry( ShapeGeometryPtr geo );

		RED_INLINE ShapeGeometryPtr		GetShapeGeometry() const
		{
			return m_geometry;
		}

		Bool HasFilterData() const;

		void SetIsImported( Bool isImported );
		Bool IsImported() const;

		RED_INLINE Bool IsQueryShapeOnly() const { return m_isQueryShapeOnly; }
		RED_INLINE void SetIsQueryShapeOnly( Bool enable ) { m_isQueryShapeOnly = enable; }

		// Object serialization interface
		virtual void						OnSerialize( class IFile& file ) override;
		virtual void						OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		static Bool 						ExtractParentID( const PhysicsBaseID shapeId, PhysicsBaseID& parentId );

		virtual String GetName() const override final;
	private:
		void								RestoreLocalToWorld();
		void								UpdateBounds();
		void								OnParentChanged();
		Color								GetColorFromParent();
	public:
		Float m_volumeModifier = 1.f;
		CName m_material;
		red::DynArray< ApperanceMaterial >  m_materialApperanceOverrides = { red::PoolBackend() };
		THandle<FilterData>					m_filterData;
		Bool								m_isObstacle;
	private:
		Matrix								m_localTransform;
		Matrix								m_parentL2W;
		Matrix								m_parentL2WInverted;

		Vector4								m_extractedScale;


		ShapeGeometryPtr					m_geometry;

		PhysicsBaseID						m_parentId;
		Bool								m_isQueryShapeOnly;
		friend class PhysicsActor;
	};

	using PhysicsShapePtr = THandle< PhysicsShape >;
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsShapeGeometryType, physics );

// !PhysicsShape

