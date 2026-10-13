/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{

	class SelectionState;
	using SelectionStatePtr = red::SharedPtr< SelectionState >;

	class SelectionIterator;

	class ISelectionHandler;
	class SelectionHandler;

} // tools