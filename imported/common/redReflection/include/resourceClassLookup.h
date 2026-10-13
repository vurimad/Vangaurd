/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "resourcePath.h"
#include "resourceLoader.h"

namespace res
{
	RED_REFLECTION_API const rtti::ClassType* GetResourceClassForFile( const ResourcePath& path );

	namespace prv
	{
		// Helper class to map a resource path to a resource class
		// Maintains a cache internally for faster access
		// TODO: Move this to the cpp, we don't need it exposed here
		class RED_REFLECTION_API ResourceClassLookup
		{
		public:
			ResourceClassLookup();

			/// find an engine class that matches given resource extension (extracted from path)
			const rtti::ClassType* GetResourceClassForFile( const ResourcePath& path ) const;

			// this is a singleton
			static ResourceClassLookup& GetInstance();

		private:
			void BuildClassMap();
			void RegisterResourceClass( String&& extension, const rtti::ClassType* resourceClass );

			typedef red::HashMap< String, const rtti::ClassType* > TResourceClasses;
			TResourceClasses	m_classes; // only classes that have unique extension->const rtti::ClassType mapping
		};

	} // prv

} // res