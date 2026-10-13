/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/animation/include/animNode_LevelOfDetail.h"
#include "../../backendAnimation/src/animGraphBackend.h"
#include "../../backendAnimation/src/animGraphBackend_Operation.h"

class VehicleAnimLodSetterCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( VehicleAnimLodSetterCommandlet );
	RED_BASE_CLASS( ResaveCommandlet );

public:
	VehicleAnimLodSetterCommandlet();
	virtual ~VehicleAnimLodSetterCommandlet();

	virtual const AnsiChar* GetOneLiner() const override final;
	virtual Bool Execute( const red::CommandLine& options ) override final;
	virtual void PrintHelp() const override final;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool ProcessRig( anim::Rig& rig, Bool isBase ) const;
	Bool ProcessAnimGraph( anim::AnimGraph& animGraph, Bool isBase ) const;
};

RTTI_BEGIN_TYPE( VehicleAnimLodSetterCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

VehicleAnimLodSetterCommandlet::VehicleAnimLodSetterCommandlet()
	: TBaseClass( RED_NAME_CONSTEXPR( "vehicleAnimLodSetterCommandlet" ) )
{
}

VehicleAnimLodSetterCommandlet::~VehicleAnimLodSetterCommandlet() = default;

const AnsiChar* VehicleAnimLodSetterCommandlet::GetOneLiner() const
{
	return nullptr;
}

void VehicleAnimLodSetterCommandlet::PrintHelp() const 
{
}

Bool VehicleAnimLodSetterCommandlet::Execute( const red::CommandLine& options )
{
	RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: \n\tVEHICLE ANIM LOD SETTER COMMANDLET STARTS...\n" );
	const Bool baseExecuteResult = TBaseClass::Execute( options );
	RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: \n\tVEHICLE ANIM LOD SETTER COMMANDLET ENDS...\n" );
	return baseExecuteResult;
}

Bool VehicleAnimLodSetterCommandlet::OnResourcePreSave( CResource& resource )
{
#if 0
	if ( resource.IsA< anim::Rig >() )
	{
		anim::Rig& rig = static_cast< anim::Rig& >( resource );
		if ( rig.GetLODsCount() == 1 )
		{
			RED_LOG_WARNING( "VehicleAnimLodSetterCommandlet: There is only one LOD in '%hs'.", rig.GetPath().ToDebugString() );
		}
	}
#endif

	const String pathStr = resource.GetPath().ToString();
	constexpr AnsiChar* c_generalVehiclesPath = "base\\vehicles";
	if ( !pathStr.Contains( c_generalVehiclesPath ) )
	{
		RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: Resouce '%hs' is not a vehicle-related resource.", resource.GetPath().ToDebugString() );
		return false;
	}
	constexpr AnsiChar* c_baseVehiclesPath = "base\\vehicles\\base_rig";
	const Bool isBase = pathStr.Contains( c_baseVehiclesPath );
	
	if ( resource.IsA< anim::Rig >() )
	{
		anim::Rig& rig = static_cast< anim::Rig& >( resource );
		if ( !ProcessRig( rig, isBase ) )
		{
			RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: Failed to process rig '%hs'.", rig.GetPath().ToDebugString() );
			return false;
		}
	}
	else if ( resource.IsA< anim::AnimGraph >() )
	{
		anim::AnimGraph& animGraph = static_cast< anim::AnimGraph& >( resource );
		if ( !ProcessAnimGraph( animGraph, isBase ) )
		{
			RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: Failed to process anim graph '%hs'.", animGraph.GetPath().ToDebugString() );
			return false;
		}
	}
	else
	{
		RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: Resource '%hs' is neither rig or anim graph.", resource.GetPath().ToDebugString() );
		return false;
	}

	return true;
}

Bool VehicleAnimLodSetterCommandlet::ProcessRig( anim::Rig& rig, Bool isBase ) const
{
	if ( !isBase )
	{
		// Non-base rigs have correct settings (default ones).
		RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: Non-base rig '%hs' was skipped.", rig.GetPath().ToDebugString() );
		return false;
	}

	red::DynArray<Int16>& distanceCategoryToLodMap = rig.EDITOR_GetDistanceCategoryToLodMap();
	distanceCategoryToLodMap.Resize( 5 );
	if ( rig.GetLODsCount() == 2 )
	{
		distanceCategoryToLodMap[0] = 0;
		distanceCategoryToLodMap[1] = 0;
		distanceCategoryToLodMap[2] = 0;
		distanceCategoryToLodMap[3] = 1;
		distanceCategoryToLodMap[4] = 1;
	}
	else if ( rig.GetLODsCount() == 3 )
	{
		distanceCategoryToLodMap[0] = 0;
		distanceCategoryToLodMap[1] = 0;
		distanceCategoryToLodMap[2] = 1;
		distanceCategoryToLodMap[3] = 2;
		distanceCategoryToLodMap[4] = 2;
	}
	else
	{
		RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: Base vehicle rig '%hs' contains different number of LODs (%u) than expected (2 or 3).", rig.GetPath().ToDebugString(), rig.GetLODsCount() );
		return false;
	}


	RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: Base rig '%hs' was processed correctly.", rig.GetPath().ToDebugString() );
	return true;
}

Bool VehicleAnimLodSetterCommandlet::ProcessAnimGraph( anim::AnimGraph& animGraph, Bool isBase ) const
{
	Bool anyLodNodeVisited = false;
	Bool result = true;
	THandle< tools::AnimGraphDescriptor > animGraphDescriptor = Cast< tools::AnimGraphDescriptor >( animGraph.GetBackendData() );
	RED_ASSERT( animGraphDescriptor );
	auto visitor = [&anyLodNodeVisited, &result, &animGraph]( tools::IGraphNodeDescriptor* nodeDescriptor )
	{
		if ( anim::AnimNode_LODBegin* lodNode = Cast< anim::AnimNode_LODBegin >( nodeDescriptor->GetNodeData().Get() ) )
		{
			anyLodNodeVisited = true;
			if ( lodNode->EDITOR_GetLOD() > 3 )
			{
				RED_LOG_ERROR( "VehicleAnimLodSetterCommandlet: LOD Begin node with id = %u in '%hs' has too big LOD number (%u).", lodNode->GetNodeID(), animGraph.GetPath().ToDebugString(), lodNode->EDITOR_GetLOD() );
				result = false;
			}
			lodNode->EDITOR_SetLOD( 3 - lodNode->EDITOR_GetLOD() );
			tools::AnimGraphBackend_LODNode* lodNodeDescriptor = Cast< tools::AnimGraphBackend_LODNode >( nodeDescriptor );
			RED_ASSERT( lodNodeDescriptor );
			lodNodeDescriptor->m_caption = lodNodeDescriptor->GetCaption();
		}
	};
	animGraphDescriptor->IterateNodes( visitor );
	if ( result )
	{
		if ( anyLodNodeVisited )
		{
			RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: Anim graph '%hs' was processed correctly.", animGraph.GetPath().ToDebugString() );
			return true;
		}
		else
		{
			RED_LOG_INFO( "VehicleAnimLodSetterCommandlet: Anim graph '%hs' was skipped.", animGraph.GetPath().ToDebugString() );
			return false;
		}
	}
	else
	{
		return false;
	}
}
