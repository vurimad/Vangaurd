#include "build.h"
#include "clearDefaultInstancedParamsCommand.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendEngine/include/resourceOptions.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( ClearDefaultInstancedParams_Command, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class ClearDefaultInstancedParams_Options : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( ClearDefaultInstancedParams_Options );

	public:
		virtual Bool NeedOptions() const override
		{
			return false;
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////

	Bool ClearDefaultInstancedParams_Command::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName ClearDefaultInstancedParams_Command::GetName() const
	{
		return RED_NAME_CONSTEXPR( "clearDefaultInstancedParams" );
	}

	ResourceCommandResult ClearDefaultInstancedParams_Command::Execute( const THandle<ResourceCommandOptions>& options )
	{
		return ClearDefaultInstancedParams();
	}

	THandle< ResourceCommandOptions > ClearDefaultInstancedParams_Command::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions /*= nullptr */ ) const
	{
		return CreateHandle< ClearDefaultInstancedParams_Options >();
	}

	ResourceCommandResult ClearDefaultInstancedParams_Command::ClearDefaultInstancedParams() const
	{
		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;
		
		THandle< CMesh > mesh = GResourceLoader->IssueLoadingRequest( Convert( GetResourcePath(), loadingOptions ) )->WaitUntilLoaded< CMesh >();
		if ( !mesh )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to load %hs", GetResourcePath().ToDebugString() ) );
		}

		mesh::DataView meshDataView = mesh::DataView( *mesh.Get() );

		red::DynArray< Float > distances;
		for ( Uint32 i = 0; i < meshDataView.GetNumMaterials(); ++i )
		{
			THandle< CMaterialInstance > materialData = Cast< CMaterialInstance >( meshDataView.GetMaterialData( i ) );
			if ( materialData == nullptr )
			{
				continue;
			}

			const CMaterialTemplate* matTemplate = Cast< const CMaterialTemplate >( materialData->GetMaterialDefinition() );
			if ( matTemplate == nullptr )
			{
				continue;
			}

			const TMaterialParameterInstances instancedParams = materialData->GetParameters();
			for ( const MaterialParameterInstance& instanceData : instancedParams )
			{
				rtti::ValuePtr baseValue, instancedValue;
				if ( materialData->ReadCustomEditableProperty( IRTTIContext::GetDefault(), instanceData.GetName(), rtti::AccessPath(), instancedValue )
					&& matTemplate->ReadCustomEditableProperty( IRTTIContext::GetDefault(), instanceData.GetName(), rtti::AccessPath(), baseValue ) )
				{
					if ( baseValue->ToString().EqualsNC( instancedValue->ToString() ) )
					{
						// write custom editable property handles 
						materialData->ClearInstanceParameter( instanceData.GetName() );
					}
				}
			}
		}

		if ( !res::SaveResource( mesh ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to clear default instanced params from '%hs' because saving failed", GetResourcePath().ToDebugString() ) );
		}

		return ResourceCommandResult::Success( mesh );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( ClearDefaultInstancedParams_Options, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_END_TYPE();