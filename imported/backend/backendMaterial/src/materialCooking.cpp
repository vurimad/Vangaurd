/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendData/include/cooker.h"
#include "../../backendData/include/dataTypesImpl.h"
#include "../../backendData/include/resourceDependency.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/dependencyUtils.h"

#include "../../backendEngine/include/resourceMetadata.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"

class MaterialCooker : public ICooker
{
public:
	RTTI_DECLARE_TYPE(MaterialCooker);

	virtual ~MaterialCooker() override = default;

	virtual bool CanCook(const res::ResourcePath& resourcePath) const override;
	virtual bool CollectDependencies(const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system) const override;
	virtual bool Cook(const res::ResourcePath& path, job::Builder& builder, data::Context& system) const override;
	virtual Uint32 GetVersion() const override;
};

namespace helper
{
	static red::DynArray< const char* > GetMaterialLikeFileExtensions()
	{
		red::DynArray< const rtti::ClassType* > materialClasses;
		GetRttiSystem().EnumClasses(ClassID< CMaterialInstance >(), materialClasses);
		GetRttiSystem().EnumClasses(ClassID< CMaterialTemplate >(), materialClasses);

		red::DynArray< const char* > result;
		result.Reserve(materialClasses.Size() * 2); // extension and deprecated extensions

		for (const rtti::ClassType* materialClass : materialClasses)
		{
			auto* defaultMaterial = materialClass->GetDefaultObject< CMaterialInstance >();
			if (defaultMaterial)
			{
				const char* ext = defaultMaterial->GetExtension();
				if (ext)
				{
					result.PushBack(ext);
				}
				const char* depExt = defaultMaterial->GetDeprecatedExtension();
				if (depExt && red::Strlen(depExt) > 0 && red::Strcmp(ext, depExt) != 0)
				{
					result.PushBack(depExt);
				}
			}
		}

		return result;
	}

	static bool ShouldMergeMaterialDependencies(const res::ResourcePath& path, data::TargetPlatform platform)
	{
		if (platform == data::TargetPlatform::WINDOWS_SERVER || platform == data::TargetPlatform::LINUX_SERVER)
		{
			return false;
		}

		return true;
	}
}

bool MaterialCooker::CanCook(const res::ResourcePath& resourcePath) const
{
	static red::DynArray< const char* > s_materialLikeExtensions = helper::GetMaterialLikeFileExtensions();
	auto extension = red::paths::GetExtension(resourcePath.ToStringView());
	return std::any_of(s_materialLikeExtensions.Begin(), s_materialLikeExtensions.End(), [extension](const char* materialLikeExtension)
	{
		return materialLikeExtension == extension;
	});
}

bool MaterialCooker::CollectDependencies(const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system) const
{
	collector.Add(MakeSourceDependency(path, dependency::Requirement::Required));

	if (helper::ShouldMergeMaterialDependencies(path, system.TargetPlatform()))
	{
		resdb::Database& database = system.SourceResourceDatabase();
		const auto resourceDependencies = dependency::CollectMergeableDependencies(path, system.TargetPlatform(), database);
		for (const res::ResourcePath& dependencyPath : resourceDependencies)
		{		
			collector.Add(MakeSourceDependency(dependencyPath));
			collector.Add(MakeBuiltDependency(dependencyPath, data::DepotLocation::Cooked));
		}
	}

	return true;
}

bool MaterialCooker::Cook(const res::ResourcePath& path, job::Builder& builder, data::Context& system) const
{
	// Load with all imports and call OnPostLoad
	res::IssueLoadingRequestParameter param;
	param.path = path;
	param.postLoadFlags.skipUploadRenderData = true;

	auto token = system.SourceLoader().IssueLoadingRequest(param);

	builder.DispatchWait(token->GetWaitCounter());

	red::DynArray< res::ResourceTokenHandle > dependencies{ red::PoolBackend() };

	if (helper::ShouldMergeMaterialDependencies(path, system.TargetPlatform()))
	{
		resdb::Database& database = system.SourceResourceDatabase();
		const auto resourceDependencies = dependency::CollectMergeableDependencies(path, system.TargetPlatform(), database);
		for (const res::ResourcePath& dependencyPath : resourceDependencies)
		{
			res::IssueLoadingRequestParameter param;
			param.path = dependencyPath;
			param.postLoadFlags.skipUploadRenderData = true;
			auto token = system.CookedLoader().IssueLoadingRequest(param);
			dependencies.PushBack(token);
			builder.DispatchWait(token->GetWaitCounter());
		}
	}

	builder.DispatchJob("MaterialCooker/Cook", [token, path, &system, dependencies](const job::RunContext& ctx)
	{
		if (!token->IsLoaded())
		{
			system.ErrorHandler().ReportError("MaterialCooker: Failed to load '%hs'", path.ToDebugString());
			system.ErrorHandler().Fail();
			return;
		}
		auto material = Cast< CMaterialInstance >(token->GetResource());
		RED_FATAL_ASSERT(material);

		job::Builder depBuilder( ctx );
		material->EnsureGlassMaskUpdated( depBuilder );

		depBuilder.DispatchJob( "MaterialCooker/Cook/SaveResource", [&system, dependencies, path, material] ( const job::RunContext& ctx )
		{
			red::DynArray< THandle< CResource > > inplaceResources{ red::PoolBackend() };
			inplaceResources.Reserve( dependencies.Size() );

			for ( const res::ResourceTokenHandle& token : dependencies )
			{
				// ??? Should ANY fail since we got them from the resDB
				// Suppose it's possible if it's a broken reference, but should have cleared these sooner during cooking?
				RED_FATAL_ASSERT( !token->HasFailed() || token->GetError() == res::ResourceTokenErrorType::ResourceNotFound, "Unexpected resource error type %u: %hs", (Uint32)token->GetError(), token->GetPath().ToDebugString() );
				if ( !token->HasFailed() )
				{
					inplaceResources.PushBack( token->GetResource() );
				}
			}

			bool saveResult = cooker::SaveResource( path, material, system.CookedDepot(), system.TargetPlatform(), inplaceResources );
			if ( !saveResult )
			{
				system.ErrorHandler().ReportError( "MaterialCooker: Failed to save '%hs'", path.ToDebugString() );
				system.ErrorHandler().Fail();
				return;
			}
		} );
	});

	return true;
}

Uint32 MaterialCooker::GetVersion() const
{
	return 3;
}

RTTI_BEGIN_TYPE(MaterialCooker);
RTTI_PARENT_TYPE(ICooker);
RTTI_END_TYPE();