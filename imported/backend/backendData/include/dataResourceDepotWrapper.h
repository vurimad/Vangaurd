/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redReflection/include/serializationAsyncSource.h"

namespace data
{

// Simple default wrapper class for a Backend Resource Depot so that simpler and more specific wrappers
// can be written by deriving from this class. This maintains the default calls to the wrapped depot reference
// and derived classes can just intercept the ones they need to.
class ResourceDepotWrapper : public res::IBackendResourceDepot
{
public:
	explicit ResourceDepotWrapper( res::IBackendResourceDepot& depot );

	virtual red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params ) override;

	virtual red::SharedPtr<IFile> CreateResourceReader( const res::ResourcePath resourcePath, bool followLinks ) override;
	virtual red::SharedPtr<IFile> CreateResourceWriter( const res::ResourcePath resourcePath ) override;

	virtual red::AbsolutePath TranslateResourcePath( const res::ResourcePath resourcePath ) const override;
	virtual res::ResourcePath TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const override;

	virtual bool ResourceIsLink( const res::ResourcePath resourcePath ) const override;
	virtual void FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const override;
	virtual void FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const override;

	virtual Bool RemoveResource( const res::ResourcePath resourcePath ) override;

	virtual bool ResourceExists( const res::ResourcePath resourcePath ) const override;
	virtual bool ResourceIsReadOnly( const res::ResourcePath resourcePath ) const override;
	virtual Uint64 GetResourceTimestamp( const res::ResourcePath resourcePath ) const override;

	virtual Uint32 Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const override;
	virtual Uint32 Debug_GetResourceObjectSizeOnDisk(const res::ResourcePath resourcePath) const override;

	virtual res::ResourcePath ResolveResourcePath( const res::ResourcePath resourcePath ) const override;

	virtual const red::DynArray< red::String >& GetDLCRootPaths() const override;
	virtual void SetDLCRootPaths( const red::DynArray< String >& rootPaths ) override;
	virtual void ScanForDLCRootPaths() override;

	// NOTE: We are not wrapping the language or archive functions here because we don't expect them to be used in the backend.
	// Add them if needed though

protected:
	virtual const char* GetDebugName() const override;

	res::IBackendResourceDepot& m_depot;
};

RED_INLINE ResourceDepotWrapper::ResourceDepotWrapper( res::IBackendResourceDepot& depot )
	: m_depot( depot )
{
}

RED_INLINE red::SharedPtr<serialization::IAsyncSource> ResourceDepotWrapper::CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params )
{
	return m_depot.CreateResourceAsyncSource( resourcePath, params );
}

RED_INLINE red::SharedPtr<IFile> ResourceDepotWrapper::CreateResourceReader( const res::ResourcePath resourcePath, bool followLinks )
{
	return m_depot.CreateResourceReader( resourcePath, followLinks );
}

RED_INLINE red::SharedPtr<IFile> ResourceDepotWrapper::CreateResourceWriter( const res::ResourcePath resourcePath )
{
	return m_depot.CreateResourceWriter( resourcePath );
}

RED_INLINE red::AbsolutePath ResourceDepotWrapper::TranslateResourcePath( const res::ResourcePath resourcePath ) const
{
	return m_depot.TranslateResourcePath( resourcePath );
}

RED_INLINE res::ResourcePath ResourceDepotWrapper::TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const
{
	return m_depot.TranslateAbsolutePath( absolutePath );
}

RED_INLINE bool ResourceDepotWrapper::ResourceIsLink( const res::ResourcePath resourcePath ) const
{
	return m_depot.ResourceIsLink( resourcePath );
}

RED_INLINE void ResourceDepotWrapper::FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const
{
	m_depot.FindDepotSubDirectories( searchDepotPath, outDirectoryNames );
}

RED_INLINE void ResourceDepotWrapper::FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const
{
	m_depot.FindDepotFiles( searchDepotPath, pattern, outFileNames );
}

RED_INLINE Bool ResourceDepotWrapper::RemoveResource( const res::ResourcePath resourcePath )
{
	return m_depot.RemoveResource( resourcePath );
}

RED_INLINE bool ResourceDepotWrapper::ResourceExists( const res::ResourcePath resourcePath ) const
{
	return m_depot.ResourceExists( resourcePath );
}

RED_INLINE bool ResourceDepotWrapper::ResourceIsReadOnly( const res::ResourcePath resourcePath ) const
{
	return m_depot.ResourceIsReadOnly( resourcePath );
}

RED_INLINE Uint64 ResourceDepotWrapper::GetResourceTimestamp( const res::ResourcePath resourcePath ) const
{
	return m_depot.GetResourceTimestamp( resourcePath );
}

RED_INLINE Uint32 ResourceDepotWrapper::Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const
{
	return m_depot.Debug_GetResourceTotalSizeOnDisk( resourcePath );
}

RED_INLINE Uint32 ResourceDepotWrapper::Debug_GetResourceObjectSizeOnDisk( const res::ResourcePath resourcePath ) const
{
	return m_depot.Debug_GetResourceObjectSizeOnDisk( resourcePath );
}

RED_INLINE const red::DynArray< red::String >& ResourceDepotWrapper::GetDLCRootPaths() const
{
	return m_depot.GetDLCRootPaths();
}

RED_INLINE void ResourceDepotWrapper::SetDLCRootPaths( const red::DynArray< String >& rootPaths )
{
	m_depot.SetDLCRootPaths( rootPaths );
}

RED_INLINE void ResourceDepotWrapper::ScanForDLCRootPaths()
{
	m_depot.ScanForDLCRootPaths();
}

RED_INLINE res::ResourcePath ResourceDepotWrapper::ResolveResourcePath( const res::ResourcePath resourcePath ) const
{
	return m_depot.ResolveResourcePath( resourcePath );
}

RED_INLINE const char* ResourceDepotWrapper::GetDebugName() const
{
	return m_depot.GetDebugName();
}

} // namespace dependency
