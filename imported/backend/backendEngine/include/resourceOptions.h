/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redCore/include/commandline.h"

class BACKEND_ENGINE_API ResourceCommandOptions : public ISerializable
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	RTTI_DECLARE_TYPE( ResourceCommandOptions );

public:
	ResourceCommandOptions();
	virtual ~ResourceCommandOptions();

	virtual Bool ParseFromCommandLine( const red::CommandLine& stringOptions );

	virtual Bool NeedOptions() const;
};

class BACKEND_ENGINE_API ResourceCommandResult
{
public:
	using ResourcesList = red::DynArray< THandle< CResource > >;

	ResourceCommandResult();
	explicit ResourceCommandResult( const ResourcesList& resources );
	explicit ResourceCommandResult( const String& errorMessage );
	~ResourceCommandResult();

	Bool IsSuccessful() const;
	Bool HasFailed() const;
	Bool HasMessage() const;
	const String& GetErrorMessage() const;
	const String& GetSuccessMessage() const;
	const ResourcesList& GetResources() const;

	static ResourceCommandResult Success( const THandle< CResource >& resource );
	static ResourceCommandResult Success( const ResourcesList& resources );
	static ResourceCommandResult Success( const THandle< CResource >& resource, const String& str );
	static ResourceCommandResult Success( const ResourcesList& resources, const String& str );
	static ResourceCommandResult Error( const char* txt, ... );
	static ResourceCommandResult Error( const String& str );

private:
	ResourcesList m_resources;
	String m_message;
	Bool m_success;
};
