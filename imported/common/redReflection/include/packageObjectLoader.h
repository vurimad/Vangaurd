/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "package.h"
#include "resourceLoaderTypes.h"
#include "packageTableOfContentReader.h"
#include "../../redMemory/include/sharedFromThis.h"

namespace res { class ResourceLoader; }
namespace job{ class CounterChain; }
namespace job{ class Builder; }


namespace red
{
	class PackageObjectLoader;
	class PackageReadStream;
	class PackageReader;
	class PackageTypeSerializerDictionary;
	class PackageErrorReporter;

	struct PackageObjectLoaderParameter
	{
		Package sourcePackage;
		res::ResourceLoader * resourceLoader;
		bool skipPostLoad;
		bool skipImports;
		bool skipUploadRenderData;
		job::Priority priority = job::Priority::Latent;
		job::Affinity affinity = job::Affinity::All;
		io::EAsyncPriority ioPriority = io::eAsyncPriority_Normal;
		BitSetDynamic optionalMask{ red::PoolEngine() }; // ctremblay: By default, every root object are loaded. A mask can be provided if only a subset of object needs to be loaded.
		red::UniqueBuffer optionalPackageBuffer; // ctremblay: If Package buffer is provided, it will be released when it is not needed, making memory available quicker.
	};

	// ctremblay: For UnitTesting Only 
	class RED_REFLECTION_API PackageObjectLoaderJobDispatcher
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		PackageObjectLoaderJobDispatcher();
		RED_MOCKABLE ~PackageObjectLoaderJobDispatcher();

		void Initialize( const PackageObjectLoader * loader );

		RED_MOCKABLE void ScheduleSerializationJob( job::Builder & builder ) const;
		RED_MOCKABLE void SchedulePostLoadJob( job::Builder & builder ) const;

	private:

		const PackageObjectLoader * m_loader;
	};

	class RED_REFLECTION_API PackageObjectLoader : public red::EnableSharedFromThis< PackageObjectLoader >
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		PackageObjectLoader();
		~PackageObjectLoader();

		void Initialize( PackageObjectLoaderParameter& parameter );

		serialization::LoadingToken IssueJobs();
		void HACK_WaitOnResourceAndCreateObjects();
		void WaitOnResourceAndCreateObjects( job::Builder & builder );

		red::DynArray< SerializableHandle > ReleaseObjects();

		// FOR UNIT TEST ONLY
		void Internal_ExecuteSerialization();
		void Internal_ExecutePostLoad();
		void Internal_SetJobDispatcher( red::UniquePtr< PackageObjectLoaderJobDispatcher > dispatcher );

	private:

		void IssueAllResourceDependecyLoading();
		void ScheduleObjectSerialization( job::Builder & builder );
		void ScheduleObjectPostLoad( job::Builder & builder );

		void CreateAllRequestedObject();
		void SerializeAllRequestedObject();

		Package m_sourcePackage;
		BitSetDynamic m_optionalMask;
		UniqueBuffer m_optionalBuffer;
		res::ResourceLoader * m_resourceLoader;
		red::DynArray< SerializableHandle > m_loadedObjects;
		bool m_skipPostLoad;
		bool m_skipImports;
		bool m_skipUploadRenderData;
		job::Priority m_priority;
		job::Affinity m_affinity;
		io::EAsyncPriority m_ioPriority;

		// ctremblay: TODO Too much Dynallocation here. Mostly for test, but not really needed.
		// Also, PackageInspector duplicate a lot of logic there. Either merge, or use PackageInspectpr.

		PackageTableReader m_table;
		red::UniquePtr< PackageTableOfContentReader > m_tableOfContent;
		red::UniquePtr< PackageReader > m_packageReader;
		red::UniquePtr< PackageReadStream > m_stream;
		red::UniquePtr< PackageTypeSerializerDictionary > m_dictionary;
		red::UniquePtr< PackageErrorReporter > m_errorReporter;
		red::UniquePtr< PackageObjectLoaderJobDispatcher > m_dispatcher;
	};

	RED_REFLECTION_API red::SharedPtr< PackageObjectLoader > CreatePackageObjectLoader( PackageObjectLoaderParameter & param );
}
