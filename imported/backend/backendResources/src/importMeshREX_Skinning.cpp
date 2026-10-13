#include "build.h"

#include "importMeshREX_Common.h"
#include "importMeshREX_Skinning.h"
#include "../../backendMesh/include/meshDataBuilder.h"


using red::DynArray;
using red::String;


void ImportRigMatrices( const std::vector< ReFileMeshPtr >& meshes, mesh::DataBuilder& buildData, DynArray< DynArray< Int32 > >& rigMapping )
{
	Uint32 meshesSize = (Uint32)meshes.size();
	rigMapping.Resize( meshesSize );

	for( Uint32 m = 0; m < meshesSize; m++ )
	{
		ReFileMeshPtr mesh = meshes[ m ];
		Int32 numRigMatrices = mesh->getNumRigMatrices();
		if( numRigMatrices > 0 )
		{
			rigMapping[ m ].Resize( numRigMatrices );
			for( Int32 i = 0; i < numRigMatrices; i++ )
			{
				//check if its already there
				Int32 indexInArray = 0;
				Bool alreadyAdded = false;

				const CName boneName = RED_NAME( mesh->getRigMatrix( i ).getName().getData() );

				for( Uint32 j = 0; j < buildData.m_bones.Size(); j++ )
				{
					if( buildData.m_bones[ j ].m_name == boneName )
					{
						alreadyAdded = true;
						indexInArray = static_cast< Int32 >( j );
						break;
					}
				}

				if( !alreadyAdded )
				{
					indexInArray = buildData.m_bones.Size();

					mesh::DataBuilder::UnpackedBone boneInfo;
					boneInfo.m_name = boneName;
					boneInfo.m_rig.SetIdentity();

					Vector4 xx;
					Vector4 yy;
					Vector4 zz;

					xx.X = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 0 ];
					xx.Y = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 1 ];
					xx.Z = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 2 ];

					yy.X = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 3 ];
					yy.Y = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 4 ];
					yy.Z = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 5 ];

					zz.X = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 6 ];
					zz.Y = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 7 ];
					zz.Z = (Float)mesh->getRigMatrix( i ).mRigMatrix[ 8 ];

					boneInfo.m_rig.Set33( xx, yy, zz );
					boneInfo.m_rig.SetTranslation(
						(Float)mesh->getRigMatrix( i ).mRigMatrix[ 9 ] * c_fromReScale,
						(Float)mesh->getRigMatrix( i ).mRigMatrix[ 10 ] * c_fromReScale,
						(Float)mesh->getRigMatrix( i ).mRigMatrix[ 11 ] * c_fromReScale );

					buildData.m_bones.PushBack( boneInfo );
				}
				rigMapping[ m ][ i ] = indexInArray;
			}
		}
	}
}
