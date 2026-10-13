/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace script
{
	namespace expression
	{
		struct ReadOnlyToken
		{
			red::StringView text;
			Uint32 id;

			void Set( const ReadOnlyToken& token )
			{
				text = token.text;
				id = token.id;
			}
		};

		class ErrorListener
		{
		public:
			ErrorListener()
				: m_hasError( false )
			{}

			Bool HasError() const { return m_hasError; }

			void SetError() { m_hasError = true; }

		private:
			Bool m_hasError;
		};
	}
}
