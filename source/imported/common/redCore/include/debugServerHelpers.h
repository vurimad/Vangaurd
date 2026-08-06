/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

class REDCORE_API DebugServerHelpers
{
public:

	static void ConnectToEditor();
	static void ConnectToGame();
	static const char* GetChannelName();
};

} // red