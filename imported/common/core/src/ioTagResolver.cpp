/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "ioTagResolver.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/xmlNewReader.h"

using red::HashMap;

CFileSystemPriorityResovler GFileSysPriorityResovler;

CFileSystemPriorityResovler::CFileSystemPriorityResovler()
	: m_context( eIOContext_Boot )
	, m_initialized( false )
{
}

Bool CFileSystemPriorityResovler::InitializePriorityTables()
{
	// load the XML file
	const red::AbsolutePath xmlPath = GFileManager->GetEngineRoot().AddFilePath( "init\\io_priority_table.xml" );
	auto xml = xml::Reader::Create( GFileManager->CreateFileReader( xmlPath, FOF_MapToMemory ) );
	if ( !xml )
	{
		RED_LOG_ERROR( "Core: Unable to parse IO priority table. Build data is corrupted." );
		return false;
	}

	// build name->tag mapping
	HashMap< String, EIOTag > tagNames{ red::PoolEngine() };
	tagNames.Reserve(eIOTag_MAX);
	for ( Uint32 i=0; i<eIOTag_MAX; ++i )
	{
		const AnsiChar* name = Helper::GetIOTagName( (EIOTag) i );
		tagNames[ name ] = (EIOTag) i;
	}

	// build name->context mapping
	HashMap< String, EIOContext > contextNames{ red::PoolEngine() };
	contextNames.Reserve(eIOContext_MAX);
	for ( Uint32 i=0; i<eIOContext_MAX; ++i )
	{
		const AnsiChar* name = Helper::GetIOContextName( (EIOContext) i );
		contextNames[ name ] = (EIOContext) i;
	}

	// priority names
	HashMap< String, Int8 > priorityNames{ red::PoolEngine() };
	priorityNames[ "normal" ] = io::eAsyncPriority_Normal;
	priorityNames[ "low" ] = io::eAsyncPriority_Background;
	priorityNames[ "high" ] = io::eAsyncPriority_High;
	priorityNames[ "veryhigh" ] = io::eAsyncPriority_High;
	priorityNames[ "critical" ] = io::eAsyncPriority_High;

	// reset memory
	red::Memset( &m_tables, 0xCC, sizeof(m_tables) );

	// process file
	if ( xml->Open( "tables" ) )
	{
		while ( xml->Open( "context" ) )
		{
			String name;
			if ( !xml->PeekAttribute( "name", name ) )
			{
				RED_LOG_ERROR( "Core: Missing context name" );
				return false;
			}

			// find context ID
			EIOContext contextID = eIOContext_MAX;
			if ( !contextNames.Find( name.AsChar(), contextID ) )
			{
				RED_LOG_ERROR( "Core: Unknown context name '%hs'", name.AsChar() );
				return false;
			}

			// start loading groups
			while ( xml->Open( "tag" ) )
			{
				String tagName;
				if ( !xml->PeekAttribute( "name", tagName ) )
				{
					RED_LOG_ERROR( "Core: Missing tag name in context '%hs'", name.AsChar() );
					return false;
				}

				// find tag ID
				EIOTag tagID = eIOTag_MAX;
				if ( !tagNames.Find( tagName.AsChar(), tagID ) )
				{
					RED_LOG_ERROR( "Core: Unknown tag name '%hs'", tagName.AsChar() );
					return false;
				}

				// already set ?
				if ( m_tables[ contextID ][ tagID ] != 0xCC )
				{
					RED_LOG_ERROR( "Core: Priority value for tag '%hs' in context '%hs' is already set", 
						tagName.AsChar(), name.AsChar() );
					return false;
				}

				// get priority value
				String priorityName;
				if ( !xml->PeekAttribute( "priority", priorityName ) )
				{
					RED_LOG_ERROR( "Core: Missing priority value for tag '%hs' in context '%hs'", 
						tagName.AsChar(), name.AsChar() );
					return false;
				}

				// resolve priority name
				Int8 priority = -1;
				if ( !priorityNames.Find( priorityName, priority ) )
				{
					RED_LOG_ERROR( "Core: Unknown priority name '%hs'", priorityName.AsChar() );
					return false;
				}


				// set priority value
				m_tables[ contextID ][ tagID ] = priority;

				// close current node, go to next one
				xml->Close();
			}

			// close current node, go to next one
			xml->Close();
		}
	}

	// fill missing values
	for ( Uint32 i=0; i<eIOContext_MAX; ++i )
	{
		for ( Uint32 j=0; j<eIOTag_MAX; ++j )
		{
			if ( m_tables[i][j] == 0xCC )
			{
				const AnsiChar* contextName = Helper::GetIOContextName( (EIOContext) i );
				const AnsiChar* tagName = Helper::GetIOContextName( (EIOContext) i );

				RED_LOG_WARNING( "Core: Loading priority for tag '%hs' in contenxt '%hs' not set. Defaulting to Normal Priority.",
					tagName, contextName );

				m_tables[i][j] = io::eAsyncPriority_Normal;
			}
		}
	}

	// stats
	RED_LOG( "Core: Loading priorities initialized. Current mode: %hs.", Helper::GetIOContextName(m_context) );
	m_initialized = true;

	// loaded
	return true;
}

