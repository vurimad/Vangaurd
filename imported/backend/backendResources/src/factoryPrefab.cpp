/**
 * Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/world/include/worldPublic.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../../common/redReflection/include/editableView.h"

class PrefabFactoryOptions : public FactoryOptions
{
	RTTI_DECLARE_TYPE(PrefabFactoryOptions);
	RED_BASE_CLASS(FactoryOptions);

public:
	PrefabFactoryOptions()
	{}

	PrefabFactoryOptions( const String& path )
		: FactoryOptions( path )
	{}

	virtual ~PrefabFactoryOptions() {}

	virtual Bool NeedOptions() const override { return true; }

	String m_options;
};


RTTI_BEGIN_TYPE(PrefabFactoryOptions);
	RTTI_PARENT_TYPE(FactoryOptions);
		RTTI_PROPERTY(m_options).editable();
RTTI_END_TYPE();

/// Factory for scene prefab
class PrefabFactory : public IFactory
{
	RTTI_DECLARE_TYPE( PrefabFactory );

public:
	PrefabFactory()
	{
		m_resourceClass = ClassID< world::Prefab >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto prefab = Cast< world::Prefab >( res );

		THandle< PrefabFactoryOptions > scenePrefabOptions = Cast< PrefabFactoryOptions>( options );
		if ( scenePrefabOptions && !scenePrefabOptions->m_options.Empty() )
		{
			if ( auto view = prefab->CreateView() )
			{
				tools::ObjectCreationInfo info( scenePrefabOptions->m_options );
				if ( info.m_arguments->IsStructure() )
				{
					for ( Uint32 i = 0; i < info.m_arguments->GetNumElements(); ++i )
					{
						const auto name = info.m_arguments->GetStructElementName( i );
						const auto value = info.m_arguments->GetStructElement( i );
						rtti::ValuePtr oldValue;
						view->GetValue( IRTTIContext::GetDefault(), rtti::AccessPath( name.AsChar() ), oldValue );
						view->SetValue( IRTTIContext::GetDefault(), rtti::AccessPath( name.AsChar() ), oldValue, value );
					}
				}
			}
		}

		return true;
	}

	virtual THandle<FactoryOptions> CreateOptions( const res::ResourcePath& path ) const override
	{
		return CreateHandle<PrefabFactoryOptions>( path.ToString() );
	}
};

RTTI_BEGIN_TYPE( PrefabFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
