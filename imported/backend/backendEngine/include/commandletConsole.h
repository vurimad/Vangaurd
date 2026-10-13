/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/redContainers/include/string/stringView.h"

namespace commandlet
{

class BACKEND_ENGINE_API InteractiveConsole
{
public:
	InteractiveConsole();
	~InteractiveConsole();

	bool Initialise();
	void Shutdown();

	red::StringView GetInput();

	void PrintOutput( red::StringView str );
    void Clear();

	void StartRedirect( const red::StringView& redirectFilename );
	void EndRedirect();

private:
	HANDLE m_inputHandle;
	HANDLE m_outputHandle;
	DWORD m_originalInputMode;
	DWORD m_originalOutputMode;

	struct RedirectInfo;
	red::UniquePtr< RedirectInfo > m_redirect;

	static constexpr Uint32 m_inputBufferSize = 4096;
	char m_inputBuffer[ m_inputBufferSize ];
};

} // commandlet