void CFileSystemPriorityResovler::SwitchConext( const EIOContext context )
{	
	if ( m_context != context )
	{
		RED_LOG( "Core: IO context switched to %hs", Helper::GetIOContextName(context) );
		m_context = context;
	}
}

const io::EAsyncPriority CFileSystemPriorityResovler::Resolve( const EIOTag tag ) const
{
	// no tables yet
	if ( !m_initialized )
		return io::eAsyncPriority_Normal;

	if ( tag == eIOTag_BundlePreload )
		return io::eAsyncPriority_High;

	// ASSERT
	RED_FATAL_ASSERT( m_context < eIOContext_MAX, "Invalid IO context" );
	RED_FATAL_ASSERT( tag < eIOTag_MAX, "Invalid IO tag" );
	return (io::EAsyncPriority) m_tables[ m_context ][ tag ];
}

//----

namespace Helper
{

	const AnsiChar* GetIOTagName( const EIOTag tag )
	{
		#define TEST(x) case x: return (#x) +7;
		switch ( tag )
		{
			TEST(eIOTag_Generic);
			TEST(eIOTag_BundlePreload);
			TEST(eIOTag_TexturesLow);
			TEST(eIOTag_TexturesNormal);
			TEST(eIOTag_TexturesImmediate);
			TEST(eIOTag_MeshesLow);
			TEST(eIOTag_MeshesNormal);
			TEST(eIOTag_MeshesImmediate);
			TEST(eIOTag_AnimationsLow);
			TEST(eIOTag_AnimationsNormal);
			TEST(eIOTag_AnimationsImmediate);
			TEST(eIOTag_SoundNormal);
			TEST(eIOTag_SoundImmediate);
			TEST(eIOTag_CollisionNormal);
			TEST(eIOTag_CollisionImmediate);
			TEST(eIOTag_TerrainHeight);
			TEST(eIOTag_TerrainControl);
			TEST(eIOTag_TerrainColor);
			TEST(eIOTag_ResourceLow);
			TEST(eIOTag_ResourceNormal);
			TEST(eIOTag_ResourceImmediate);
		}
		#undef TEST

		return "Unknown";
	}

	const AnsiChar* GetIOContextName( const EIOContext context )
	{
		#define TEST(x) case x: return (#x) + 11;
		switch ( context )
		{
			TEST(eIOContext_Boot);
			TEST(eIOContext_WorldLoading);
			TEST(eIOContext_WorldStreaming);
			TEST(eIOContext_SceneLoading);
		}
		#undef TEST

		return "Unknown";
	}

} // Helper

//----