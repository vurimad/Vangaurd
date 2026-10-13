/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../include/serializationLoader.h"

namespace serialization
{

class FileTables;

// loader of serialized data from a binary format
class BinaryLoader : public ILoader
{
public:
	BinaryLoader();
	virtual ~BinaryLoader();

	// Create a job chain loading the data from the file
	virtual void LoadAsyncWithCallback( const AsyncSourcePtr& asyncSource, const LoadingContext& context, const CallbackContext& callbackContext, job::CompletionDeferral&& deferral ) const override final;

	virtual Bool UseInCookedGame() const override;

	// Inline loading from a memory buffer directly
	// NOTE: pro users only, this may cause dead locks
	Bool LoadFromMemory( const void* memory, const Uint32 size, const LoadingContext& context, LoadingResult& resultPtr, Bool doActiveWaiting );

private:
	// validate data tables (CRC)
	static const Bool ValidateTables( const Uint64 baseOffset, IFile& file, const FileTables& fileTables );
};

} // serialization
