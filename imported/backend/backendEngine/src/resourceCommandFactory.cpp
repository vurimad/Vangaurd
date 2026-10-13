/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "resourceCommandFactory.h"
#include "resourceImportCommand.h"
#include "resourceBackend.h"
#include "environmentPresetsManager.h"

#include "../../../common/redReflection/include/resourceClassLookup.h"
//#include "../../../common/resource/include/backendResourceDepot.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( GeneralResourceCommand, tools );
RTTI_END_TYPE();

namespace tools
{

	ResourceCommandFactory::ResourceCommandFactory( ResourceBackend* backend )
		: m_backend( backend )
	{
		RED_FATAL_ASSERT( m_backend );
		RED_FATAL_ASSERT( m_backend->GetResourceDepot() );
	}

	ResourceCommandFactory::~ResourceCommandFactory()
	{
	}

	tools::ResourceCommandUniquePtr ResourceCommandFactory::CreateImportCommand( const res::ResourcePath& destinationPath, const red::AbsolutePath& sourcePath )
	{
		return red::CreateUniquePtr< ResourceImportCommand >( destinationPath, sourcePath, *m_backend->GetEnvironmentPresetsManager(), m_backend->GetResourceDepot(), &m_backend->GetThumbnailNotifier() );
	}

	tools::ResourceCommandUniquePtr ResourceCommandFactory::CreateCommand( const CName& commandName, const res::ResourcePath& resPath, const red::DynArray< res::ResourcePath >& selectedPaths )
	{
		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( resPath );
		if ( !resourceClass )
		{
			return nullptr;
		}

		red::DynArray< const rtti::ClassType* > commandsClasses;
		GetRttiSystem().EnumClasses( ClassID<GeneralResourceCommand>(), commandsClasses );

		for ( const rtti::ClassType* commandClass : commandsClasses )
		{
			GeneralResourceCommand* command = commandClass->GetDefaultObject< GeneralResourceCommand >();
			if ( command->SupportsResource( resourceClass ) && command->GetName() == commandName )
			{
				GeneralResourceCommand* result = commandClass->CreateObject<GeneralResourceCommand>();
				result->Initialize( resPath, selectedPaths );
				return ResourceCommandUniquePtr( result );
			}
		}

		return nullptr;
	}
}
