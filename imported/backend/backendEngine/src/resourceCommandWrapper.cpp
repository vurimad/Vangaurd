/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "resourceCommandWrapper.h"

#include "resourceEditor.h"
#include "resourceBackend.h"
#include "../../../common/redReflection/include/resourceMonitor.h"

namespace tools
{

	void ResourceCommandWrapper::SetupInterop( ResourceCommandWrapper* operationWrapper, ::interop::utils::Object& op )
	{
		op.RegisterObject( "descriptor", operationWrapper->m_objectCollection->GetRootInterop() );
	}

	ResourceCommandWrapper::ResourceCommandWrapper( ResourceCommandUniquePtr command, THandle< ResourceCommandOptions >& options )
		: m_actionHistory( red::CreateUniquePtr<IEditActionHistory>( nullptr ) )
		, m_objectCollection( red::CreateUniquePtr<ObjectCollection>( *m_actionHistory ) )
		, m_options( options )
		, m_command( std::move( command ) )
	{
		m_objectCollection->SetObject( m_options.Get() );
	}

	ResourceCommandWrapper::~ResourceCommandWrapper()
	{
	}

	const THandle<ResourceCommandOptions>& ResourceCommandWrapper::GetOptions() const
	{
		return m_options;
	}

	ResourceCommandResult ResourceCommandWrapper::Execute()
	{
		return m_command->Execute( m_options );
	}

} // namespace tools