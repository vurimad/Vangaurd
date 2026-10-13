/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/gameTweakDB/include/tweakDBResource.h"

class TweakDBFactoryOptions: public FactoryOptions
{
	RTTI_DECLARE_TYPE( TweakDBFactoryOptions );
	RED_BASE_CLASS( FactoryOptions );

public:
	TweakDBFactoryOptions()
	{}

	TweakDBFactoryOptions( const String& path )
		: FactoryOptions( path )
	{}

	virtual ~TweakDBFactoryOptions() {}

	virtual Bool NeedOptions() const override { return true; }

	String m_package;
};

RTTI_BEGIN_TYPE( TweakDBFactoryOptions );
	RTTI_PARENT_TYPE( FactoryOptions );
	RTTI_PROPERTY( m_package ).editable().inlined();
RTTI_END_TYPE();

class TweakDBFactory: public IFactory
{
	RTTI_DECLARE_TYPE( TweakDBFactory );

public:
	TweakDBFactory()
	{
		m_resourceClass = ClassID< TweakDBResource >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{		
		THandle< TweakDBFactoryOptions > tweakDBOptions = Cast< TweakDBFactoryOptions >( options );

		if( tweakDBOptions->m_package.Empty() )
		{
			RED_LOG_ERROR( "Package name cannot be empty." );
			return false;
		}

		for( const auto& ch : tweakDBOptions->m_package )
		{
			if( !( ( ch >= '0' && ch <= '9' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) || ch == '_' ) )
			{
				RED_LOG_ERROR( "Invalid package character '%c'.", ch );
				return false;
			}
		}

		String data = String::Printf( "package %hs\r\n", tweakDBOptions->m_package );
		Cast< TweakDBResource >( res )->ParseData( std::move( data ) );
		return true;
	}

	virtual THandle<FactoryOptions> CreateOptions( const res::ResourcePath& path ) const override
	{
		return CreateHandle< TweakDBFactoryOptions >( path.ToString() );
	}
};

RTTI_BEGIN_TYPE( TweakDBFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
