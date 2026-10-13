/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#ifdef RED_PLATFORM_DURANGO

namespace res
{
namespace prv
{

class ArchiveLanguageMap;

red::DynArray< red::String > GetInstalledLanguages( prv::ArchiveLanguageMap &languageMap );
Bool RefreshInstalledLanguages( prv::ArchiveLanguageMap &languageMap );

} // prv
} // res

#endif // RED_PLATFORM_DURANGO