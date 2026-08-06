/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageObjectLoader.h"
#include "packageIterator.h"
#include "packageTableOfContentReader.h"
#include "packageReadStream.h"
#include "packageReader.h"
#include "packageErrorReporter.h"
#include "packageTypeSerializerDictionary.h"
#include "resourceToken.h"
#include "resourceLoader.h"

#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "../../redJobs2/include/jobCounterOwner.h"

namespace red
{
	PackageObjectLoaderJobDispatcher::PackageObjectLoaderJobDispatcher()
		: m_loader( nullptr )
	{
	}

	PackageObjectLoaderJobDispatcher::~PackageObjectLoaderJobDispatcher()
	{
	}

	void PackageObjectLoaderJobDispatcher::Initialize( const PackageObjectLoader * loader )
	{
		m_loader = loader;
	}

	void PackageObjectLoaderJobDispatcher::ScheduleSerializationJob( job::Builder & builder ) const
	{
		red::WeakPtr< PackageObjectLoader > weakLoader = m_loader->SharedFromThis();
	
		builder.DispatchJob( "PackageObjectLoader_Serialize", [weakLoader]( const job::RunContext & context )
		{
			red::SharedPtr< PackageObjectLoader > loader = weakLoader.Lock();
			if( loader )
			{
				loader->Internal_ExecuteSerialization();
			}
		} );	 
	}

	void PackageObjectLoaderJobDispatcher::SchedulePostLoadJob( job::Builder & builder ) const
	{
		red::WeakPtr< PackageObjectLoader > weakLoader = m_loader->SharedFromThis();

		builder.DispatchJob( "PackageObjectLoader_PostLoad", [weakLoader]( const job::RunContext & context )
		{
			red::SharedPtr< PackageObjectLoader > loader = weakLoader.Lock();
			if( loader )
			{
				loader->Internal_ExecutePostLoad();
			}
		} );	
	}

	PackageObjectLoader::PackageObjectLoader()
		: m_optionalMask( red::PoolEngine() )
		, m_resourceLoader( nullptr )
		, m_loadedObjects( red::PoolEngine() )
		, m_skipPostLoad( false )
		, m_skipImports( false ) 
		, m_skipUploadRenderData( false )
		, m_priority( job::Priority::Latent )
		, m_affinity( job::Affinity::All )
	{
	}

	PackageObjectLoader::~PackageObjectLoader()
	{
	}

	void PackageObjectLoader::Initialize( PackageObjectLoaderParameter& parameter )
	{		
		m_sourcePackage = parameter.sourcePackage;
		m_resourceLoader = parameter.resourceLoader;
		m_optionalMask = parameter.optionalMask;
		m_optionalBuffer = std::move( parameter.optionalPackageBuffer );
		m_skipPostLoad = parameter.skipPostLoad;
		m_skipImports = parameter.skipImports;
		m_skipUploadRenderData = parameter.skipUploadRenderData;
		m_priority = parameter.priority;
		m_affinity = parameter.affinity;
		m_ioPriority = parameter.ioPriority;

		m_loadedObjects.Resize( m_sourcePackage.rootObjectTable.Size() );

		m_tableOfContent = CreatePackageTableOfContentReader( m_sourcePackage, m_table );
		m_stream = CreateReadStream( m_sourcePackage.buffer );
		m_dictionary = CreatePackageTypeSerializerDictionary();
		m_errorReporter = red::CreateUniquePtr< PackageErrorReporter >();

		PackageReaderParameter param = 
		{
			m_sourcePackage.version, 
			m_stream.Get(), 
			m_tableOfContent.Get(),
			m_dictionary.Get(),
			m_errorReporter.Get()
		};

		m_packageReader = CreatePackageReader( param );

		m_dispatcher = red::CreateUniquePtr< PackageObjectLoaderJobDispatcher >();
		m_dispatcher->Initialize( this );
	}

	serialization::LoadingToken PackageObjectLoader::IssueJobs()
	{
		IssueAllResourceDependecyLoading();
		
		job::Counter loadingCounter{ job::ScheduleParam( m_priority, m_affinity ) };
		for( const auto & token : m_table.resourceTable )
		{
			if( token )
			{
				loadingCounter += token->GetWaitCounter();
			}
			else
			{ /* if token is null, it most means it's not an import. */ }
		}

		job::Builder builder{ job::ScheduleParam( m_priority, m_affinity ) };
		ScheduleObjectSerialization(builder);

		builder.DispatchWait(loadingCounter);

		ScheduleObjectPostLoad( builder );

		return serialization::LoadingToken( builder.ExtractWaitCounter() );
	}

	void PackageObjectLoader::HACK_WaitOnResourceAndCreateObjects()
	{		
		if( !m_skipImports )
		{
			IssueAllResourceDependecyLoading();

			// ctremblay: Mega Hack for entity template.
			while( 1 )
			{
				bool done = true;

				for( auto & token : m_table.resourceTable )
				{
					if( token && !token->HasFinished() )
					{
						done = false;
						break;
					}
				}

				if( done )
				{
					break;
				}
			}
		}

		Internal_ExecuteSerialization();
		
		if(!m_skipPostLoad)
		{
			Internal_ExecutePostLoad();
		}
	}

