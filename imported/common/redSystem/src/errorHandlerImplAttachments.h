/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace dbgutils
{
	struct RegisteredAttachmentTable;
}

namespace red { namespace err
{

template< typename TChar >
void RegisterAttachment( dbgutils::RegisteredAttachmentTable& table, const TChar* pathToRegister );

namespace hacks
{
#ifdef RED_PLATFORM_DURANGO
	void DurangoUnregisterFilesNonRecursive( const wchar_t* registeredFilesDir );
#endif
}

} }