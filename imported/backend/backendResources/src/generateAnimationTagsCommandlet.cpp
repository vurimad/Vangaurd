/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "resaveCommandlet.h"

#include "../../backendAnimation/include/animationTags.h"

class AnimationSetTagGenResaveCommandlet : public ResaveCommandlet
{
	RED_BASE_CLASS( ResaveCommandlet );
	RTTI_DECLARE_TYPE( AnimationSetTagGenResaveCommandlet );

public:
	AnimationSetTagGenResaveCommandlet();
	virtual ~AnimationSetTagGenResaveCommandlet() override final;

protected:
	virtual bool Execute( const red::CommandLine& options ) override final;
	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;
	virtual bool IgnoreFile( const red::AbsolutePath& path ) override final;
	virtual bool OnResourcePreSave( CResource& resource ) override final;
};

RTTI_BEGIN_TYPE( AnimationSetTagGenResaveCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

AnimationSetTagGenResaveCommandlet::AnimationSetTagGenResaveCommandlet()
	: TBaseClass( RED_NAME_CONSTEXPR( "AnimSetTagGenResave" ) )
{}

AnimationSetTagGenResaveCommandlet::~AnimationSetTagGenResaveCommandlet() = default;

bool AnimationSetTagGenResaveCommandlet::Execute( const red::CommandLine& options )
{
	return TBaseClass::Execute( options );
}

const char* AnimationSetTagGenResaveCommandlet::GetOneLiner() const
{
	return "Generates animation tags for animsets and then performs a resave of them.";
}

void AnimationSetTagGenResaveCommandlet::PrintHelp() const
{

}

bool AnimationSetTagGenResaveCommandlet::IgnoreFile( const red::AbsolutePath& path )
{
	red::StringView extension = red::paths::GetExtension( path );

	return extension != "anims";
}

bool AnimationSetTagGenResaveCommandlet::OnResourcePreSave( CResource& resource )
{
	if ( !TBaseClass::OnResourcePreSave( resource ) )
		return false;

	THandle< anim::AnimSet > animationSetResource = Cast< anim::AnimSet >( HandleFromPtr( &resource ) );
	if ( !animationSetResource )
		return false;

	tools::GenerateAnimationTagsFromAnimsetDirect( animationSetResource );

	return true;
}
