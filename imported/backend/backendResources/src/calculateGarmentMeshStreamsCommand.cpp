/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "calculateGarmentMeshStreamsCommand.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshParamGarmentSupport.h"

#include "../../backendMesh/include/meshBackendUtils.h"
#include "../../backendREA/include/reFileHelpers.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

namespace tools
{
	// Custom editor class for choosing mesh body type abbreviation (i.e. "ma", "wa", "mb", "mf" etc.).
	class CustomPropertyEditor_MeshBodyTypeName : public IEditableCustomProperty									/// @todo DP: move to separate file?
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_MeshBodyTypeName );

	public:
		CustomPropertyEditor_MeshBodyTypeName() 
			: m_bodyTypeNames{ red::PoolBackend() }
		{
			m_bodyTypeNames = mesh::GetAllBodyTypeNames();
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override final;
		virtual Bool HasOptions() const override final;
		virtual const red::DynArray<String>& GetOptions() const override final;
		virtual CName GetControlType() const override final;

	private:
		red::DynArray<red::String> m_bodyTypeNames;
	};

	const Bool CustomPropertyEditor_MeshBodyTypeName::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const
	{
		return customTypeName == RED_NAME_CONSTEXPR_NOREG( "MeshBodyTypeName" );
	}

	Bool CustomPropertyEditor_MeshBodyTypeName::HasOptions() const
	{
		return true;
	}

	const red::DynArray<String>& CustomPropertyEditor_MeshBodyTypeName::GetOptions() const
	{
		return m_bodyTypeNames;
	}

	CName CustomPropertyEditor_MeshBodyTypeName::GetControlType() const
	{
		return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ) );
	}

	//-------------------------------------------------------------------------------------------------------------------------

	class CalculateGarmentMeshStreamsOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( CalculateGarmentMeshStreamsOptions );

	public:
		CalculateGarmentMeshStreamsOptions( red::StringView bodyType = "ma" );

		RED_INLINE const String& GetBodyType() const { return m_bodyType; }
		Bool IsBodyTypeRecognitionOn() const { return m_bodyTypeAutomaticRecognition; }
		Bool ShouldSkipProperStreams() const { return m_skipProperStreams; }
		Bool ShouldSkipHandmadeStreams() const { return m_skipHandmadeStreams; }

		virtual Bool NeedOptions() const override;

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

	private:
		String m_bodyType;
		Bool m_skipProperStreams = true;
		Bool m_skipHandmadeStreams = true;
		Bool m_bodyTypeAutomaticRecognition = true;
	};

	CalculateGarmentMeshStreamsOptions::CalculateGarmentMeshStreamsOptions( red::StringView bodyType ) : m_bodyType( bodyType.ToString() )
	{
	}

	Bool CalculateGarmentMeshStreamsOptions::NeedOptions() const
	{
		return true;
	}

	void CalculateGarmentMeshStreamsOptions::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		ResourceCommandOptions::GetCustomEditableProperties( outRootProperties );

		if ( !m_bodyTypeAutomaticRecognition )
		{
			rtti::ClassEditablePropertyInfo info;
			info.m_name = RED_NAME_CONSTEXPR( "bodyType" );
			info.m_type = GetTypeObject< CName >();
			info.m_customTypeName = RED_NAME_CONSTEXPR( "MeshBodyTypeName" );
			outRootProperties.Add( info );
		}
	}

	void CalculateGarmentMeshStreamsOptions::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		if ( path.ToString().EqualsNC( "bodyTypeAutomaticRecognition" ) )
		{
			NotifyPropertyChanged( rtti::AccessPath() );
		}
	}

	//-------------------------------------------------------------------------------------------------------------------------

	Bool CalculateGarmentMeshStreamsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName CalculateGarmentMeshStreamsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "calculateGarmentMeshStreams" );
	}

	THandle< ResourceCommandOptions > CalculateGarmentMeshStreamsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		res::ResourcePath meshPath = GetResourcePath();
		String bodyTypeApprev = mesh::ExtractBodyTypeNameFromMeshPath( meshPath.ToStringView() );

		THandle< CalculateGarmentMeshStreamsOptions > options = CreateHandle< CalculateGarmentMeshStreamsOptions >( bodyTypeApprev );
		if ( suggestedOptions && suggestedOptions->GetClass() == options->GetClass() )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
		}

		return options;
	}

	ResourceCommandResult CalculateGarmentMeshStreamsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< CalculateGarmentMeshStreamsOptions > generationOptions = Cast< CalculateGarmentMeshStreamsOptions >( options );
		if ( !generationOptions )
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		String bodyTypeAbbrev = generationOptions->GetBodyType();
		if ( generationOptions->IsBodyTypeRecognitionOn() )
		{
			String bodyType = mesh::ExtractBodyTypeNameFromMeshPath( GetResourcePath().ToStringView() );
			if ( bodyType.Empty() )
			{
				return ResourceCommandResult::Error( "Couldn't recognize body type automatically" );
			}
			bodyTypeAbbrev = bodyType;
		}

		TResRef< CMesh > meshRef( GetResourcePath() );
		THandle< CMesh > mesh = meshRef.Get();

		mesh::DataViewRO meshView( *mesh.Get() );
		
		if ( generationOptions->ShouldSkipProperStreams() )
		{
			Bool needsGeneration = false;
			for ( Uint32 i = 0; i < meshView.GetNumChunks(); i++ )
			{
				mesh::ChunkStreamDataRO garmentStream = meshView.GetChunkStreamData( i, EMeshStreamType::MST_GarmentFlags_U32 );
				mesh::ChunkStreamDataRO morphOffsetStream = meshView.GetChunkStreamData( i, EMeshStreamType::MST_MorphOffset_3F );

				if ( garmentStream.Empty() || morphOffsetStream.Empty() )
				{
					needsGeneration = true;
					break;
				}

				Float avg = 0.f;
				for ( mesh::StreamIteratorRO<Vector3> it( morphOffsetStream ); it; ++it )
				{
					Vector3 m = ( *it );
					Float len = m.Mag();      // Not using Normalize, since it crashes on vectors like e+24.
					if ( len != 0 )
					{
						Float oolen = 1.0f / len;
						m *= oolen;
						len = m.Mag();
					}
					avg += len;
				}

				avg = avg / morphOffsetStream.m_count;
				if ( avg <= 0 )
				{
					needsGeneration = true;
					break;
				}
			}

			if ( !needsGeneration )
			{
				return ResourceCommandResult::Success( red::DynArray< THandle< CResource > >(), "Mesh contains proper streams, skipping." );
			}
		}

		if ( generationOptions->ShouldSkipHandmadeStreams() )
		{
			THandle< mesh::MeshParamGarmentSupport > garmentParam = meshView.GetParameter< mesh::MeshParamGarmentSupport >();
			if ( garmentParam && garmentParam->HasCustomMorphs() )
			{
				return ResourceCommandResult::Success( red::DynArray< THandle< CResource > >(), "Mesh contains handmade garment streams, skipping." );
			}
		}

		res::ResourcePath meshPath = GetResourcePath();
		Bool dispatcherStartResult = ReFileHelpers::ExportGarmentHelper( meshPath, bodyTypeAbbrev );     // Note, that this is not the result of the operation, just of trigering it in the dispatcher.

		if ( !dispatcherStartResult )
			return ResourceCommandResult( "Unable to start Houdini dispatcher - see log for more information" );

		return ResourceCommandResult( red::DynArray< THandle< CResource > >() );					/// @todo DP: it would be best to make this operation async and wait untill the dispatcher finishes and notifies
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( CalculateGarmentMeshStreamsCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_MeshBodyTypeName, tools );
	RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CalculateGarmentMeshStreamsOptions, tools );
RTTI_PARENT_TYPE( ResourceCommandOptions );
	RTTI_PROPERTY( m_skipProperStreams ).editable();
	RTTI_PROPERTY( m_skipHandmadeStreams ).editable();
	RTTI_PROPERTY( m_bodyTypeAutomaticRecognition ).editable();
	RTTI_PROPERTY( m_bodyType );
RTTI_END_TYPE();
