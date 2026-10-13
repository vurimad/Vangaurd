/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/


#include "build.h"

#include "resourceOptions.h"

RTTI_BEGIN_ABSTRACT_TYPE( ResourceCommandOptions );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

ResourceCommandOptions::ResourceCommandOptions()
{
}

ResourceCommandOptions::~ResourceCommandOptions()
{
}

Bool ResourceCommandOptions::ParseFromCommandLine( const red::CommandLine& stringOptions )
{
	return true;
}

Bool ResourceCommandOptions::NeedOptions() const
{
	return false;
}

ResourceCommandResult::ResourceCommandResult()
	: m_success( false )
{
}

ResourceCommandResult::ResourceCommandResult( const ResourcesList& resources )
	: m_resources( resources )
	, m_success( true )
{}

ResourceCommandResult::ResourceCommandResult( const String& errorMessage )
	: m_message( errorMessage )
	, m_success( false )
{}

ResourceCommandResult::~ResourceCommandResult()
{
}

Bool ResourceCommandResult::IsSuccessful() const
{
	return m_success;
}

Bool ResourceCommandResult::HasFailed() const
{
	return !m_success;
}

Bool ResourceCommandResult::HasMessage() const
{
	return !m_message.Empty();
}

const String& ResourceCommandResult::GetErrorMessage() const
{
	RED_FATAL_ASSERT( HasFailed(), "Getting the error message when resource command was successful!" );
	return m_message;
}

const String& ResourceCommandResult::GetSuccessMessage() const
{
	RED_FATAL_ASSERT( IsSuccessful(), "Getting the success message when resource command has failed!" );
	return m_message;
}

const red::DynArray< THandle< CResource > >& ResourceCommandResult::GetResources() const
{
	RED_FATAL_ASSERT( IsSuccessful(), "Getting the resource when the resource command has failed" );
	return m_resources;
}

ResourceCommandResult ResourceCommandResult::Success( const THandle< CResource >& resource )
{
	ResourcesList resources;
	resources.PushBack( resource );
	return ResourceCommandResult( resources );
}

ResourceCommandResult ResourceCommandResult::Success( const ResourcesList& resources )
{
	return ResourceCommandResult( resources );
}

ResourceCommandResult ResourceCommandResult::Success( const THandle< CResource >& resource, const String& str )
{
	auto res = Success( resource );
	res.m_message = str;
	return res;
}

ResourceCommandResult ResourceCommandResult::Success( const ResourcesList& resources, const String& str )
{
	auto res = Success( resources );
	res.m_message = str;
	return res;
}

ResourceCommandResult ResourceCommandResult::Error( const char* txt, ... )
{
	va_list args;
	va_start( args, txt );
	auto message = String::PrintfV(txt, args);
	va_end( args );

	return ResourceCommandResult( message );
}

ResourceCommandResult ResourceCommandResult::Error( const String& str )
{
	return ResourceCommandResult( str );
}