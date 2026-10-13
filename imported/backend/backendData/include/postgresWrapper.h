/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../external/pgsql/include/libpq-fe.h"

namespace data
{
namespace postgres
{

//------------------------------------------------------------------------------

// Simple wrapper around the PostgreSQL C API result
class BACKEND_DATA_API Result
{
public:
	Result();
	explicit Result( PGresult* result );
	~Result();

	Result( const Result& ) = delete;
	Result( Result&& other );

	Result& operator=( const Result& ) = delete;
	Result& operator=( Result&& other );

	bool IsNull() const;

	explicit operator bool() const;

	const char* GetStatusText() const;

	PGresult* GetResult() const;

	Int32 Rows() const;
	Int32 Columns() const;

	Int32 ColumnIndexOf( const char* title ) const;

	const char* GetValue( Int32 row, Int32 column ) const;
	Uint32 GetValueLength( Int32 row, Int32 column ) const;

private:
	PGresult* m_result;
};

//------------------------------------------------------------------------------

// Simple wrapper around the PostgreSQL C API connection
// This can be used to execute queries on, although it is a very light wrapper so
// not everything is abstracted away. Read the notes below to find out the requirements of use
class BACKEND_DATA_API Connection
{
public:
	Connection();
	explicit Connection( PGconn* connection );
	~Connection();

	Connection( const Connection& ) = delete;
	Connection( Connection&& other );

	Connection& operator=( const Connection& ) = delete;
	Connection& operator=( Connection&& other );

	explicit operator bool () const;

	// Immediately executes a query on the connection and wait for the result to be returned
	// NOTE: Only returns a single result no matter how many queries are submitted
	// NOTE: Cannot be used interleaved with SendQuery, calls PQexec
	Result ExecuteQuery( const red::String& query );

	// Sends a query on the connection, need to call blocking GetResult function to get results
	// NOTE: Keep calling the GetResult function until no more results are returned
	// NOTE: Cannot be used interleaved with ExecuteQuery, calls PQsendQuery
	bool SendQuery( const red::String& query );

	// Gets a single set of results
	Result GetResult();

	PGconn* Get() const;

	void Disconnect();

	static Connection Connect( const String& connectionString );

private:
	PGconn* m_connection;
};

//------------------------------------------------------------------------------

} // postgres
} // data
