/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCachePostgresDepsServer.h"

#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/redJobs2/include/jobDeferral.h"
#include "../../../common/redContainers/include/fundamentalStringParser.h"
#include "../../../common/redSystem/include/hex.h"

#include "postgresWrapper.h"

#include <numeric>

RTTI_BEGIN_TYPE_IN_NAMESPACE( PostgresDepsServer, data, prv )
	RTTI_PARENT_TYPE( data::prv::DepsServer )
RTTI_END_TYPE()

namespace data
{
namespace prv
{

static red::String ParamString_GetServerName( const red::StringView& params )
{
	auto atPos = params.Find( '@' );
	if ( atPos != red::StringView::npos )
	{
		auto slashPos = params.Find( '/', atPos );
		if ( slashPos != red::StringView::npos )
		{
			return params.Slice( atPos, slashPos ).ToString();
		}
	}
	return {};
}

//------------------------------------------------------------------------------

class PostgresBatchWorker : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
public:
	PostgresBatchWorker();
	~PostgresBatchWorker();

	bool Initialise( const red::String& params );
	void Shutdown();

	bool IsInitialised() const;

	virtual void ThreadFunc() override;

	Cache::Result QueueQuery( String&& query, dependency::OutputFileList& outputList, job::Builder& builder );

private:

	struct Entry
	{
		red::String m_query;
		dependency::OutputFileList& m_outputList;
		job::CompletionDeferral m_deferral;
		Cache::Result m_result;

		Entry( red::String&& query, dependency::OutputFileList& outputList, job::Builder& builder );
	};
	using EntryPtr = red::UniquePtr< Entry, red::PoolBackend >;

	red::DynArray< EntryPtr > GetEntriesToProcess();
	red::String CombineQueries( const red::DynArray< EntryPtr >& entries ) const;
	bool ProcessResult( postgres::Result& result, dependency::OutputFileList& outputList ) const;

	red::CircularBuffer< EntryPtr > m_queue;
	red::SpinLock m_queueGuard;

	postgres::Connection m_connection;

