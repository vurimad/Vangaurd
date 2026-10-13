#include "build.h"

#include "../../../backend/backendMesh/include/meshPhysicsUtils.h"
#include "../../../backend/backendEngine/include/commandletPhysicsNatives.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/resource/include/resourceSaver.h"

class GenerateCollisionVolumesCommandlet: public red::ICommandlet
{
	RTTI_DECLARE_TYPE(GenerateCollisionVolumesCommandlet);

public:
	GenerateCollisionVolumesCommandlet();
	virtual Bool Execute(const red::CommandLine& options) override final;
	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;

private:
	virtual Bool InitializeEnvironment() override final;
	virtual void TeardownEnvironment() override final;

	Bool RegenerateCollisionVolume(CResource & resource);
	Bool ParseType(const String& type);
	tools::MeshSimpleCollisionType m_type;
	commandlet::PhysicsNatives natives;
};

GenerateCollisionVolumesCommandlet::GenerateCollisionVolumesCommandlet()
	: ICommandlet(RED_NAME_CONSTEXPR("generatecollisionshapes"))
	, m_type(tools::MeshSimpleCollisionType::Box)
{
}

const char* GenerateCollisionVolumesCommandlet::GetOneLiner() const
{
	return "Regenerate collision volumes";
}

void GenerateCollisionVolumesCommandlet::PrintHelp() const
{
}

Bool GenerateCollisionVolumesCommandlet::InitializeEnvironment()
{
	natives.Initialize();
	return true;
}

void GenerateCollisionVolumesCommandlet::TeardownEnvironment()
{
	natives.Shutdown();
}

Bool GenerateCollisionVolumesCommandlet::Execute(const red::CommandLine& options)
{
	if(!options.HasOption("volume_type"))
	{
		RED_LOG_ERROR("Please specify volume_type parameter with box, sphere, cylinder, capsule, convex or trimesh");
		return false;
	}

	String out;
	if(!options.GetFirstOption("volume_type",out))
	{
		RED_LOG_ERROR("Please specify volume_type parameter as first option");
		return false;
	}

	if(!ParseType(out))
		return false;

	red::DynArray<red::String> m_failures;

	auto params = options.GetValues("file");
	for(auto& path : params)
	{
		auto res = GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build(path))->WaitUntilLoaded();

		if(res)
		{
			if(RegenerateCollisionVolume(*res))
			{
				auto res_path = res->GetPath();

				res::ResourceSavingOptions options;
				options.m_targetPath = res_path;
				if(!res::SaveResource(res, options))
				{
					m_failures.PushBack(path);
					res->UnmarkModified();
				}
			}
		}
		else
		{
			m_failures.PushBack(path);
			res->UnmarkModified();
		}
	}

	if(m_failures.Empty())
		return true;

	for(auto& failure : m_failures)
	{
		RED_LOG_ERROR("Failed to regenerate collision volumes for [%hs]",failure.AsChar());
	}

	return false;
}

Bool GenerateCollisionVolumesCommandlet::RegenerateCollisionVolume(CResource & resource)
{
	if(resource.IsA<CMesh>())
	{
		CMesh& mesh = static_cast<CMesh&>(resource);
		THandle< physics::SystemResource > physics;

		THandle<physics::ICollider> collider;

		if(m_type == tools::MeshSimpleCollisionType::Box)
		{
			collider = tools::BuildBoxFromMesh(mesh);
		}
		else if(m_type == tools::MeshSimpleCollisionType::Sphere)
		{
			collider = tools::BuildSphereFromMesh(mesh);
		}
		else if(m_type == tools::MeshSimpleCollisionType::Capsule)
		{
			collider = tools::BuildCapsuleFromMesh(mesh);
		}
		else if(m_type == tools::MeshSimpleCollisionType::Convex)
		{
			collider = tools::BuildConvexFromMesh(mesh);
		}
		else
		{
			RED_LOG_ERROR("Invalid Collider");
			return false;
		}

		physics = tools::BuildSystemFromCollider(collider);
		auto param = mesh.CreateParameter< mesh::MeshParamPhysics>();
		param->SetPhysicsData(physics);
		return true;
	}

	return false;
}

Bool GenerateCollisionVolumesCommandlet::ParseType(const String& _type)
{
	String type = _type;
	red::StrToLower(type.AsChar(),type.Length());

	if(type == "box")
	{
		m_type = tools::MeshSimpleCollisionType::Box;
	}
	else if(type == "sphere")
	{
		m_type = tools::MeshSimpleCollisionType::Sphere;
	}
	else if(type == "capsule")
	{
		m_type = tools::MeshSimpleCollisionType::Capsule;
	}
	else if(type == "convex")
	{
		m_type = tools::MeshSimpleCollisionType::Convex;
	}
	else
	{
		return false;
	}

	return true;
}

RTTI_BEGIN_TYPE(GenerateCollisionVolumesCommandlet);
	RTTI_PARENT_TYPE(ICommandlet);
RTTI_END_TYPE();