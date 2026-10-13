/**
 * Copyright c 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"

class MeshAppearanceCensorshipCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( MeshAppearanceCensorshipCommandlet );

public:

	MeshAppearanceCensorshipCommandlet();
	virtual ~MeshAppearanceCensorshipCommandlet();

	virtual Bool Execute( const red::CommandLine& options ) override;

	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override final;

private:
	void SetError( const String& error );
	Bool ProcessMesh( const THandle< CMesh >& mesh );

	String m_suffix;

	red::UniquePtr< red::InteractiveCommandletCommunicationHandler > m_comm;
};

RTTI_BEGIN_TYPE( MeshAppearanceCensorshipCommandlet )
RTTI_PARENT_TYPE( ICommandlet )
RTTI_END_TYPE()


MeshAppearanceCensorshipCommandlet::MeshAppearanceCensorshipCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "generateCensoredAppearances" ) )
	, m_suffix( "_censored" )
{
}

MeshAppearanceCensorshipCommandlet::~MeshAppearanceCensorshipCommandlet()
{
}

const char* MeshAppearanceCensorshipCommandlet::GetOneLiner() const
{
	return "Generate mesh censored/naked appearances";
}

void MeshAppearanceCensorshipCommandlet::PrintHelp() const
{
}

Bool MeshAppearanceCensorshipCommandlet::Execute( const red::CommandLine& cmdline )
{
	m_comm = red::CreateUniquePtr< red::InteractiveCommandletCommunicationHandler >( cmdline );

	red::Stages stages = m_comm->DeclareStages( { "Process files" } );

	String param;
	red::AbsolutePath fileListPath;
	if ( !cmdline.GetFirstOption( "filesList", param ) )
	{
		SetError( "Couldn't parse commandline parameter 'filesList'" );
		return false;
	}

	cmdline.GetFirstOption( "suffix", m_suffix );
	if ( !m_suffix.BeginsWith( "_" ) )
	{
		m_suffix.Insert( 0, "_" );
	}

	if ( m_suffix.Length() < 1 )
	{
		SetError( "Missing suffix" );
		return false;
	}

	fileListPath = red::AbsolutePath::CreateFilePath( param );
	if ( !GFileManager->FileExist( fileListPath ) )
	{
		SetError( String::Printf( "Temporary file '%hs' not found", fileListPath.AsChar() ) );
		return false;
	}

	String fileContent;
	if ( !red::LoadFileToString( fileListPath, fileContent ) )
	{
		SetError( String::Printf( "Failed to load temporary file '%hs'", fileListPath.AsChar() ) );
		return false;
	}

	red::DynArray< String > filePaths = fileContent.Split( "\n" );
	const Uint32 size = filePaths.Size();
	const Uint32 taskId = m_comm->GetTask( "Files processed" );
	for ( Uint32 index : filePaths.Indices() )
	{
		const String& filePathString = filePaths[ index ];

		m_comm->SetTaskProgress( taskId, index, size );
		m_comm->GetMessage( taskId, String::Printf( "Processing %hs", filePathString.AsChar() ) );

		if ( !red::paths::ExtractExtension( filePathString ).EqualsNC( "mesh" ) )
		{
			continue;
		}

		res::ResourcePath path = res::ResourcePath::Build( filePathString.AsChar() );
		TResAsyncRef< CMesh > meshRef( path );

		res::LoadingOptions loadingOptions;
		loadingOptions.postLoadFlags.skipUploadRenderData = true;
		if ( THandle< CMesh > mesh = meshRef.IssueLoadingRequest( loadingOptions )->WaitUntilLoaded< CMesh >() )
		{
			if ( ProcessMesh( mesh ) )
			{
				if ( !res::SaveResource( mesh ) )
				{
					SetError( String::Printf( "Failed to save '%hs'", mesh->GetPath().ToString().AsChar() ) );
				}
			}
		}
	}

	stages.FinishCurrentStage();

	return true;
}

void MeshAppearanceCensorshipCommandlet::SetError( const String& errorMsg )
{
	if ( !m_comm )
	{
		return;
	}

	m_comm->SendGenericMessage( errorMsg );
}

Bool MeshAppearanceCensorshipCommandlet::ProcessMesh( const THandle< CMesh >& meshData )
{
	Bool hasChanged = false;

	const red::ArraySpan< const THandle< mesh::MeshAppearance > > sourceAppearances = meshData->GetAllAppearances();
	red::Set< CName > appearancesNames;

	for ( const THandle< mesh::MeshAppearance >& appearance : sourceAppearances )
	{
		appearancesNames.Insert( appearance->GetName() );
	}

	mesh::DataView meshView( *meshData.Get() );
	for ( const CName& app : appearancesNames )
	{
		const String appearanceName( app.AsChar() );
		if ( appearanceName.EndsWith( m_suffix ) )
		{
			continue;
		}

		const CName censoredAppearanceName = RED_NAME( String::Printf( "%hs%hs", appearanceName.AsChar(), m_suffix.AsChar() ) );
		if ( appearancesNames.Exist( censoredAppearanceName ) )
		{
			// regenerate!
			meshView.RemoveAppearance( censoredAppearanceName );
		}

		THandle< mesh::MeshAppearance > appearance = meshView.GetAppearanceData( meshView.GetAppearanceIndex( appearanceName ) );

		Uint32 censoredMaterials = 0;
		for ( Uint32 chunkInd = 0; chunkInd < meshView.GetNumChunks(); ++chunkInd )
		{
			const CName& materialName = appearance->GetChunkMaterialName( chunkInd );
			{
				const String censoredMaterialName = String::Printf( "%hs%hs", materialName.AsChar(), m_suffix.AsChar() );
				// always regenerate!
				/*if ( meshView.GetMaterialDataByName( RED_NAME_CONSTEXPR( censoredMaterialName.AsChar() ) ) )
				{
					continue;
				}*/

				THandle< IMaterial > material = meshView.GetMaterialDataByName( materialName );
				if ( material->IsA< CMaterialInstance >() )
				{
					res::ResourcePath diffuseCensoredPath, normalCensoredPath;

					TResRef< ITexture > textureParam;
					if ( ( material->ReadParameter( RED_NAME_CONSTEXPR( "Albedo" ), textureParam ) || material->ReadParameter( RED_NAME_CONSTEXPR( "BaseColor" ), textureParam ) )
						&& textureParam.IsValid() )
					{
						String filename = red::paths::ExtractFileStem( textureParam.GetPath().ToStringView() );
						if ( !filename.EndsWith( m_suffix ) )
						{
							diffuseCensoredPath = res::ResourcePath::Build(
								String::Printf( "%hs%hs.xbm", textureParam.GetPath().ToString().StringBefore( "." ).AsChar(), m_suffix.AsChar() ).AsChar() );

							if ( !GetResourceDepot()->ResourceExists( diffuseCensoredPath ) )
							{
								diffuseCensoredPath = res::ResourcePath();
							}
						}
					}

					if ( ( material->ReadParameter( RED_NAME_CONSTEXPR( "Normal" ), textureParam ) || material->ReadParameter( RED_NAME_CONSTEXPR( "GlobalNormal" ), textureParam ) )
						&& textureParam.IsValid() )
					{
						String filename = red::paths::ExtractFileStem( textureParam.GetPath().ToStringView() );
						if ( !filename.EndsWith( m_suffix ) )
						{
							normalCensoredPath = res::ResourcePath::Build(
								String::Printf( "%hs%hs.xbm", textureParam.GetPath().ToString().StringBefore( "." ).AsChar(), m_suffix.AsChar() ).AsChar() );

							if ( !GetResourceDepot()->ResourceExists( normalCensoredPath ) )
							{
								normalCensoredPath = res::ResourcePath();
							}
						}
					}

					if ( diffuseCensoredPath.IsEmpty() && normalCensoredPath.IsEmpty() )
					{
						continue;
					}
					censoredMaterials++;

					THandle<IMaterial> baseMaterial = HandleFromPtr( material->GetBaseMaterial() );
					if ( baseMaterial == nullptr )
					{
						baseMaterial = material;
					}

					if ( THandle< CMaterialInstance > censoredMaterial = CreateHandle< CMaterialInstance >( meshData.Get(), baseMaterial ) )
					{
						CMaterialInstance* matExampleInstance = Cast< CMaterialInstance >( material.Get() );
						for ( const MaterialParameterInstance& param : matExampleInstance->GetParameters() )
						{
							censoredMaterial->WriteParameterRaw( param.GetName(), param.GetType(), param.GetData() );
						}

						CName diffuseParamName, normalParamName;
						if ( baseMaterial->ReadParameter( RED_NAME_CONSTEXPR( "Normal" ), textureParam ) )
						{
							normalParamName = RED_NAME_CONSTEXPR( "Normal" );
						}
						else if ( baseMaterial->ReadParameter( RED_NAME_CONSTEXPR( "GlobalNormal" ), textureParam ) )
						{
							normalParamName = RED_NAME_CONSTEXPR( "GlobalNormal" );
						}

						if ( baseMaterial->ReadParameter( RED_NAME_CONSTEXPR( "Albedo" ), textureParam ) )
						{
							diffuseParamName = RED_NAME_CONSTEXPR( "Albedo" );
						}
						else if ( baseMaterial->ReadParameter( RED_NAME_CONSTEXPR( "BaseColor" ), textureParam ) )
						{
							diffuseParamName = RED_NAME_CONSTEXPR( "BaseColor" );
						}

						if ( meshView.GetMaterialDataByName( RED_NAME( censoredMaterialName ) ) )
						{
							meshView.SetMaterialData( meshView.GetMaterialIndex( RED_NAME( censoredMaterialName ) ), censoredMaterial );
						}
						else
						{
							meshView.AddMaterial( censoredMaterial, RED_NAME( censoredMaterialName ) );
						}

						if ( THandle< CMaterialInstance > censoredMaterial = Cast< CMaterialInstance >( meshView.GetMaterialDataByName( RED_NAME( censoredMaterialName ) ) ) )
						{
							if ( diffuseCensoredPath.IsValid() )
							{
								censoredMaterial->WriteParameter( diffuseParamName, TResRef< ITexture >( diffuseCensoredPath ) );
							}

							if ( normalCensoredPath.IsValid() )
							{
								censoredMaterial->WriteParameter( normalParamName, TResRef< ITexture >( normalCensoredPath ) );
							}
						}
					}
				}
			}
		}

		if ( censoredMaterials == 0 )
		{
			SetError( String::Printf( "'%hs': Cannot censor appearance '%hs' due to missing '%hs' textures",
				meshData->GetPath().ToString().AsChar(), appearanceName.AsChar(), m_suffix.AsChar() ) );
			continue;
		}

		if ( !meshView.AddAppearanceCopy( censoredAppearanceName, *appearance.Get() ) )
		{
			SetError( String::Printf( "'%hs': Failed to add appearance '%hs'", meshData->GetPath().ToString().AsChar(), censoredAppearanceName.AsChar() ) );
			continue;
		}

		Int32 censoredAppearanceIndex = meshView.GetAppearanceIndex( censoredAppearanceName );
		if ( censoredAppearanceIndex < 0 )
		{
			SetError( String::Printf( "'%hs': Failed to obtain appearance '%hs' that was just added. This shouldn't happen!", meshData->GetPath().ToString().AsChar(), censoredAppearanceName.AsChar() ) );
			continue;
		}

		for ( Uint32 chunkInd = 0; chunkInd < meshView.GetNumChunks(); ++chunkInd )
		{
			const CName& materialName = appearance->GetChunkMaterialName( chunkInd );
			const String censoredMaterialName = String::Printf( "%hs%hs", materialName.AsChar(), m_suffix.AsChar() );
			if ( !meshView.SetAppearanceChunkMaterial( censoredAppearanceIndex, chunkInd, RED_NAME( censoredMaterialName ) ) )
			{
				SetError( String::Printf( "'%hs': Cannot set material '%hs'. Removing appearance '%hs' as it won't be valid.", meshData->GetPath().ToString().AsChar(),
					censoredMaterialName.AsChar(), censoredAppearanceName.AsChar() ) );
				meshView.RemoveAppearance( censoredAppearanceName );
				break;
			}
		}

		if ( meshData->GetAppearanceSafe( censoredAppearanceName ) )
		{
			hasChanged = true;
		}
	}

	return hasChanged;
}