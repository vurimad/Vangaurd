/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace res
{
class IBackendResourceDepot;
} // res

namespace commandlet
{

class BACKEND_DATA_API FileLinkMap
{
public:
	enum class ErrorType
	{
		GENERAL_ERROR,
		BOTH_RES_AND_LINK_EXIST,
		CIRCULAR_LINK_CHAIN,
		BROKEN_LINK,
		INVALID_PATH,
		LINK_TO_LINK,
		MAX_LINK_CHAIN,
		MISSING_TARGET,
	};

	struct ErrorEntry
	{
		res::ResourcePath linkPath;			// Link ResourcePath that is at error
		res::ResourcePath originalPath;		// Original resource that was being looked up
		ErrorType type;
		red::DynArray< res::ResourcePath > previousPaths;	// Previous resource paths that led to this error
	};

	FileLinkMap();
	FileLinkMap( const FileLinkMap& ) = delete;
	FileLinkMap( FileLinkMap&& other );
	~FileLinkMap();

	FileLinkMap& operator=( const FileLinkMap& ) = delete;
	FileLinkMap& operator=( FileLinkMap&& other );

	bool Initialise( res::IBackendResourceDepot& depot );
	bool Initialise( res::IBackendResourceDepot& depot, red::DynArray< res::ResourcePath >& allResources);

	RED_INLINE red::DynArray< res::ResourcePath >& Resources() { return m_resources; }
	RED_INLINE const red::DynArray< res::ResourcePath >& Resources() const { return m_resources; }

	RED_INLINE red::DynArray< res::ResourcePath >& Links() { return m_links; }
	RED_INLINE const red::DynArray< res::ResourcePath >& Links() const { return m_links; }

	RED_INLINE red::HashMap< res::ResourcePath, res::ResourcePath >& Mapping() { return m_linkFileMap; }
	RED_INLINE const red::HashMap< res::ResourcePath, res::ResourcePath >& Mapping() const { return m_linkFileMap; }

	RED_INLINE red::DynArray< ErrorEntry >& Errors() { return m_errorResources; }
	RED_INLINE const red::DynArray< ErrorEntry >& Errors() const { return m_errorResources; }

	RED_INLINE red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > >& ReverseMapping() { return m_resourceLinkMap; }
	RED_INLINE const red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > >& ReverseMapping() const { return m_resourceLinkMap; }

	RED_INLINE red::DynArray< res::ResourcePath > MissingResources() const { return m_missingResourcesMap.GetKeys(); }

	RED_INLINE red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > >& MissingResourcesMap() { return m_missingResourcesMap; }
	RED_INLINE const red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > >& MissingResourcesMap() const { return m_missingResourcesMap; }

private:
	void InitialiseFileLists( red::DynArray< res::ResourcePath >& allResources );
	void ResolveLinkFile( const res::ResourcePath& path );
	void ResolveSingleLink( const res::ResourcePath& linkPath, const res::ResourcePath& originalPath, const res::ResourcePath& originalLinkPath, red::DynArray< res::ResourcePath >& previousPaths );

	res::IBackendResourceDepot* m_depot;

	// List of paths which are valid resources, sorted by ResourcePath hash
	red::DynArray< res::ResourcePath > m_resources;

	// List of paths which are known link files, extension is "link", sorted by ResourcePath hash
	red::DynArray< res::ResourcePath > m_links;

	// Resolved input resource path to final followed non-link file
	// Maps resource path without ".link" to final followed resource file
	red::HashMap< res::ResourcePath, res::ResourcePath > m_linkFileMap;

	// Maps pointed to resources to all of the links that point to it
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > m_resourceLinkMap;

	// Maps missing resource to the links that point to it
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > m_missingResourcesMap;

	// List of links with errors
	red::DynArray< ErrorEntry > m_errorResources;
};


} // commandlet