/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourceOptions.h"
#include "backendMemory.h"

namespace tools
{
	class BACKEND_ENGINE_API IResourceCommand
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IResourceCommand() {}
		virtual ~IResourceCommand() {}

		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const = 0;
		virtual ResourceCommandResult Execute( const THandle< ResourceCommandOptions >& options ) = 0;
		virtual void SetOptionalParameter( const String& param ) {}
	};

	class BACKEND_ENGINE_API GeneralResourceCommand : public IResourceCommand
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( GeneralResourceCommand );

	public:
		virtual ~GeneralResourceCommand() {}

		void Initialize( const res::ResourcePath& path, const red::DynArray< res::ResourcePath >& aditionalPaths ) 
		{
			m_path = path;
			m_additionalPaths = aditionalPaths;
		}

		const res::ResourcePath& GetResourcePath() const { return m_path; }
		const red::DynArray< res::ResourcePath >& GetAdditionalPaths() const { return m_additionalPaths; }

		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) = 0;
		virtual CName GetName() const = 0;

	private:
		res::ResourcePath m_path;
		red::DynArray< res::ResourcePath > m_additionalPaths;
	};

	typedef red::UniquePtr< IResourceCommand > ResourceCommandUniquePtr;

} // namespace tools