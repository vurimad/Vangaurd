#include "build.h"

#include "../../backendEngine/include/batchTool.h"
#include "../../backendMesh/include/meshDataView.h"

#include "../../../common/resourceMesh/include/mesh.h"

namespace tools
{
	struct LODSetup
	{
		RTTI_DECLARE_TYPE( LODSetup );

		mesh::ChunkFlags m_flags;
		Float m_distance;
	};

	class MeshCustomPropertiesDescriptor_LODSetup : public IResourceCustomPropertiesDescriptor
	{
		RTTI_DECLARE_TYPE( MeshCustomPropertiesDescriptor_LODSetup );

	public:
		virtual void Initialize( const red::DynArray< THandle< CResource > >& resources ) override;
		virtual void ApplyChanges( const job::RunContext& runContext ) const override;

		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
		virtual const String& GetTitle() const override;

	private:
		void ApplyChangesToMesh( const THandle< CMesh >& mesh ) const;
		void UpdateFlags( mesh::DataView& meshView, Uint32 lodIndex, const String& flagName ) const;
		void UpdateDistances( mesh::DataView& meshView, const red::DynArray< Bool >& updateSetup ) const;
		void UpdateDistance( mesh::DataView& meshView, const red::DynArray< Bool >& updateSetup, Uint32 lodIndex ) const;

		red::DynArray< LODSetup > m_LODSetup;
	};
	

	void MeshCustomPropertiesDescriptor_LODSetup::Initialize( const red::DynArray<THandle<CResource>>& resources )
	{
		IResourceCustomPropertiesDescriptor::Initialize( resources );

		Uint32 maxLodsCount = 0;
		for ( const THandle<CResource>& resource : resources )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				mesh::DataViewRO meshView( *mesh.Get() );
				if ( meshView.GetNumLODs() > maxLodsCount )
				{
					maxLodsCount = meshView.GetNumLODs();
				}
			}
		}

		if ( maxLodsCount != m_LODSetup.Size() )
		{
			m_LODSetup.Resize( maxLodsCount );
		}
	}

	void MeshCustomPropertiesDescriptor_LODSetup::ApplyChanges( const job::RunContext& runContext ) const
	{
		if ( m_switches.Empty() )
		{
			return;
		}

		for ( const THandle<CResource>& resource : m_resources )
		{
			if ( THandle< CMesh > mesh = Cast< CMesh >( resource ) )
			{
				ApplyChangesToMesh( mesh );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_LODSetup::ApplyChangesToMesh( const THandle<CMesh>& mesh ) const
	{
		mesh::DataView meshView( *mesh.Get() );

		red::DynArray< Bool > distanceUpdate;
		distanceUpdate.Resize( meshView.GetNumLODs() );

		for ( Uint32 i = 0; i < distanceUpdate.Size(); ++i )
		{
			distanceUpdate[i] = false;
		}

		for ( auto it = m_switches.Begin(); it != m_switches.End(); ++it )
		{
			if ( !it.Value() )
			{
				continue;
			}

			String propPath( it.Key().AsChar() );
			if ( propPath.BeginsWith( "LODSetup" ) )
			{
				propPath = propPath.StringAfter( "LODSetup" );
				propPath.TrimLeft( '[' );

				Uint32 lodInd = FromStringDirect<Uint32>( propPath.StringBefore( "]" ) );
				propPath = propPath.StringAfter( "]." );

				if ( lodInd >= m_LODSetup.Size() || lodInd >= meshView.GetNumLODs() )
				{
					continue;
				}

				if ( propPath.EqualsNC( "distance" ) )
				{
					if ( m_LODSetup[lodInd].m_distance < 0.f )
					{
						continue;
					}
					distanceUpdate[lodInd] = true;
				}
				else if ( propPath.BeginsWith( "flags" ) )
				{
					propPath = propPath.StringAfter( "flags" );
					propPath.TrimLeft( '.' );
					UpdateFlags( meshView, lodInd, propPath );
				}
			}
		}

		UpdateDistances( meshView, distanceUpdate );
	}

	void MeshCustomPropertiesDescriptor_LODSetup::UpdateFlags( mesh::DataView& meshView, Uint32 lodIndex, const String& flagName ) const
	{
		const mesh::ChunkFlags& sourceFlags = m_LODSetup[lodIndex].m_flags;
		for ( Uint16 chunkInd : meshView.GetLODChunkIndices( lodIndex ) )
		{
			mesh::ChunkFlags flags = meshView.GetChunkFlags( chunkInd );

			if ( const rtti::Property* prop = flags.GetClass()->FindProperty( RED_NAME_NOREG( flagName ) ) )
			{
				prop->GetType()->Copy( prop->GetOffsetPtr( &flags ), prop->GetOffsetPtr( &sourceFlags ) );
				meshView.SetChunkFlags( chunkInd, flags );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_LODSetup::UpdateDistances( mesh::DataView& meshView, const red::DynArray<Bool>& inputUpdateSetup ) const
	{
		red::DynArray< Bool > updateSetup = inputUpdateSetup;

		for ( Uint32 lodInd = 0; lodInd < meshView.GetNumLODs(); ++lodInd )
		{
			if ( !updateSetup[lodInd] )
			{
				continue;
			}

			for ( Uint32 i = 0; i < lodInd; ++i )
			{
				if ( updateSetup[i] && m_LODSetup[i].m_distance > m_LODSetup[lodInd].m_distance )
				{
					updateSetup[lodInd] = false;
					break;
				}
			}

			if ( updateSetup[lodInd] )
			{
				UpdateDistance( meshView, updateSetup, lodInd );
			}
		}
	}

	void MeshCustomPropertiesDescriptor_LODSetup::UpdateDistance( mesh::DataView& meshView, const red::DynArray<Bool>& updateSetup, Uint32 lodIndex ) const
	{
		if ( m_LODSetup[lodIndex].m_distance == meshView.GetLODDistance( lodIndex ) )
		{
			return;
		}

		if ( m_LODSetup[lodIndex].m_distance > meshView.GetLODDistance( lodIndex ) )
		{
			// increasing distance - if next LOD's distance is smaller but will be enlarged with this batch tool, try to update it now
			Uint32 nextInd = lodIndex + 1;
			if ( nextInd < meshView.GetNumLODs() && m_LODSetup[lodIndex].m_distance >= meshView.GetLODDistance( nextInd ) )
			{
				if ( updateSetup[nextInd] && m_LODSetup[nextInd].m_distance > meshView.GetLODDistance( nextInd ) )
				{
					UpdateDistance( meshView, updateSetup, nextInd );
				}
			}
		}

		meshView.SetLODDistance( lodIndex, m_LODSetup[lodIndex].m_distance );
	}

	Bool MeshCustomPropertiesDescriptor_LODSetup::SupportsResource( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CMesh >();
	}

	const String& MeshCustomPropertiesDescriptor_LODSetup::GetTitle() const
	{
		static String title( "LOD Setup" );
		return title;
	}
}


RTTI_BEGIN_TYPE_IN_NAMESPACE( LODSetup, tools );
	RTTI_PROPERTY( m_flags ).editable();
	RTTI_PROPERTY( m_distance ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshCustomPropertiesDescriptor_LODSetup, tools );
	RTTI_PARENT_TYPE( tools::IResourceCustomPropertiesDescriptor );
	RTTI_PROPERTY( m_LODSetup ).editable();
RTTI_END_TYPE();