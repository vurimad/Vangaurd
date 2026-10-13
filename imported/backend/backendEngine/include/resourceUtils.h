/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace res
{
class ResourcePath;
}

// TODO: Move these into a res or resource namespace

// NOTE: These are not general "resource utilities" but instead only functions that deal with resource paths and naming

// Simple test using file extensions to detect if the path is not a resource
// NOTE: This test is not exhaustive but just a simple filter
BACKEND_ENGINE_API bool IsNotResource( const res::ResourcePath& path );

// Simple test using file extensions to detect if the path could be a resource
BACKEND_ENGINE_API bool CouldBeResource( const res::ResourcePath& path );

// Tests if the resource path is a known generated resource
BACKEND_ENGINE_API bool IsGeneratedResource( const res::ResourcePath& path );

// Tests if the path could be a binary resource
BACKEND_ENGINE_API bool IsBinaryResource( const res::ResourcePath& path );

// Tests if the path can be opened up as a binary resource and scanned
BACKEND_ENGINE_API bool CanBinaryScanResource( const res::ResourcePath& path );

// Tests is the path is a known text resource
BACKEND_ENGINE_API bool IsTextResource( const res::ResourcePath& path );

// Tests is the path is a known text resource
BACKEND_ENGINE_API bool IsCsvResource( const res::ResourcePath& path );

// Tests if the path is a known json resource
BACKEND_ENGINE_API bool IsJsonResource( const res::ResourcePath& path );

// Tests if the path is a known BINK resource
BACKEND_ENGINE_API bool IsBinkResource( const res::ResourcePath& path );

// Tests if the path is a known ICU resource
BACKEND_ENGINE_API bool IsICUResource( const res::ResourcePath& path );

// Tests if the path is a known WISE/OPUS Audio resource
BACKEND_ENGINE_API bool IsAudioResource( const res::ResourcePath& path );

// Tests if the path is json but is not a resource
BACKEND_ENGINE_API bool IsJsonNotResource( const res::ResourcePath& path );

// Tests if the path is a known resource in the engine
BACKEND_ENGINE_API bool IsResource( const res::ResourcePath& path );

// Tests if the path is a link
BACKEND_ENGINE_API bool IsLinkResource( const res::ResourcePath& path );

// Tests if the path is not a server resource and should be excluded
BACKEND_ENGINE_API bool IsNotServerResource( const res::ResourcePath& path );

// Computes a path to a link file given the incoming resource path
// If the resource path is already a link file it will return it
BACKEND_ENGINE_API res::ResourcePath ComputeLinkResourcePath( const res::ResourcePath& path );

// Computes a path to a non-link file given the incoming resource path
// If the resource path is a link file then it will remove the .link, otherwise
// it will return the original path
BACKEND_ENGINE_API res::ResourcePath ComputeNonLinkResourcePath( const res::ResourcePath& path );
