/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "resource.h"
#include "resourcePath.h"
#include "resourceClassLookup.h"
#include "../../redCore/include/absolutePath.h"

using red::DynArray;

namespace res
{

	namespace prv
	{
		ResourceClassLookup::ResourceClassLookup()
			: m_classes{ red::PoolResource() }
		{
			BuildClassMap();
		}

		const rtti::ClassType* ResourceClassLookup::GetResourceClassForFile( const ResourcePath& path ) const
		{
			const String fileExtension = red::paths::ExtractExtension( path.ToStringView() );

			// find in map
			const rtti::ClassType* existingResourceClass = nullptr;
			m_classes.Find( fileExtension, existingResourceClass );

			return existingResourceClass; // known class
		}

		ResourceClassLookup& ResourceClassLookup::GetInstance()
		{
			static ResourceClassLookup theInstance;
			return theInstance;
		}

		void ResourceClassLookup::BuildClassMap()
		{
			GetRttiSystem().RegisterPendingTypes();

			DynArray< const rtti::ClassType* > resourceClasses{ red::PoolEngine() };
			GetRttiSystem().EnumClasses( ClassID< CResource >(), resourceClasses );

			for ( const rtti::ClassType* resourceClass : resourceClasses )
			{
				// get the default object
				CResource* resource = resourceClass->GetDefaultObject< CResource >();
				RED_FATAL_ASSERT( resource != nullptr, "Invalid resource class '%s'", resourceClass->GetName().AsChar() );

				// get extensions
				String extension = resource->GetExtension();
				String deprecatedExtension = resource->GetDeprecatedExtension();

				RED_FATAL_ASSERT( !extension.Empty(), "Invalid resource '%s' extension", resourceClass->GetName().AsChar() );

				RegisterResourceClass( std::move( extension ), resourceClass );
				if ( !deprecatedExtension.Empty() && extension != deprecatedExtension )
				{
					RegisterResourceClass( std::move( deprecatedExtension ), resourceClass );
				}
			}
		}

		void ResourceClassLookup::RegisterResourceClass( String&& extension, const rtti::ClassType* resourceClass )
		{
			// make sure extension is not used by other resource class
			const rtti::ClassType* existingResourceClass = nullptr;
			if ( m_classes.Find( extension, existingResourceClass ) )
			{
				// do not complain if everything is all right
				if ( existingResourceClass != resourceClass )
				{
					if ( existingResourceClass )
					{
						RED_LOG_ERROR( "Core: Resource extension '%hs' is already used by class '%hs'. Trying to reuse it for class '%hs'",
							extension.AsChar(), existingResourceClass->GetName().AsChar(), resourceClass->GetName().AsChar() );
					}

					m_classes.Set( extension, nullptr );
					return;
				}
			}

			// register in the map
			m_classes.Set( extension, resourceClass );
		}

	} // prv

const rtti::ClassType* GetResourceClassForFile( const ResourcePath& path )
{
	return prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( path );
}

} // res


