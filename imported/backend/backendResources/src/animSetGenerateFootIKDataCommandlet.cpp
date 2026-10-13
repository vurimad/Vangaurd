/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"
#include "../../backendAnimation/include/footIkAnimDataGeneration.h"
#include "../../../common/game/include/gameItemActions.h"
#include "../../../common/gameTweakDB/include/records/tweakDBItem.h"
#include "../../../common/generalEditorBackend/include/animTreeNodeData.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

class AnimSetGenerateFootIKDataCommandlet final : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( AnimSetGenerateFootIKDataCommandlet );
	RED_BASE_CLASS( ResaveCommandlet );

public:
	AnimSetGenerateFootIKDataCommandlet();
	virtual ~AnimSetGenerateFootIKDataCommandlet() = default;

	bool Execute( const red::CommandLine& options ) override;
	const AnsiChar* GetOneLiner() const override { return ""; }
	void PrintHelp() const override {}

private:
	bool OnResourcePreSave( CResource& resource ) override;

	THandle< tools::FootIKDataGenerationOptions > m_generationOptions;
};

RTTI_BEGIN_TYPE( AnimSetGenerateFootIKDataCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

AnimSetGenerateFootIKDataCommandlet::AnimSetGenerateFootIKDataCommandlet()
	: TBaseClass( RED_NAME_CONSTEXPR( "animSetGenerateFootIKData" ) )
{}

Bool AnimSetGenerateFootIKDataCommandlet::Execute( const red::CommandLine& options )
{
	// TODO add command line options to control generation options
	m_generationOptions = CreateHandle< tools::FootIKDataGenerationOptions >(); // TODO class member
	// we only want to regenerate the data, don't add it if the animation doesn't have it already
	m_generationOptions->m_forceAddData = false;
	

	const auto result = TBaseClass::Execute( options );
	return result;
}

bool AnimSetGenerateFootIKDataCommandlet::OnResourcePreSave( CResource& resource )
{
	const auto& resourcePath = resource.GetPath();
	if( !resource.IsA< anim::AnimSet >() )
	{
		RED_LOG_ERROR( "Resource '%hs' is not an anim set.", resourcePath.ToDebugString() );
		return false;
	}

	auto& animSet = static_cast< anim::AnimSet& >( resource );
	const auto& anims = animSet.EDITOR_GetAnimations();
	const auto rig = animSet.EDITOR_GetRig().Get();

	
	Bool animSetModifed = false;
	const auto result = GenerateFootIKDataFloatTracksOnAnimset( rig, anims, {}, m_generationOptions, animSetModifed );
	if( !result )
	{
		RED_LOG_ERROR( "Failure occurred generating data for resource '%hs'.", resourcePath.ToDebugString() );
	}
	// only return true if we actually modified the animset, otherwise we end up with many animsets
	// that are "modified" but are changes that don't matter and just clutter up the CL
	// TODO: return false makes it say Failed: it would be nice if it said Unchanged: or something
	// if it actually succeeded but it just didn't need to change anything
	return result && animSetModifed;
}
