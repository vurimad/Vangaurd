/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/redSystem/include/crt.h"
#include "../../../common/redSystem/include/hex.h"
#include "../../../common/redSystem/include/clock.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../backend/backendEngine/include/commandletUtils.h"
#include "../../../backend/backendEngine/include/commandletEngineInstance.h"

class ResourcePathHashToStringPreset_Entry : public ISerializable
{
	RTTI_DECLARE_TYPE( ResourcePathHashToStringPreset_Entry );
	RED_BASE_CLASS( ISerializable );

public:
	ResourcePathHashToStringPreset_Entry() = default;
	ResourcePathHashToStringPreset_Entry( Uint64 entityPathHash, red::String entityPath, Uint64 timestamp )
		: m_resourcePath( std::move( entityPath ) )
		, m_timestamp( timestamp )
	{
		const void* hashPtr = static_cast<const void*>( &entityPathHash );
		Uint32 pathHashEncodedLength = red::HexEncodedSize( sizeof( Uint64 ) ) + 1;
		m_resourcePathHash.Resize( m_resourcePathHash.Length() + pathHashEncodedLength );
		red::HexEncode( hashPtr, sizeof( Uint64 ), m_resourcePathHash.AsChar(), pathHashEncodedLength );
	}

	Bool UnhexKey( Uint64& resourcePathHash ) const
	{
		resourcePathHash = 0;
		void* hashPtr = static_cast<void*>( &resourcePathHash );

		if( !red::HexDecode( m_resourcePathHash.AsChar(), m_resourcePathHash.Length(), hashPtr, sizeof( Uint64 ) ) )
		{
			return false;
		}
		return true;
	}

	red::String m_resourcePathHash;
	red::String m_resourcePath;
	Uint64 m_timestamp;
};

class ResourcePathHashToStringPreset : public ISerializable
{
	RTTI_DECLARE_TYPE( ResourcePathHashToStringPreset );
	RED_BASE_CLASS( ISerializable );

protected:
	virtual void OnPostLoad( const PostLoadContext& context ) override
	{
		TBaseClass::OnPostLoad( context );

		for( auto& preset : m_presets )
		{
			Uint64 key;
			preset.UnhexKey( key );
			const auto result = m_presetLookup.Insert( key, preset.m_resourcePath );
			RED_FATAL_ASSERT( result.IsSuccessful(), "There are two identical entity-tag presets defined! %s and %s", preset.m_resourcePathHash, preset.m_resourcePath.AsChar() ); // or a hash collision occurred 
		}
	}

public:
	void AddUniqueEntry( Uint64 hash, red::String&& path )
	{
		auto result = m_presetLookup.Insert( hash, path );
		if( result.IsSuccessful() )
		{
			red::DateTime timestamp;
			red::Clock::GetInstance().GetLocalTime( timestamp );
			m_presets.EmplaceBack( hash, std::move( path ), timestamp.GetUnixTimestamp() );
		}
	}

	red::DynArray< ResourcePathHashToStringPreset_Entry > m_presets{ game::PoolGMPL_Population_Data() };

private:
	red::HashMap< Uint64, red::String > m_presetLookup{ game::PoolGMPL_Population_Data() };
};

RTTI_BEGIN_TYPE( ResourcePathHashToStringPreset );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_presets );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( ResourcePathHashToStringPreset_Entry );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_resourcePathHash );
	RTTI_PROPERTY( m_resourcePath );
	RTTI_PROPERTY( m_timestamp );
RTTI_END_TYPE();

class ResourcePathHashMapperCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ResourcePathHashMapperCommandlet );

public:
	ResourcePathHashMapperCommandlet();
	~ResourcePathHashMapperCommandlet();

	// interface
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	commandlet::EngineInstance m_engineInstance;
	res::ResourceEditorDepot* m_sourceDepot;
};

RTTI_BEGIN_TYPE( ResourcePathHashMapperCommandlet );
	RTTI_PARENT_TYPE( ICommandlet );
RTTI_END_TYPE();

namespace
{
	static constexpr char s_resourcePathHashMap[] = "test\\commandlet\\resourcepath_hash_mapper\\resourcepath_hash.json";

	void WriteLine( const red::StringView& line, FILE* stream = stdout )
	{
		fprintf( stream, "%s\n", line.Data() );
		fflush( stream );
	}

	Bool SaveJsonResource( THandle<ISerializable> data, const res::ResourcePath& path )
	{
		serialization::json::JsonSerializer serializer( data );

		String jsonText;
		serializer.Serialize( jsonText );

		auto file = commandlet::GetEditorDepot()->CreateResourceWriter( path );

		if( !file )
		{
			WriteLine( "\nCouldn't open file '" + path.ToString() + "'" );
			WriteLine( "\nDid you forget to checkout file in P4?" );
			return false;
		}

		file->Serialize( jsonText.AsChar(), jsonText.Length() );

		WriteLine( "Saved to file: " + path.ToString() );

		return true;
	}
}

ResourcePathHashMapperCommandlet::ResourcePathHashMapperCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "resourcePathHashMapper" ) )
{
}

ResourcePathHashMapperCommandlet::~ResourcePathHashMapperCommandlet()
{
}

Bool ResourcePathHashMapperCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void ResourcePathHashMapperCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

Bool ResourcePathHashMapperCommandlet::Execute( const red::CommandLine& options )
{
	// load existing json into a map
	const TResRef< JsonResource > jsonResource( res::ResourcePath::Build( s_resourcePathHashMap ) );
	THandle< JsonResource > presetJsonResource = jsonResource.Get();
	if( !presetJsonResource )
	{
		return false;
	}

	THandle< ResourcePathHashToStringPreset > preset = presetJsonResource->GetRoot< ResourcePathHashToStringPreset >();
	if( !preset )
	{
		return false;
	}

	// load all .quest resources
	auto* depot = res::GetEditorDepot();
	const red::DynArray<res::ResourcePath> questResourcePaths = commandlet::ScanResourceDepotDirectories( *depot, "", "*.quest" ); // hardcoded for .quest resources, that's all we need right now
	for( const auto& questPath : questResourcePaths )
	{
		preset->AddUniqueEntry( questPath.GetHash(), std::move( questPath.ToString() ) );
	}

	return SaveJsonResource( preset, res::ResourcePath::Build( s_resourcePathHashMap ) );
}

const char* ResourcePathHashMapperCommandlet::GetOneLiner() const
{
	return "Scans the depot for .quest resources and maps their paths and paths' hashes.";
}

void ResourcePathHashMapperCommandlet::PrintHelp() const
{
}