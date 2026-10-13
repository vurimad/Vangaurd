/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/* public api for steve reid's public domain SHA-1 implementation */
/* this file is in the public domain */

namespace crypto
{
	namespace sha1
	{
		typedef struct
		{
			Uint32 state[ 5 ];
			Uint32 count[ 2 ];
			Uint8  buffer[ 64 ];
		} SHA1_CTX;
	
		#define SHA1_DIGEST_SIZE 20
	
		void SHA1_Init( SHA1_CTX* context );
		void SHA1_Update( SHA1_CTX* context, const Uint8* data, const size_t len );
		void SHA1_Final( SHA1_CTX* context, Uint8 digest[ SHA1_DIGEST_SIZE ] );
	
	} // sha1
} // crypto