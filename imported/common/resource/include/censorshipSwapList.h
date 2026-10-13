/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once


namespace res
{
// Returns the path of the censorship swap list
RED_RESOURCE_API res::ResourcePath GetCensorshipSwapListPath();

// Loads the censorship swap list, filters the entries with the current flags in the censorship system
// and then applies them to the global resource loader
RED_RESOURCE_API void UpdateCensorshipSwapListToResourceLoader();

} // res
