/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// modes defining how render proxy should look like in isolation mode
enum RenderProxyIsolationMode
{
	RPIM_Visible = 0,		// proxy is visible but can't be selected
	RPIM_Checkerboard = 1,	// proxy is has checkerboard fade effect
	RPIM_Hidden = 2,		// proxy is invisible
};
