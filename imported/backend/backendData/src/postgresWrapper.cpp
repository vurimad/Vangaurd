/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "postgresWrapper.h"


namespace data
{
namespace postgres
{

//------------------------------------------------------------------------------

Result::Result()
	: m_result( nullptr )
{}

Result::Result( PGresult* result )
	: m_result( result )
{}

Result::~Result()
{
	if ( m_result )
	{
		PQclear( m_result );
		m_result = nullptr;
	}
}

Result::Result( Result&& other )
	: m_result( other.m_result )
{
	other.m_result = nullptr;
}

Result& Result::operator=( Result&& other )
{
	if ( this != &other )
	{
		m_result = other.m_result;
		other.m_result = nullptr;
	}
	return *this;
}

bool Result::IsNull() const
{
	return m_result == nullptr;
}

Result::operator bool() const
{
	if ( m_result != nullptr )
	{
		auto status = PQresultStatus( m_result );
		return status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK;
	}
	return false;
}

const char* Result::GetStatusText() const
{
	return PQresStatus( PQresultStatus( m_result ) );
}

PGresult* Result::GetResult() const
{
	return m_result;
}

Int32 Result::Rows() const
{
	return PQntuples( m_result );
}

Int32 Result::Columns() const
{
	return PQnfields( m_result );
}

Int32 Result::ColumnIndexOf( const char* title ) const
{
	return PQfnumber( m_result, title );
}

const char* Result::GetValue( Int32 row, Int32 column ) const
{
	return PQgetvalue( m_result, row, column );
}

Uint32 Result::GetValueLength( Int32 row, Int32 column ) const
{
	return PQgetlength( m_result, row, column );
}

//------------------------------------------------------------------------------

Connection::Connection()
	: m_connection( nullptr )
{}

Connection::Connection( PGconn* connection )
	: m_connection( connection )
{}

Connection::~Connection()
{
	Disconnect();
}

Connection::Connection( Connection&& other )
	: m_connection( other.m_connection )
{
	other.m_connection = nullptr;
}

Connection& Connection::operator=( Connection&& other )
{
	if ( this != &other )
	{
		m_connection = other.m_connection;
		other.m_connection = nullptr;
	}
	return *this;
}

Connection::operator bool() const
{
	return m_connection != nullptr;
}

Result Connection::ExecuteQuery( const red::String& query )
{
	RED_FATAL_ASSERT( m_connection != nullptr, "PostgresConnection: Connection must be valid before attempting to use it" );
	Result result{ PQexec( m_connection, query.AsChar() ) };
	if ( !result )
	{
		RED_LOG_ERROR( "PostgresConnection: Error '%hs' when exectuting query '%hs'", result.GetStatusText(), query.AsChar() );
	}
	return result;
}

bool Connection::SendQuery( const red::String& query )
{
	RED_FATAL_ASSERT( m_connection != nullptr, "PostgresConnection: Connection must be valid before attempting to use it" );
	if ( PQsendQuery( m_connection, query.AsChar() ) == 0 )
	{
		RED_LOG_ERROR( "PostgresConnection: Error '%hs' when sending query '%hs'", PQerrorMessage( m_connection ), query.AsChar() );
		return false;
	}
	return true;
}

Result Connection::GetResult()
{
	RED_FATAL_ASSERT( m_connection != nullptr, "PostgresConnection: Connection must be valid before attempting to use it" );
	return Result( PQgetResult( m_connection ) );
}

PGconn* Connection::Get() const
{
	RED_FATAL_ASSERT( m_connection != nullptr, "PostgresConnection: Connection must be valid before attempting to use it" );
	return m_connection;
}

void Connection::Disconnect()
{
	if ( m_connection )
	{
		PQfinish( m_connection );
		m_connection = nullptr;
	}
}

Connection Connection::Connect( const String& connectionString )
{
	auto connection = PQconnectdb( connectionString.AsChar() );
	auto connStatus = PQstatus( connection );
	if ( connStatus != CONNECTION_OK )
	{
		RED_LOG_ERROR( "Failed to connect to '%hs'", connectionString.AsChar() );
		return Connection();
	}

	return Connection( connection );
}

//------------------------------------------------------------------------------

} // postgres
} // data
