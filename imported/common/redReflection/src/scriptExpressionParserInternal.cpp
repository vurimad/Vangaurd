/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptExpressionParserInternal.h"
#include "scriptExpressionParserPath.h"

namespace script { namespace expression
{
	int parsertime( ReadOnlyToken& lvalp, TokenStream< ReadOnlyToken >* stream )
	{
		if ( stream->End() )
			return 0;

		lvalp.Set( stream->GetToken() );

		stream->Next();

		return lvalp.id;
	}

	void errortime( const ReadOnlyToken& lvalp, const TokenStream< ReadOnlyToken >* stream, const Path* path, ErrorListener* errorListener, const char* msg )
	{
		errorListener->SetError();
	}

} } // namespace script { namespace expression
