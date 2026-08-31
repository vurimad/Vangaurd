#include <vanguard/rendering/render_command_system.hpp>

namespace
{
    namespace rendering = vanguard::rendering;

    using CheckFunction = void (*)(bool condition, const char* message) noexcept;

    rendering::RenderFrameExecutionStatus ExecuteCameraFrameTick(rendering::RenderFrameTickContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameExecutionStatus ExecuteCameraFrame(rendering::RenderFrameContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderCameraDesc CameraDesc(const rendering::RenderSceneHandle scene, const char* const name) noexcept
    {
        rendering::RenderCameraDesc desc;
        desc.scene = scene;
        desc.name = name;
        desc.state.phases = rendering::RenderPhaseSet(1);
        return desc;
    }
} // namespace

void RunRenderCameraTests(CheckFunction check) noexcept
{
    rendering::RenderSceneManager scenes;
    rendering::RenderSceneFailure sceneFailure;
    check(scenes.Initialize({}, &sceneFailure), "camera tests initialize RenderSceneManager");

    rendering::RenderCameraStorage cameras;
    rendering::RenderCameraFailure cameraFailure;
    rendering::RenderCameraStorageConfig cameraConfig;
    cameraConfig.maximumDependencyDepth = 4;
    check(cameras.Initialize(scenes, cameraConfig, &cameraFailure), "camera tests initialize RenderCameraStorage");

    rendering::RenderCommandSystem commands;
    rendering::RenderCommandFailure commandFailure;
    rendering::RenderCommandSystemConfig commandConfig;
    commandConfig.executeFrameTick = &ExecuteCameraFrameTick;
    commandConfig.executeFrame = &ExecuteCameraFrame;
    check(commands.Initialize(scenes, cameras, commandConfig, &commandFailure), "camera tests initialize RenderCommandSystem");

    rendering::RenderSceneDesc mainSceneDesc;
    mainSceneDesc.name = "CameraMainScene";
    mainSceneDesc.maximumProxies = 32;
    mainSceneDesc.maximumPendingProxyMutations = 32;
    mainSceneDesc.maximumViews = 8;
    rendering::RenderSceneHandle mainScene;
    check(scenes.CreateScene(mainSceneDesc, mainScene, &sceneFailure), "camera tests create main RenderScene");

    rendering::RenderSceneDesc secondarySceneDesc = mainSceneDesc;
    secondarySceneDesc.name = "CameraSecondaryScene";
    rendering::RenderSceneHandle secondaryScene;
    check(scenes.CreateScene(secondarySceneDesc, secondaryScene, &sceneFailure), "camera tests create secondary RenderScene");

    rendering::RenderCameraHandle mainCamera;
    rendering::RenderCameraHandle mirrorCamera;
    rendering::RenderCameraHandle portalCamera;
    check(commands.RegisterCamera(CameraDesc(mainScene, "MainCamera"), mainCamera, &cameraFailure), "register main camera");
    check(commands.RegisterCamera(CameraDesc(mainScene, "MirrorCamera"), mirrorCamera, &cameraFailure), "register mirror camera");
    check(commands.RegisterCamera(CameraDesc(mainScene, "PortalCamera"), portalCamera, &cameraFailure), "register portal camera");
    check(commands.AddCameraDependency(mainCamera, mirrorCamera, rendering::RenderCameraDependencyOutputs::Color, &cameraFailure),
          "main camera depends on mirror color");
    check(commands.AddCameraDependency(mirrorCamera, portalCamera, rendering::RenderCameraDependencyOutputs::Final, &cameraFailure),
          "mirror camera depends on portal final output");

    check(!commands.AddCameraDependency(portalCamera, mainCamera, rendering::RenderCameraDependencyOutputs::Color, &cameraFailure) &&
              cameraFailure.code == rendering::RenderCameraFailureCode::DependencyCycle,
          "camera dependency graph rejects cycles");
    check(commands.AddCameraDependency(mainCamera, mirrorCamera, rendering::RenderCameraDependencyOutputs::Final, &cameraFailure),
          "duplicate camera dependency merges output requirements");
    rendering::RenderCameraDependencyOutputs mergedOutputs = rendering::RenderCameraDependencyOutputs::None;
    check(cameras.HasDependency(mainCamera, mirrorCamera, &mergedOutputs) &&
              rendering::HasOutput(mergedOutputs, rendering::RenderCameraDependencyOutputs::Color) &&
              rendering::HasOutput(mergedOutputs, rendering::RenderCameraDependencyOutputs::Final),
          "camera dependency output requirements merge without duplicate edges");

    rendering::RenderCameraHandle foreignCamera;
    rendering::RenderCameraDesc foreignDesc = CameraDesc(secondaryScene, "AlwaysCamera");
    foreignDesc.renderPolicy = rendering::RenderCameraRenderPolicy::Always;
    check(commands.RegisterCamera(foreignDesc, foreignCamera, &cameraFailure), "register always-render camera in a second scene");
    check(!commands.AddCameraDependency(mainCamera, foreignCamera, rendering::RenderCameraDependencyOutputs::Color, &cameraFailure) &&
              cameraFailure.code == rendering::RenderCameraFailureCode::WrongScene,
          "camera dependencies cannot cross RenderScenes");

    check(!scenes.DestroyScene(mainScene, &sceneFailure) && sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
          "RenderScene destruction is blocked while cameras remain alive");
    check(!commands.UnregisterCamera(portalCamera, &cameraFailure) && cameraFailure.code == rendering::RenderCameraFailureCode::CameraStillReferenced,
          "camera unregistration is blocked while a parent references it");

    check(commands.RemoveCameraDependency(mainCamera, mirrorCamera, &cameraFailure), "remove main-to-mirror dependency");
    check(commands.RemoveCameraDependency(mirrorCamera, portalCamera, &cameraFailure), "remove mirror-to-portal dependency");
    check(commands.UnregisterCamera(portalCamera, &cameraFailure), "unregister portal camera");
    check(commands.UnregisterCamera(mirrorCamera, &cameraFailure), "unregister mirror camera");
    check(commands.UnregisterCamera(mainCamera, &cameraFailure), "unregister main camera");

    rendering::RenderCameraHandle reusedCamera;
    check(commands.RegisterCamera(CameraDesc(mainScene, "ReusedCamera"), reusedCamera, &cameraFailure) && reusedCamera.index == mainCamera.index &&
              reusedCamera.generation != mainCamera.generation && !cameras.IsAlive(mainCamera),
          "camera slot reuse advances generation and invalidates stale handles");
    check(commands.UnregisterCamera(reusedCamera, &cameraFailure), "unregister reused camera");
    check(commands.UnregisterCamera(foreignCamera, &cameraFailure), "unregister second-scene camera");

    check(scenes.DestroyScene(secondaryScene, &sceneFailure), "destroy secondary camera RenderScene");
    check(scenes.DestroyScene(mainScene, &sceneFailure), "destroy main camera RenderScene");
    check(commands.Shutdown(&commandFailure), "camera tests shut down RenderCommandSystem");
    check(cameras.Shutdown(&cameraFailure), "camera tests shut down RenderCameraStorage");
    check(scenes.Shutdown(&sceneFailure), "camera tests shut down RenderSceneManager");
}
