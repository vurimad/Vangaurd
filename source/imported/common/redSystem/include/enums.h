/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

// Temporary namespace
namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// Common enums definitions

	/// describes a status of "async" state operation, mostly state initialization and shutdown
	enum class AsyncStatus : Uint8
	{
		NotStarted,

		// operation is not finished but is progress and everything is going fine
		NotDone,

		// operation has finished with errors
		Error,

		// operation has finished without errors
		Done,
	};
}
