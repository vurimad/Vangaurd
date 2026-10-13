/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "tweakdbInteropHelper.h"
#include "..\..\common\gameTweakDB\include\tweakDB.h"

namespace tools
{
	TweakDbInteropHelper::TweakDbInteropHelper() = default;
	TweakDbInteropHelper::~TweakDbInteropHelper() = default;

	void TweakDbInteropHelper::SetupInterop( TweakDbInteropHelper* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "visualTags", ptr->m_visualTags );
#ifdef ALLOW_TWEAK_DB_RELOADING
		op.RegisterFunctor< void >( "reload", [] ( ::interop::IContext& ) {
			game::data::GetTweakDB().ReloadOnce( false );
		} );
#endif
	}
}
