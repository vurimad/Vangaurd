/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace script
{
	namespace expression
	{
		// DLL-Exported for unit tests
		class RED_REFLECTION_API Parser
		{
		public:
			Parser();
			~Parser();

			Bool Parse( const AnsiChar* expression, class Path& path );
		};
	}
}