	void PackageObjectLoader::WaitOnResourceAndCreateObjects( job::Builder& builder )
	{
		if ( !m_skipImports )
		{
			IssueAllResourceDependecyLoading();

			builder.DispatchJob( "PackageObjectLoader/WaitOnResource", [ this ]( const job::RunContext& ctx )
			{
				job::Builder builder { ctx };
				auto counter = builder.ExtractWaitCounter();

				for ( auto & token : m_table.resourceTable )
				{
					if ( token )
					{
						counter += token->GetWaitCounter();
					}
				}

			} );
		}

		builder.DispatchJob( "PackageObjectLoader/Serialize", [ this ]( const job::RunContext& ctx )
		{
			Internal_ExecuteSerialization();

			if ( !m_skipPostLoad )
			{
				Internal_ExecutePostLoad();
			}
		} );
	}

	red::DynArray< SerializableHandle > PackageObjectLoader::ReleaseObjects()
	{
		return std::move( m_loadedObjects );
	}

	void PackageObjectLoader::IssueAllResourceDependecyLoading()
	{
		if( !m_skipImports )
		{
			for( PackageResourceIterator iter( m_sourcePackage ); iter.IsValid(); iter.Next() )
			{
				if( iter.GetImportType() == PackageResourceImportType_Sync )
				{
					res::IssueLoadingRequestParameter param;
					param.path = iter.GetPath();
					param.postLoadFlags.skipUploadRenderData = m_skipUploadRenderData;
					param.skipPostLoad = m_skipPostLoad;
					param.priority = m_ioPriority;
					param.useGameCache = true;
					const res::ResourceTokenHandle token = m_resourceLoader->IssueLoadingRequest( param );
					m_table.resourceTable[ iter.GetIndex() ] = token;
				}
			}
		}
	}

	void PackageObjectLoader::ScheduleObjectSerialization( job::Builder& builder )
	{
		m_dispatcher->ScheduleSerializationJob( builder );
	}
	
	void PackageObjectLoader::ScheduleObjectPostLoad( job::Builder & builder )
	{
		if( !m_skipPostLoad )
		{
			m_dispatcher->SchedulePostLoadJob( builder );
		}
	}

	void PackageObjectLoader::Internal_ExecuteSerialization()
	{
		CreateAllRequestedObject();
		SerializeAllRequestedObject();

		m_errorReporter->BroadcastAllReportedErrors();	
	
		m_optionalBuffer.Reset();
	}

	void PackageObjectLoader::CreateAllRequestedObject()
	{
		PC_SCOPE( PackageObjectLoader_CreateAllRequestedObject );

		const Uint32 maskSize = m_optionalMask.Size();

		for( Uint32 index = 0, end = m_sourcePackage.rootObjectTable.Size(); index != end; ++index )
		{
			if( index >= maskSize || m_optionalMask.Get( index ) )
			{
				SerializableHandle & rootObject = m_loadedObjects[ index ];
				m_tableOfContent->UnmapObject( index, rootObject );
			}
		}
	}
	
	void PackageObjectLoader::SerializeAllRequestedObject()
	{
		PC_SCOPE( PackageObjectLoader_SerializeAllRequestedObject );

		for( Uint32 pendingIndex = 0; pendingIndex != m_table.pendingObjectContainer.Size(); ++pendingIndex )
		{
			const Int32 pendingObject = m_table.pendingObjectContainer[ pendingIndex ];
			SerializableHandle & handle = m_table.objectTable[ pendingObject ];
			if( handle )
			{
				m_errorReporter->SetCurrentObject( handle );
				const rtti::ClassType * classType = handle->GetClass();
				
				PC_SCOPE_INST_OBJ( classType->GetInstrumentationObject(), classType->GetName().AsChar() );
				
				m_packageReader->SetContextMemoryPool( classType->GetInnerTypeMemoryPool() );
				const ObjectDescriptor & descriptor = m_sourcePackage.objectTable[ pendingObject ];
				m_stream->Seek( descriptor.dataOffset );
				m_packageReader->SerializeType( { handle.Get(), handle->GetClass(), nullptr } );
			}
		}
	}
	
	void PackageObjectLoader::Internal_ExecutePostLoad()
	{
		PC_SCOPE( PackageObjectLoader_ExecutePostLoad );

		PostLoadFlags postLoadFlag;
		postLoadFlag.skipUploadRenderData = m_skipUploadRenderData;
		PostLoadContext context;
		context.flags = postLoadFlag;

		for( SerializableHandle & object : m_table.objectTable )
		{
			if( object )
			{
				PC_SCOPE_INST_OBJ( object->GetClass()->GetInstrumentationObject(), object->GetClass()->GetName().AsChar());

				object->OnPostLoad( context );
			}
		}
	}
	
	void PackageObjectLoader::Internal_SetJobDispatcher( red::UniquePtr< PackageObjectLoaderJobDispatcher > dispatcher )
	{
		m_dispatcher = std::move( dispatcher );
	}
	
	red::SharedPtr< PackageObjectLoader > CreatePackageObjectLoader( PackageObjectLoaderParameter & param )
	{
		red::SharedPtr< PackageObjectLoader > loader = red::CreateUniquePtr< PackageObjectLoader >();
		loader->Initialize( param );
		return loader;
	}
}
