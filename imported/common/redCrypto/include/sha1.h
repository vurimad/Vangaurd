/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace crypto
{
	struct Ssha1
	{
		static constexpr Uint32 NUM_BYTES = 20;
		red::Uint8 m_value[ NUM_BYTES ];
	};

	// calcualte SHA-1 hash from a given content
	// NOTE: **** DO NOT USE FOR ANYTHING REQUIRING REAL SECURITY!!! IT IS INSCECURE BY TODAY'S STANDARDS ***
	// SHA-1 is provided for systems that still require it for non-security related purposes. E.g., WebSockets server handshake.
	extern REDCRYPTO_API void CalculateSha1_Insecure( const void* buffer, const red::Uint64 size, Ssha1& outHash );

	// Simple class to maintain the state of the SHA1 computation so it can be done in blocks
	class REDCRYPTO_API Sha1Computer
	{
	public:
		Sha1Computer();

		void PushData( const void* buffer, const red::Uint64 size );
		Ssha1 Finalise();

		Sha1Computer( const Sha1Computer& ) = delete;
		Sha1Computer( Sha1Computer&& ) = delete;
		Sha1Computer& operator=( const Sha1Computer& ) = delete;
		Sha1Computer& operator=( Sha1Computer&& ) = delete;

	private:
		Uint8 m_contextBuffer[92];
	};

} // crypto