	mutable red::ManualResetEvent m_wakeEvent;
	red::Atomic< bool > m_shutdownFlag;
};

PostgresBatchWorker::Entry::Entry( red::String&& query, dependency::OutputFileList& outputList, job::Builder& builder )
	: m_query( std::move( query ) )
	, m_outputList( outputList )
	, m_result( false )
{
	job::Counter counter;
	m_deferral = counter.CreateDeferral();
	builder.DispatchWait( counter );
}

PostgresBatchWorker::PostgresBatchWorker()
	: red::Thread( "PostgreSQLDepsServerWorker" )
{
}

PostgresBatchWorker::~PostgresBatchWorker()
{
}

bool PostgresBatchWorker::Initialise( const red::String& params )
{
	m_connection = postgres::Connection::Connect( params );
	if ( m_connection )
	{
		InitThread();

		return true;
	}
	return false;
}

void PostgresBatchWorker::Shutdown()
{
	if ( IsInitialised() )
	{
		if ( !m_shutdownFlag.GetValue() )
		{
			m_shutdownFlag.SetValue( true );
			m_wakeEvent.SetEvent();

			JoinThread();
		}
	}
}

bool PostgresBatchWorker::IsInitialised() const
{
	return m_connection && IsValid();
}

void PostgresBatchWorker::ThreadFunc()
{
	RED_FATAL_ASSERT( m_connection );

	for ( ;; )
	{
		m_wakeEvent.Wait();
		m_wakeEvent.ResetEvent();

		if ( m_shutdownFlag.GetValue() )
		{
			break;
		}

		red::DynArray< EntryPtr > entries = GetEntriesToProcess();
		if ( entries.Empty() )
		{
			continue;
		}

		String combinedQuery = CombineQueries( entries );

		if ( !m_connection.SendQuery( combinedQuery ) )
		{
			continue;
		}

		for ( const auto& entry : entries )
		{
			// NOTE: GetResult might be blocking when the library is waiting for results from the server
			postgres::Result result = m_connection.GetResult();

			bool success = ProcessResult( result, entry->m_outputList );
			entry->m_result.SetValue( success );
			entry->m_deferral.FinishDeferral();
		}

		// Need to call it one more time until it is nullptr, should be the next item in the list
		while ( postgres::Result result = m_connection.GetResult() )
		{
			RED_LOG_ERROR( "PostgreSQLDepsServer: Getting extra results from the server than were expected" );
		}
	}

	m_connection.Disconnect();
}

Cache::Result PostgresBatchWorker::QueueQuery( String&& query, dependency::OutputFileList& outputList, job::Builder& builder )
{
	RED_SCOPE_LOCK( m_queueGuard );
	m_queue.PushBack( EntryPtr::Create( std::move( query ), outputList, builder ) );
	m_wakeEvent.SetEvent();
	auto& entry = m_queue.Back();
	return entry->m_result;
}

red::DynArray< PostgresBatchWorker::EntryPtr > PostgresBatchWorker::GetEntriesToProcess()
{
	RED_SCOPE_LOCK( m_queueGuard );

	red::DynArray< EntryPtr > result{ red::PoolBackend() };
	const Uint32 queueSize = m_queue.Size();
	result.Reserve( queueSize );
	for ( Uint32 i = 0; i < queueSize; ++i )
	{
		result.PushBack( std::move( m_queue.Front() ) );
		m_queue.PopFront();
	}

	return result;
}

red::String PostgresBatchWorker::CombineQueries( const red::DynArray< EntryPtr >& entries ) const
{
	Uint32 stringSize = std::accumulate( entries.Begin(), entries.End(), 0,
		[]( Uint32 total, const PostgresBatchWorker::EntryPtr& entry ) -> Uint32
	{
		return total + entry->m_query.Length();
	} );

	red::String result;
	result.Reserve( stringSize );
	for ( const auto& entry : entries )
	{
		result += entry->m_query;
	}

	return result;
}

bool PostgresBatchWorker::ProcessResult( postgres::Result& result, dependency::OutputFileList& outputList ) const
{
	if ( !result )
	{
		RED_LOG_ERROR( "PostgreSQLDepsServer: Failed to get a result from the server when one was expected, error: '%hs'", result.GetStatusText() );
		return false;
	}

	const Int32 rows = result.Rows();
	const Int32 columns = result.Columns();
	RED_FATAL_ASSERT( columns == 5, "Expected to get 5 columns back in this query, got %u!", columns );

	outputList.Reserve( outputList.Size() + rows );

	const Int32 pathColumn = result.ColumnIndexOf( "path" );
	const Int32 pathHashColumn = result.ColumnIndexOf( "path_hash" );
	const Int32 timestampColumn = result.ColumnIndexOf( "timestamp" );
	const Int32 hashColumn = result.ColumnIndexOf( "content_hash" );
	const Int32 depotColumn = result.ColumnIndexOf( "depot" );

	for ( Int32 row = 0; row < rows; ++row )
	{
		RED_FATAL_ASSERT( result.GetValueLength( row, pathColumn ) <= res::ResourcePath::MAX_LENGTH, "Resource path is too long in the database!" );
		Uint64 pathHash = 0ull;
		red::SScanf( result.GetValue( row, pathHashColumn ), "%llX", &pathHash );

		dependency::OutputFileInfo fileInfo;
		fileInfo.path = res::ResourcePath::Build( result.GetValue( row, pathColumn ) );
		RED_FATAL_ASSERT( pathHash == fileInfo.path.GetHash(), "Resource path hash computed locally does not match path hash stored on the server. %.16llX != %.16llx", pathHash, fileInfo.path.GetHash() );

		const char* timestampStr = result.GetValue( row, timestampColumn );
		RED_VERIFY( GParseInteger( timestampStr, fileInfo.timestamp ) );

		Uint32 depotInt = 0;
		const char* depotStr = result.GetValue( row, depotColumn );
		RED_VERIFY( GParseInteger( depotStr, depotInt ) );
		RED_FATAL_ASSERT( depotInt <= static_cast< Uint32 >( data::DepotLocation::Output ), "Invalid depot id stored in the database" );
		fileInfo.depot = static_cast< data::DepotLocation >( depotInt );

		Uint32 hashLength = result.GetValueLength( row, hashColumn );
		const char* hashStr = result.GetValue( row, hashColumn );
		fileInfo.hash = dependency::Hash::FromEncodedString( red::StringView( hashStr, hashLength ) );

		outputList.PushBack( fileInfo );
	}

	return true;
}

//------------------------------------------------------------------------------

PostgresDepsServer::PostgresDepsServer()
{
}

PostgresDepsServer::~PostgresDepsServer()
{
}

bool PostgresDepsServer::Initialise( const red::String& params, job::Counter& counter )
{
	job::Counter noWait;
	job::DispatchJob< red::PoolBackend >( "PostgreSQL/Connect", noWait, counter, [this, params]( const job::RunContext& ) 
	{
		const red::String serverName = ParamString_GetServerName( params );

		m_worker = red::CreateUniquePtr< PostgresBatchWorker >();
		if ( m_worker && m_worker->Initialise( params ) )
		{
			RED_LOG_INFO( "PostgreSQLDepsServer: Successfully connected to %hs", serverName.AsChar() );
		}
		else
		{
			RED_LOG_ERROR( "PostgreSQLDepsServer: Failed to initialise connection to %hs", serverName.AsChar() );
			m_worker = nullptr;
		}
	} );
	return true;
}

void PostgresDepsServer::Shutdown()
{
	if ( m_worker )
	{
		m_worker->Shutdown();
		m_worker = nullptr;
	}
}

bool PostgresDepsServer::IsInitialised() const
{
	return m_worker && m_worker->IsInitialised();
}

Cache::Result PostgresDepsServer::GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder )
{
	auto prereqHashStr = dependency::Hash::ToEncodedString( prereqHash );
	auto query = String::Printf( "SELECT path, path_hash, timestamp, content_hash, depot FROM build_resources WHERE prerequisite_hash = '%hs';", prereqHashStr.AsChar() );

	return m_worker->QueueQuery( std::move( query ), outputList, builder ); 
}

Cache::Result PostgresDepsServer::GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder )
{
	auto subquery = String::Printf( "SELECT prerequisite_hash FROM build_resources WHERE path_hash = '%.16llX' ORDER BY timestamp DESC LIMIT 1", path.GetHash() );
	auto query = String::Printf( "SELECT path, path_hash, timestamp, content_hash, depot FROM build_resources WHERE prerequisite_hash = (%hs);", subquery.AsChar() );

	return m_worker->QueueQuery( std::move( query ), outputList, builder );
}

//------------------------------------------------------------------------------

} // prv
} // data
