#include "build.h"

#include "editableCollectionCustomPropertyEditor.h"
#include "objImporter.h"

#include "../../common/interop/include/interopUtilsManual.h"
#include "../../common/triggers/include/geometryShape.h"
#include "../../common/redReflection/include/serializableMap.h"
#include "../../common/resourceMesh/include/mesh.h"
#include "../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../common/resourceMesh/include/meshUtils.h"
#include "../../common/redFileSystem/include/fileSys.h"
#include "../../common/physics/include/pointCache.h"

namespace tools
{
	const CName c_geometryShapeControlType = RED_NAME_CONSTEXPR( "geometryShape" );

	class GeometryShape_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( GeometryShape_CustomProperty );
		RED_BASE_CLASS( IEditableCustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			return customTypeName == c_geometryShapeControlType;
		}

		virtual CName GetControlType() const override
		{
			return c_geometryShapeControlType;
		}

		virtual Bool HasUnlistedChildren() const override
		{
			return true;
		}

		void SetShape( interop::IContext& ctx, const red::DynArray< Vector3 >& vertices, const red::DynArray< GeometryShapeFace >& faces ) const
		{
			constexpr Uint32 c_maxIndices = 10000;
			constexpr Uint32 c_maxVertices = 3000;

			GeometryShape shape;
			shape.Editor_SetVertices( vertices );
			shape.Editor_SetFaces( faces );
			shape.Editor_BuildIndices();

			if ( shape.GetIndices().Size() > c_maxIndices )
			{
				ctx.ThrowException( "Number of indices is bigger than the supported amount (%d > %d)", shape.GetIndices().Size(), c_maxIndices );
				return;
			}

			if ( shape.GetVertices().Size() > c_maxVertices )
			{
				ctx.ThrowException( "Number of vertices is bigger than the supported amount (%d > %d)", shape.GetVertices().Size(), c_maxVertices );
				return;
			}

			rtti::ValuePtr newValue;
			shape.GetClass()->ReadValue( IRTTIContext::GetDefault(), &shape, rtti::AccessPath(), newValue );
			newValue->SetStructElement( RED_NAME_CONSTEXPR( "class" ), rtti::ValueHolder::CreateSingle( "GeometryShape" ) );

			SetValue( IRTTIContext::GetDefault(), GetPathSoFar(), newValue );
		}

		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override
		{
			if ( name == "importOBJ" )
			{
				using FunctorType = interop::utils::Functor1< void, String >;
				return interop::ObjectPtr( RED_NEW( FunctorType )( [ this ]( interop::IContext& ctx, const String& pathStr )
				{
					if ( !red::AbsolutePath::IsValidPath( pathStr ) )
					{
						ctx.ThrowException( "Invalid path." );
						return;
					}

					const red::AbsolutePath path = red::AbsolutePath::CreateFilePath( pathStr );

					ObjImporter objImporter;
					if ( !objImporter.LoadFile( path ) )
					{
						ctx.ThrowException( "Failed to load the file." );
						return;
					}

					red::DynArray< Vector3 > vertices;
					red::DynArray< GeometryShapeFace > faces;

					for ( const ObjImporter::Mesh& mesh : objImporter.m_meshes )
					{
						vertices.PushBack( mesh.m_vertices );
						faces.PushBack( mesh.m_faces );
					}

					vertices.PushBack( objImporter.m_vertices );
					faces.PushBack( objImporter.m_faces );

					SetShape( ctx, vertices, faces );
				} ) );
			}
			else if ( name == "importMesh" )
			{
				using FunctorType = interop::utils::Functor1< void, String >;
				return interop::ObjectPtr( RED_NEW( FunctorType )( [ this ]( interop::IContext& ctx, const String& pathStr )
				{
					const res::ResourcePath path = res::ResourcePath::Build( pathStr );
					if ( !path.IsValid() )
					{
						ctx.ThrowException( "Invalid path." );
						return;
					}

					THandle< CMesh > mesh = GResourceLoader->IssueLoadingRequest( path )->WaitUntilLoaded< CMesh >();
					if ( !mesh )
					{
						ctx.ThrowException( "Failed to load the mesh file." );
						return;
					}

					const mesh::DataViewRO sourceView( *mesh );
					red::DynArray< Vector3 > vertices;
					red::DynArray< Uint32 > indices;
					red::DynArray< GeometryShapeFace > faces;

					mesh::utils::ExtractGeometry( sourceView, vertices, &indices );
					red::PointCache cache( red::PoolBackend(), red::RoundUpToPowerOf2( vertices.Size() ) );

					for ( Uint32 index = 0, size = indices.Size(); index < size; ++index )
					{
						indices[ index ] = cache.MapPoint( vertices[ indices[ index ] ] );
					}

					red::DynArray< Vector3 > mappedVertices;
					mappedVertices.Reserve( cache.GetNumPoints() );
					for ( Uint32 index = 0, size = cache.GetNumPoints(); index < size; ++index )
					{
						mappedVertices.PushBack( cache.GetPointPosition( index ) );
					}

					for ( Uint32 index = 0, size = indices.Size(); index < size; index += 3 )
					{
						GeometryShapeFace& face = faces.EmplaceBack();
						face.m_indices.PushBack( indices[ index + 0 ] );
						face.m_indices.PushBack( indices[ index + 1 ] );
						face.m_indices.PushBack( indices[ index + 2 ] );
					}

					SetShape( ctx, mappedVertices, faces );
				} ) );
			}
			else if ( name == "verticesCount" )
			{
				rtti::ValuePtr value;
				if ( GetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) && value && value->IsHandle() )
				{
					SerializableID id( FromStringDirect<Uint64>( value->GetHandleValue() ) );
					THandle< ISerializable > serializable;
					GSerializableMap->FindSerializable( id, serializable );

					if ( THandle< GeometryShape > shape = Cast< GeometryShape >( serializable ) )
					{
						Uint32 verticesCount = shape->GetVertices().Size();
						return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Uint32> )( verticesCount ) );
					}
				}
			}
			else if ( name == "trianglesCount" )
			{
				rtti::ValuePtr value;
				if ( GetValue( IRTTIContext::GetDefault(), GetPathSoFar(), value ) && value && value->IsHandle() )
				{
					SerializableID id( FromStringDirect<Uint64>( value->GetHandleValue() ) );
					THandle< ISerializable > serializable;
					GSerializableMap->FindSerializable( id, serializable );

					if ( THandle< GeometryShape > shape = Cast< GeometryShape >( serializable ) )
					{
						Uint32 trianglesCount = shape->GetIndices().Size() / 3;
						return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Uint32> )( trianglesCount ) );
					}
				}
			}

			return TBaseClass::GetUnlistedChild( ctx, name );
		}
	};
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( GeometryShape_CustomProperty, tools );
	RTTI_PARENT_TYPE( GeometryShape_CustomProperty::TBaseClass );
RTTI_END_TYPE();