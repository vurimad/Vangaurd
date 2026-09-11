#include <vanguard/rendering/render_camera.hpp>

#include <vanguard/rendering/render_node_impl_context.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 InvalidCameraIndex = ~u32{0};
        inline constexpr u32 ClosingFrameReferences = ~u32{0};

        void ClearFailure(RenderCameraFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(RenderCameraFailure* const failure, const RenderCameraFailureCode code, const char* const message,
                                const RenderSceneHandle scene = {}, const RenderCameraHandle camera = {}, const RenderCameraHandle relatedCamera = {}) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->scene = scene;
                failure->camera = camera;
                failure->relatedCamera = relatedCamera;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] bool ValidCustomDataPriority(const CustomDataPriority priority) noexcept
        {
            return priority == CustomDataPriority::Low || priority == CustomDataPriority::Normal || priority == CustomDataPriority::High;
        }

        [[nodiscard]] bool CopyName(char* const destination, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < MaximumRenderCameraNameBytes && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        void CopyNameUnchecked(char* const destination, const char* const source) noexcept
        {
            u32 index = 0;
            while (index + 1u < MaximumRenderCameraNameBytes && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            destination[index] = '\0';
        }

        [[nodiscard]] bool ValidRenderPolicy(const RenderCameraRenderPolicy policy) noexcept
        {
            return static_cast<u8>(policy) <= static_cast<u8>(RenderCameraRenderPolicy::Always);
        }

        [[nodiscard]] bool ValidOutputs(const RenderCameraDependencyOutputs outputs) noexcept
        {
            constexpr u8 valid = static_cast<u8>(RenderCameraDependencyOutputs::Color) | static_cast<u8>(RenderCameraDependencyOutputs::Final);
            const u8 value = static_cast<u8>(outputs);
            return value != 0 && (value & ~valid) == 0;
        }

        [[nodiscard]] bool Finite(const f32 value) noexcept
        {
            constexpr f32 maximum = 3.402823466e+38f;
            return value >= -maximum && value <= maximum;
        }

        [[nodiscard]] bool Finite(const f32* const values, const u32 count) noexcept
        {
            for (u32 index = 0; index < count; ++index)
                if (!Finite(values[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool ValidCameraState(const RenderCameraState& state) noexcept
        {
            constexpr f32 pi = 3.14159265358979323846f;
            constexpr u32 derivedFlags = static_cast<u32>(RenderViewFlags::Orthographic) | static_cast<u32>(RenderViewFlags::ReverseDepth) |
                                         static_cast<u32>(RenderViewFlags::TemporalHistory) | static_cast<u32>(RenderViewFlags::Jittered) |
                                         static_cast<u32>(RenderViewFlags::InfiniteFarPlane);
            constexpr u32 validInputFlags = static_cast<u32>(RenderViewFlags::Primary) | static_cast<u32>(RenderViewFlags::OcclusionCulling);
            const u32 flags = static_cast<u32>(state.flags);
            const f32 orientationLengthSquared = state.pose.orientation[0] * state.pose.orientation[0] + state.pose.orientation[1] * state.pose.orientation[1] +
                                                 state.pose.orientation[2] * state.pose.orientation[2] + state.pose.orientation[3] * state.pose.orientation[3];
            const bool validResolution =
                static_cast<u8>(state.resolution.mode) <= static_cast<u8>(RenderCameraResolutionMode::Fixed) &&
                ((state.resolution.mode == RenderCameraResolutionMode::Fixed && state.resolution.fixedExtent.IsValid()) ||
                 (state.resolution.mode != RenderCameraResolutionMode::Fixed && Finite(state.resolution.scale) && state.resolution.scale > 0.0f));
            const bool validProjectionKind = static_cast<u8>(state.projection.kind) <= static_cast<u8>(RenderCameraProjectionKind::Orthographic);
            const bool validProjectionShape =
                state.projection.kind == RenderCameraProjectionKind::Perspective
                    ? Finite(state.projection.verticalFieldOfViewRadians) && state.projection.verticalFieldOfViewRadians > 0.0f &&
                          state.projection.verticalFieldOfViewRadians < pi
                    : Finite(state.projection.orthographicHeight) && state.projection.orthographicHeight > 0.0f && !state.projection.infiniteFarPlane;
            return validProjectionKind && validProjectionShape && validResolution && Finite(state.pose.origin.localPosition, 3) &&
                   Finite(state.pose.orientation, 4) && orientationLengthSquared > 1.0e-12f && Finite(state.projection.aspectRatio) &&
                   state.projection.aspectRatio >= 0.0f && Finite(state.projection.nearPlane) && state.projection.nearPlane > 0.0f &&
                   (state.projection.infiniteFarPlane || (Finite(state.projection.farPlane) && state.projection.farPlane > state.projection.nearPlane)) &&
                   Finite(state.projection.offset, 2) && Finite(state.lodBias) && !state.phases.Empty() &&
                   static_cast<u8>(state.purpose) <= static_cast<u8>(RenderViewPurpose::Diagnostic) && (flags & derivedFlags) == 0 &&
                   (flags & ~validInputFlags) == 0;
        }

        [[nodiscard]] bool RequiresCameraCut(const RenderCameraState& previous, const RenderCameraState& next) noexcept
        {
            const RenderCameraProjection& left = previous.projection;
            const RenderCameraProjection& right = next.projection;
            return left.kind != right.kind || left.verticalFieldOfViewRadians != right.verticalFieldOfViewRadians ||
                   left.orthographicHeight != right.orthographicHeight || left.aspectRatio != right.aspectRatio || left.nearPlane != right.nearPlane ||
                   left.farPlane != right.farPlane || left.offset[0] != right.offset[0] || left.offset[1] != right.offset[1] ||
                   left.reverseDepth != right.reverseDepth || left.infiniteFarPlane != right.infiniteFarPlane ||
                   previous.resolution.mode != next.resolution.mode || previous.resolution.fixedExtent.width != next.resolution.fixedExtent.width ||
                   previous.resolution.fixedExtent.height != next.resolution.fixedExtent.height || previous.resolution.scale != next.resolution.scale ||
                   previous.temporalHistory != next.temporalHistory || previous.temporalJitter != next.temporalJitter || previous.purpose != next.purpose;
        }

        [[nodiscard]] u64 NextRevision(const u64 revision) noexcept
        {
            const u64 next = revision + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] RenderCameraExtent ResolveExtent(const RenderCameraState& state, const RenderCameraExtent frameExtent) noexcept
        {
            if (state.resolution.mode == RenderCameraResolutionMode::Fixed)
                return state.resolution.fixedExtent;
            if (state.resolution.mode == RenderCameraResolutionMode::InheritFrame)
                return frameExtent;
            constexpr f32 maximumDimension = 4'294'967'040.0f;
            const f32 width = static_cast<f32>(frameExtent.width) * state.resolution.scale;
            const f32 height = static_cast<f32>(frameExtent.height) * state.resolution.scale;
            if (!Finite(width) || !Finite(height) || width > maximumDimension || height > maximumDimension)
                return {};
            const u32 resolvedWidth = width >= 1.0f ? static_cast<u32>(width + 0.5f) : 1u;
            const u32 resolvedHeight = height >= 1.0f ? static_cast<u32>(height + 0.5f) : 1u;
            return {resolvedWidth, resolvedHeight};
        }

        void Multiply(const f32 left[16], const f32 right[16], f32 output[16]) noexcept
        {
            f32 result[16]{};
            for (u32 row = 0; row < 4; ++row)
                for (u32 column = 0; column < 4; ++column)
                    for (u32 index = 0; index < 4; ++index)
                        result[row * 4u + column] += left[row * 4u + index] * right[index * 4u + column];
            for (u32 index = 0; index < 16; ++index)
                output[index] = result[index];
        }

        void BuildWorldToView(const RenderCameraPose& pose, f32 matrix[16]) noexcept
        {
            const f32 length = std::sqrt(pose.orientation[0] * pose.orientation[0] + pose.orientation[1] * pose.orientation[1] +
                                         pose.orientation[2] * pose.orientation[2] + pose.orientation[3] * pose.orientation[3]);
            const f32 x = pose.orientation[0] / length;
            const f32 y = pose.orientation[1] / length;
            const f32 z = pose.orientation[2] / length;
            const f32 w = pose.orientation[3] / length;
            matrix[0] = 1.0f - 2.0f * (y * y + z * z);
            matrix[1] = 2.0f * (x * y - z * w);
            matrix[2] = 2.0f * (x * z + y * w);
            matrix[3] = 0.0f;
            matrix[4] = 2.0f * (x * y + z * w);
            matrix[5] = 1.0f - 2.0f * (x * x + z * z);
            matrix[6] = 2.0f * (y * z - x * w);
            matrix[7] = 0.0f;
            matrix[8] = 2.0f * (x * z - y * w);
            matrix[9] = 2.0f * (y * z + x * w);
            matrix[10] = 1.0f - 2.0f * (x * x + y * y);
            matrix[11] = 0.0f;
            // GPU Scene positions are made camera-relative from RenderViewOrigin before these matrices are used.
            // Keeping translation out of the matrix prevents the camera origin from being applied twice.
            matrix[12] = 0.0f;
            matrix[13] = 0.0f;
            matrix[14] = 0.0f;
            matrix[15] = 1.0f;
        }

        void BuildProjection(const RenderCameraProjection& projection, const f32 aspect, const f32 jitter[2], f32 matrix[16]) noexcept
        {
            for (u32 index = 0; index < 16; ++index)
                matrix[index] = 0.0f;
            const f32 nearPlane = projection.nearPlane;
            const f32 farPlane = projection.farPlane;
            if (projection.kind == RenderCameraProjectionKind::Perspective)
            {
                const f32 verticalScale = 1.0f / std::tan(projection.verticalFieldOfViewRadians * 0.5f);
                matrix[0] = verticalScale / aspect;
                matrix[5] = verticalScale;
                matrix[8] = projection.offset[0] + jitter[0];
                matrix[9] = projection.offset[1] + jitter[1];
                matrix[11] = 1.0f;
                matrix[10] = projection.reverseDepth ? (projection.infiniteFarPlane ? 0.0f : nearPlane / (nearPlane - farPlane))
                                                     : (projection.infiniteFarPlane ? 1.0f : farPlane / (farPlane - nearPlane));
                matrix[14] = projection.reverseDepth ? (projection.infiniteFarPlane ? nearPlane : nearPlane * farPlane / (farPlane - nearPlane))
                                                     : (projection.infiniteFarPlane ? -nearPlane : -nearPlane * farPlane / (farPlane - nearPlane));
                return;
            }

            const f32 width = projection.orthographicHeight * aspect;
            matrix[0] = 2.0f / width;
            matrix[5] = 2.0f / projection.orthographicHeight;
            matrix[10] = projection.reverseDepth ? -1.0f / (farPlane - nearPlane) : 1.0f / (farPlane - nearPlane);
            matrix[12] = projection.offset[0] + jitter[0];
            matrix[13] = projection.offset[1] + jitter[1];
            matrix[14] = projection.reverseDepth ? farPlane / (farPlane - nearPlane) : -nearPlane / (farPlane - nearPlane);
            matrix[15] = 1.0f;
        }

        void NormalizePlane(VisibilityPlane& plane) noexcept
        {
            const f32 inverseLength =
                1.0f / std::sqrt(plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] + plane.normal[2] * plane.normal[2]);
            plane.normal[0] *= inverseLength;
            plane.normal[1] *= inverseLength;
            plane.normal[2] *= inverseLength;
            plane.distance *= inverseLength;
        }

        void ExtractPlane(const f32 matrix[16], const u32 column, const f32 sign, VisibilityPlane& plane) noexcept
        {
            plane.normal[0] = matrix[3] + sign * matrix[column];
            plane.normal[1] = matrix[7] + sign * matrix[4u + column];
            plane.normal[2] = matrix[11] + sign * matrix[8u + column];
            plane.distance = matrix[15] + sign * matrix[12u + column];
            NormalizePlane(plane);
        }

        void ExtractDepthPlane(const f32 matrix[16], VisibilityPlane& plane) noexcept
        {
            plane.normal[0] = matrix[2];
            plane.normal[1] = matrix[6];
            plane.normal[2] = matrix[10];
            plane.distance = matrix[14];
            NormalizePlane(plane);
        }

        void BuildFrustum(const f32 matrix[16], const bool reverseDepth, const bool infiniteFarPlane, VisibilityFrustum& frustum) noexcept
        {
            ExtractPlane(matrix, 0, 1.0f, frustum.planes[0]);
            ExtractPlane(matrix, 0, -1.0f, frustum.planes[1]);
            ExtractPlane(matrix, 1, 1.0f, frustum.planes[2]);
            ExtractPlane(matrix, 1, -1.0f, frustum.planes[3]);
            if (reverseDepth)
                ExtractPlane(matrix, 2, -1.0f, frustum.planes[4]);
            else
                ExtractDepthPlane(matrix, frustum.planes[4]);
            frustum.planeCount = 5;
            if (!infiniteFarPlane)
            {
                if (reverseDepth)
                    ExtractDepthPlane(matrix, frustum.planes[5]);
                else
                    ExtractPlane(matrix, 2, -1.0f, frustum.planes[5]);
                frustum.planeCount = 6;
            }
        }

        [[nodiscard]] f32 Halton(u32 index, const u32 base) noexcept
        {
            f32 fraction = 1.0f;
            f32 result = 0.0f;
            while (index != 0)
            {
                fraction /= static_cast<f32>(base);
                result += fraction * static_cast<f32>(index % base);
                index /= base;
            }
            return result;
        }

        void CalculateJitter(const u32 index, const RenderCameraExtent extent, f32 jitter[2]) noexcept
        {
            const u32 sample = index % 16u + 1u;
            jitter[0] = 2.0f * (Halton(sample, 2u) - 0.5f) / static_cast<f32>(extent.width);
            jitter[1] = -2.0f * (Halton(sample, 3u) - 0.5f) / static_cast<f32>(extent.height);
        }

    } // namespace

    struct RenderCameraStorage::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        enum class FrameDisposition : u32
        {
            Open,
            Committing,
            Committed
        };

        struct RenderCamera
        {
            RenderCameraHandle handle;
            RenderCameraState state;
            RenderCameraRenderPolicy renderPolicy = RenderCameraRenderPolicy::OnDemand;
            RenderCameraDependency dependencies[MaximumRenderCameraDependencies]{};
            RenderCameraHandle dependents[MaximumRenderCameraDependencies]{};
            CameraCustomData* customData[MaximumCameraCustomDataTypes]{};
            u32 dependencyCount = 0;
            u32 dependentCount = 0;
            u32 pendingPreparedFamilies = 0;
            RenderViewOrigin historyOrigin;
            RenderViewMatrices historyMatrices;
            RenderCameraExtent historyExtent;
            f32 historyJitter[2]{};
            u64 lastSubmittedFrameSerial = 0;
            u64 temporalIdentity = 0;
            u64 requestedCutRevision = 1;
            u64 committedCutRevision = 0;
            bool historyValid = false;
            bool usedInRender = false;
            bool enabled = false;
            char name[MaximumRenderCameraNameBytes]{};
        };

        struct CameraSlot
        {
            RenderCamera camera;
            u32 nextFree = InvalidCameraIndex;
            u32 denseActiveIndex = InvalidCameraIndex;
            u32 denseAlwaysIndex = InvalidCameraIndex;
            u32 generation = 0;
            bool active = false;
        };

        struct FrameSlot
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            FrameSlot() noexcept
                : cameras(memory::pools::Rendering::GetInstance()), views(memory::pools::Rendering::GetInstance()),
                  dependencies(memory::pools::Rendering::GetInstance()), cutRevisions(memory::pools::Rendering::GetInstance())
            {
            }

            containers::DynamicArray<RenderCameraHandle> cameras;
            containers::DynamicArray<RenderView> views;
            containers::DynamicArray<RenderCameraDependency> dependencies;
            containers::DynamicArray<u64> cutRevisions;
            concurrency::Atomic<u32> references{0};
            concurrency::Atomic<u32> disposition{static_cast<u32>(FrameDisposition::Open)};
            RenderViewFamily family;
            RenderSceneHandle scene;
            u32 cameraCount = 0;
            u32 dependencyCount = 0;
            u32 generation = 0;
            u64 frameSerial = 0;
        };

        struct TraversalFrame
        {
            u32 cameraIndex = InvalidCameraIndex;
            u32 nextDependency = 0;
            u32 depth = 0;
        };

        struct SceneState
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            SceneState() noexcept
                : slots(memory::pools::Rendering::GetInstance()), activeCameras(memory::pools::Rendering::GetInstance()),
                  alwaysCameras(memory::pools::Rendering::GetInstance()), discoveredStamps(memory::pools::Rendering::GetInstance()),
                  completedStamps(memory::pools::Rendering::GetInstance()), traversal(memory::pools::Rendering::GetInstance()),
                  preparationCameras(memory::pools::Rendering::GetInstance()), preparationDependencies(memory::pools::Rendering::GetInstance()),
                  frameSlots(memory::pools::Rendering::GetInstance())
            {
            }

            ~SceneState()
            {
                for (FrameSlot* const frame : frameSlots)
                    if (frame != nullptr)
                        VANGUARD_DELETE(frame);
            }

            RenderSceneHandle scene;
            SceneCustomData* customData[MaximumSceneCustomDataTypes]{};
            containers::DynamicArray<CameraSlot> slots;
            containers::DynamicArray<u32> activeCameras;
            containers::DynamicArray<u32> alwaysCameras;
            containers::DynamicArray<u32> discoveredStamps;
            containers::DynamicArray<u32> completedStamps;
            containers::DynamicArray<TraversalFrame> traversal;
            containers::DynamicArray<RenderCameraHandle> preparationCameras;
            containers::DynamicArray<RenderCameraDependency> preparationDependencies;
            containers::DynamicArray<FrameSlot*> frameSlots;
            concurrency::Atomic<u32> activePreparedFamilies{0};
            u32 firstFree = InvalidCameraIndex;
            u32 traversalStamp = 0;
        };

        RenderSceneManager* scenes = nullptr;
        RenderCameraStorageConfig config;
        CameraCustomDataDescriptor cameraCustomData[MaximumCameraCustomDataTypes]{};
        SceneCustomDataDescriptor sceneCustomData[MaximumSceneCustomDataTypes]{};
        SceneState* sceneStates[MaximumRenderScenes]{};
        RenderCameraStorageStats stats;
        concurrency::Atomic<u64> preparedPlans{0};
        concurrency::Atomic<u64> preparedFrames{0};
        concurrency::Atomic<u64> committedFrames{0};
        concurrency::Atomic<u64> abandonedFrames{0};
        concurrency::Atomic<u64> rejectedOperations{0};
        u64 nextTemporalIdentity = 1;

        void CopyCustomDataCatalog(const CustomDataCatalog& catalog) noexcept
        {
            for (const CameraCustomDataDescriptor& descriptor : catalog.cameraTypes)
                cameraCustomData[descriptor.typeIndex] = descriptor;
            for (const SceneCustomDataDescriptor& descriptor : catalog.sceneTypes)
                sceneCustomData[descriptor.typeIndex] = descriptor;
        }

        void DestroyCameraCustomData(RenderCamera& camera) noexcept
        {
            for (u32 index = MaximumCameraCustomDataTypes; index != 0; --index)
            {
                CameraCustomData*& data = camera.customData[index - 1u];
                if (data == nullptr)
                    continue;
                data->Evict();
                cameraCustomData[index - 1u].destroy(data);
                data = nullptr;
            }
        }

        [[nodiscard]] bool CreateCameraCustomData(RenderCamera& camera) noexcept
        {
            for (u32 index = 0; index < MaximumCameraCustomDataTypes; ++index)
            {
                const CameraCustomDataDescriptor& descriptor = cameraCustomData[index];
                if (descriptor.create == nullptr)
                    continue;
                CameraCustomData* const data = descriptor.create();
                if (data == nullptr || data->GetTypeIndex() != index)
                {
                    if (data != nullptr)
                        descriptor.destroy(data);
                    DestroyCameraCustomData(camera);
                    return false;
                }
                camera.customData[index] = data;
                data->Initialize();
            }
            return true;
        }

        void DestroySceneCustomData(SceneState& scene) noexcept
        {
            for (u32 index = MaximumSceneCustomDataTypes; index != 0; --index)
            {
                SceneCustomData*& data = scene.customData[index - 1u];
                if (data == nullptr)
                    continue;
                data->Evict();
                sceneCustomData[index - 1u].destroy(data);
                data = nullptr;
            }
        }

        [[nodiscard]] bool CreateSceneCustomData(SceneState& scene) noexcept
        {
            for (u32 index = 0; index < MaximumSceneCustomDataTypes; ++index)
            {
                const SceneCustomDataDescriptor& descriptor = sceneCustomData[index];
                if (descriptor.create == nullptr)
                    continue;
                SceneCustomData* const data = descriptor.create();
                if (data == nullptr || data->GetTypeIndex() != index)
                {
                    if (data != nullptr)
                        descriptor.destroy(data);
                    DestroySceneCustomData(scene);
                    return false;
                }
                scene.customData[index] = data;
                data->Initialize();
            }
            return true;
        }

        [[nodiscard]] SceneState* FindScene(const RenderSceneHandle scene) noexcept
        {
            SceneState* const state = scene.index < MaximumRenderScenes ? sceneStates[scene.index] : nullptr;
            return state != nullptr && state->scene == scene ? state : nullptr;
        }

        [[nodiscard]] const SceneState* FindScene(const RenderSceneHandle scene) const noexcept
        {
            const SceneState* const state = scene.index < MaximumRenderScenes ? sceneStates[scene.index] : nullptr;
            return state != nullptr && state->scene == scene ? state : nullptr;
        }

        [[nodiscard]] CameraSlot* Find(const RenderCameraHandle camera) noexcept
        {
            SceneState* const scene = FindScene(camera.scene);
            if (scene == nullptr || camera.index >= scene->slots.Size())
                return nullptr;
            CameraSlot& slot = scene->slots[camera.index];
            return slot.active && slot.generation == camera.generation ? &slot : nullptr;
        }

        [[nodiscard]] const CameraSlot* Find(const RenderCameraHandle camera) const noexcept
        {
            const SceneState* const scene = FindScene(camera.scene);
            if (scene == nullptr || camera.index >= scene->slots.Size())
                return nullptr;
            const CameraSlot& slot = scene->slots[camera.index];
            return slot.active && slot.generation == camera.generation ? &slot : nullptr;
        }

        void AddAlwaysCamera(SceneState& scene, CameraSlot& slot) noexcept
        {
            if (slot.denseAlwaysIndex != InvalidCameraIndex)
                return;
            slot.denseAlwaysIndex = scene.alwaysCameras.Size();
            scene.alwaysCameras.PushBackUnchecked(slot.camera.handle.index);
            ++stats.alwaysRenderCameras;
        }

        void RemoveAlwaysCamera(SceneState& scene, CameraSlot& slot) noexcept
        {
            if (slot.denseAlwaysIndex == InvalidCameraIndex)
                return;
            const u32 removedIndex = slot.denseAlwaysIndex;
            const u32 lastIndex = scene.alwaysCameras.Size() - 1u;
            if (removedIndex != lastIndex)
            {
                const u32 movedCamera = scene.alwaysCameras[lastIndex];
                scene.alwaysCameras[removedIndex] = movedCamera;
                scene.slots[movedCamera].denseAlwaysIndex = removedIndex;
            }
            static_cast<void>(scene.alwaysCameras.PopBack());
            slot.denseAlwaysIndex = InvalidCameraIndex;
            --stats.alwaysRenderCameras;
        }

        void RemoveActiveCamera(SceneState& scene, CameraSlot& slot) noexcept
        {
            const u32 removedIndex = slot.denseActiveIndex;
            const u32 lastIndex = scene.activeCameras.Size() - 1u;
            if (removedIndex != lastIndex)
            {
                const u32 movedCamera = scene.activeCameras[lastIndex];
                scene.activeCameras[removedIndex] = movedCamera;
                scene.slots[movedCamera].denseActiveIndex = removedIndex;
            }
            static_cast<void>(scene.activeCameras.PopBack());
            slot.denseActiveIndex = InvalidCameraIndex;
        }

        [[nodiscard]] bool Reaches(SceneState& scene, const u32 start, const u32 target) noexcept
        {
            ++scene.traversalStamp;
            if (scene.traversalStamp == 0)
            {
                for (u32 index = 0; index < scene.discoveredStamps.Size(); ++index)
                    scene.discoveredStamps[index] = 0;
                scene.traversalStamp = 1;
            }
            scene.traversal.Clear();
            scene.traversal.PushBackUnchecked({start, 0, 0});
            scene.discoveredStamps[start] = scene.traversalStamp;
            while (!scene.traversal.Empty())
            {
                const u32 cameraIndex = scene.traversal.Back().cameraIndex;
                static_cast<void>(scene.traversal.PopBack());
                if (cameraIndex == target)
                    return true;
                const RenderCamera& camera = scene.slots[cameraIndex].camera;
                for (u32 index = 0; index < camera.dependencyCount; ++index)
                {
                    const u32 childIndex = camera.dependencies[index].child.index;
                    if (scene.discoveredStamps[childIndex] == scene.traversalStamp)
                        continue;
                    scene.discoveredStamps[childIndex] = scene.traversalStamp;
                    scene.traversal.PushBackUnchecked({childIndex, 0, 0});
                }
            }
            return false;
        }

        [[nodiscard]] u32 FindDependency(const RenderCamera& parent, const RenderCameraHandle child) const noexcept
        {
            for (u32 index = 0; index < parent.dependencyCount; ++index)
                if (parent.dependencies[index].child == child)
                    return index;
            return InvalidCameraIndex;
        }

        [[nodiscard]] u32 FindDependent(const RenderCamera& child, const RenderCameraHandle parent) const noexcept
        {
            for (u32 index = 0; index < child.dependentCount; ++index)
                if (child.dependents[index] == parent)
                    return index;
            return InvalidCameraIndex;
        }

        [[nodiscard]] FrameSlot* FindFrame(const PreparedRenderViewFamily& frame) noexcept
        {
            SceneState* const scene = FindScene(frame.m_scene);
            if (scene == nullptr || frame.m_slot >= scene->frameSlots.Size())
                return nullptr;
            FrameSlot* const slot = scene->frameSlots[frame.m_slot];
            const u32 references = slot != nullptr ? slot->references.GetValue() : 0;
            return slot != nullptr && slot->generation == frame.m_generation && slot->frameSerial == frame.m_frameSerial && references != 0 &&
                           references != ClosingFrameReferences
                       ? slot
                       : nullptr;
        }

        [[nodiscard]] const FrameSlot* FindFrame(const PreparedRenderViewFamily& frame) const noexcept
        {
            const SceneState* const scene = FindScene(frame.m_scene);
            if (scene == nullptr || frame.m_slot >= scene->frameSlots.Size())
                return nullptr;
            const FrameSlot* const slot = scene->frameSlots[frame.m_slot];
            const u32 references = slot != nullptr ? slot->references.GetValue() : 0;
            return slot != nullptr && slot->generation == frame.m_generation && slot->frameSerial == frame.m_frameSerial && references != 0 &&
                           references != ClosingFrameReferences
                       ? slot
                       : nullptr;
        }

        [[nodiscard]] bool BuildView(RenderCamera& camera, const RenderViewFamilyId family, const RenderViewFamilyPrepareRequest& request, RenderView& view,
                                     u64& cutRevision) const noexcept
        {
            view = {};
            const RenderCameraState& state = camera.state;
            RenderCameraExtent frameExtent = request.frameExtent;
            f32 outputAspect = 0.0f;
            for (const auto& region : request.outputRegions)
            {
                if (region.camera != camera.handle) continue;
                if (!request.outputExtent.IsValid() || !region.rect.IsValid() ||
                    region.rect.x >= request.outputExtent.width || region.rect.y >= request.outputExtent.height ||
                    region.rect.width > request.outputExtent.width - region.rect.x ||
                    region.rect.height > request.outputExtent.height - region.rect.y)
                    return false;
                // Scale destination edges, not widths independently: adjacent
                // regions keep the same boundary under an odd render extent.
                const u32 left = static_cast<u32>(static_cast<u64>(region.rect.x) * frameExtent.width / request.outputExtent.width);
                const u32 top = static_cast<u32>(static_cast<u64>(region.rect.y) * frameExtent.height / request.outputExtent.height);
                const u32 right = static_cast<u32>(static_cast<u64>(region.rect.x + region.rect.width) * frameExtent.width / request.outputExtent.width);
                const u32 bottom = static_cast<u32>(static_cast<u64>(region.rect.y + region.rect.height) * frameExtent.height / request.outputExtent.height);
                frameExtent = {right > left ? right - left : 1u, bottom > top ? bottom - top : 1u};
                outputAspect = static_cast<f32>(region.rect.width) / static_cast<f32>(region.rect.height);
                break;
            }
            const RenderCameraExtent extent = ResolveExtent(state, frameExtent);
            if (!extent.IsValid())
                return false;

            const f32 automaticAspect = outputAspect > 0.0f ? outputAspect : static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
            const f32 aspect = state.projection.aspectRatio > 0.0f ? state.projection.aspectRatio : automaticAspect;
            f32 unjitteredProjection[16]{};
            f32 unjitteredWorldToClip[16]{};
            const f32 noJitter[2]{};
            BuildWorldToView(state.pose, view.matrices.worldToView);
            BuildProjection(state.projection, aspect, noJitter, unjitteredProjection);
            Multiply(view.matrices.worldToView, unjitteredProjection, unjitteredWorldToClip);
            BuildFrustum(unjitteredWorldToClip, state.projection.reverseDepth, state.projection.infiniteFarPlane, view.frustum);

            const bool useJitter = state.temporalHistory && state.temporalJitter && request.enableTemporalJitter;
            if (useJitter)
                CalculateJitter(request.jitterIndex, extent, view.jitter);
            BuildProjection(state.projection, aspect, view.jitter, view.matrices.viewToClip);
            Multiply(view.matrices.worldToView, view.matrices.viewToClip, view.matrices.worldToClip);

            const bool cameraCut = request.forceCameraCut || !camera.historyValid || camera.requestedCutRevision != camera.committedCutRevision ||
                                   request.frameSerial <= camera.lastSubmittedFrameSerial ||
                                   extent.width != camera.historyExtent.width || extent.height != camera.historyExtent.height ||
                                   view.matrices.viewToClip[0] != camera.historyMatrices.viewToClip[0] ||
                                   view.matrices.viewToClip[5] != camera.historyMatrices.viewToClip[5];
            view.id = {camera.handle.index, camera.handle.generation};
            view.family = family;
            view.purpose = state.purpose;
            view.flags = state.flags;
            if (state.projection.kind == RenderCameraProjectionKind::Orthographic)
                view.flags = view.flags | RenderViewFlags::Orthographic;
            if (state.projection.reverseDepth)
                view.flags = view.flags | RenderViewFlags::ReverseDepth;
            if (state.temporalHistory)
                view.flags = view.flags | RenderViewFlags::TemporalHistory;
            if (useJitter)
                view.flags = view.flags | RenderViewFlags::Jittered;
            if (state.projection.infiniteFarPlane)
                view.flags = view.flags | RenderViewFlags::InfiniteFarPlane;
            view.phases = state.phases;
            view.origin = state.pose.origin;
            view.previousOrigin = cameraCut ? view.origin : camera.historyOrigin;
            view.rect = {0, 0, extent.width, extent.height};
            view.layerMask = state.layerMask;
            view.visibilityMask = state.visibilityMask;
            view.temporalIdentity = state.temporalHistory ? camera.temporalIdentity : 0;
            view.frameSerial = request.frameSerial;
            view.nearPlane = state.projection.nearPlane;
            view.farPlane = state.projection.infiniteFarPlane ? 0.0f : state.projection.farPlane;
            view.lodBias = state.lodBias;
            for (u32 index = 0; index < 16; ++index)
                view.matrices.previousWorldToClip[index] = cameraCut ? view.matrices.worldToClip[index] : camera.historyMatrices.worldToClip[index];
            view.previousJitter[0] = cameraCut ? view.jitter[0] : camera.historyJitter[0];
            view.previousJitter[1] = cameraCut ? view.jitter[1] : camera.historyJitter[1];
            u32 nameIndex = 0;
            while (nameIndex + 1u < MaximumRenderViewNameBytes && camera.name[nameIndex] != '\0')
            {
                view.name[nameIndex] = camera.name[nameIndex];
                ++nameIndex;
            }
            view.name[nameIndex] = '\0';
            cutRevision = camera.requestedCutRevision;
            return true;
        }
    };

    RenderCameraStorage::~RenderCameraStorage()
    {
        if (m_impl != nullptr)
        {
            static_cast<void>(Shutdown());
            if (m_impl != nullptr)
                VANGUARD_DELETE(m_impl);
        }
    }

    bool RenderCameraStorage::Initialize(RenderSceneManager& scenes, const RenderCameraStorageConfig& config, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderCameraFailureCode::AlreadyInitialized, "RenderCameraStorage is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCameraStorage must initialize on the main thread");
        CustomDataCatalogFailure customDataFailure;
        if (!scenes.IsInitialized() || config.maximumDependencyDepth == 0 || config.maximumDependencyDepth >= MaximumRenderViews ||
            config.maximumFramesInFlight == 0 || config.maximumFramesInFlight > 8 ||
            !ValidateCustomDataCatalog(config.customData, &customDataFailure))
        {
            if (failure != nullptr)
                failure->customDataFailure = customDataFailure;
            return Fail(failure, RenderCameraFailureCode::InvalidConfiguration,
                        "RenderCameraStorage requires an initialized scene manager and a bounded dependency depth");
        }
        m_impl = VANGUARD_NEW(Impl);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCameraStorage allocation failed");
        m_impl->scenes = &scenes;
        m_impl->config = config;
        m_impl->config.customData = {};
        m_impl->CopyCustomDataCatalog(config.customData);
        if (!scenes.AttachCameraStorage(*this))
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return Fail(failure, RenderCameraFailureCode::Busy, "RenderCameraStorage must attach before any RenderScene is alive");
        }
        return true;
    }

    bool RenderCameraStorage::Shutdown(RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCameraStorage must shut down on the main thread");
        if (m_impl->stats.attachedScenes != 0 || m_impl->stats.activeCameras != 0)
            return Fail(failure, RenderCameraFailureCode::CamerasRemainAlive, "RenderCameraStorage shutdown is blocked by live RenderScenes or cameras");
        if (!m_impl->scenes->DetachCameraStorage(*this))
            return Fail(failure, RenderCameraFailureCode::Busy, "RenderCameraStorage detachment failed");
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool RenderCameraStorage::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool RenderCameraStorage::AttachScene(const RenderSceneHandle scene, const u32 maximumCameras) noexcept
    {
        if (m_impl == nullptr || !scene.IsValid() || maximumCameras == 0 || maximumCameras > MaximumRenderViews || m_impl->sceneStates[scene.index] != nullptr)
            return false;
        Impl::SceneState* const state = VANGUARD_NEW(Impl::SceneState);
        if (state == nullptr)
            return false;
        state->scene = scene;
        state->slots.Resize(maximumCameras);
        state->activeCameras.Reserve(maximumCameras);
        state->alwaysCameras.Reserve(maximumCameras);
        state->discoveredStamps.Resize(maximumCameras);
        state->completedStamps.Resize(maximumCameras);
        state->traversal.Reserve(maximumCameras);
        state->preparationCameras.Resize(maximumCameras);
        state->preparationDependencies.Resize(maximumCameras * MaximumRenderCameraDependencies);
        state->frameSlots.Resize(m_impl->config.maximumFramesInFlight);
        for (u32 index = 0; index < state->frameSlots.Size(); ++index)
            state->frameSlots[index] = nullptr;
        const u32 maximumPreparedCameras = maximumCameras < MaximumRenderViewsPerFamily ? maximumCameras : MaximumRenderViewsPerFamily;
        for (u32 index = 0; index < state->frameSlots.Size(); ++index)
        {
            Impl::FrameSlot* const frame = VANGUARD_NEW(Impl::FrameSlot);
            if (frame == nullptr)
            {
                VANGUARD_DELETE(state);
                return false;
            }
            frame->scene = scene;
            frame->cameras.Resize(maximumPreparedCameras);
            frame->views.Resize(maximumPreparedCameras);
            frame->cutRevisions.Resize(maximumPreparedCameras);
            frame->dependencies.Resize(maximumPreparedCameras * MaximumRenderCameraDependencies);
            state->frameSlots[index] = frame;
        }
        for (u32 index = 0; index < maximumCameras; ++index)
        {
            state->slots[index].nextFree = index + 1u < maximumCameras ? index + 1u : InvalidCameraIndex;
            state->discoveredStamps[index] = 0;
            state->completedStamps[index] = 0;
        }
        if (!m_impl->CreateSceneCustomData(*state))
        {
            VANGUARD_DELETE(state);
            return false;
        }
        state->firstFree = 0;
        m_impl->sceneStates[scene.index] = state;
        ++m_impl->stats.attachedScenes;
        return true;
    }

    bool RenderCameraStorage::CanDetachScene(const RenderSceneHandle scene) const noexcept
    {
        const Impl::SceneState* const state = m_impl != nullptr ? m_impl->FindScene(scene) : nullptr;
        return state != nullptr && state->activeCameras.Empty() && state->activePreparedFamilies.GetValue() == 0;
    }

    bool RenderCameraStorage::DetachScene(const RenderSceneHandle scene) noexcept
    {
        if (m_impl == nullptr)
            return false;
        Impl::SceneState* const state = m_impl->FindScene(scene);
        if (state == nullptr || !state->activeCameras.Empty() || state->activePreparedFamilies.GetValue() != 0)
            return false;
        m_impl->DestroySceneCustomData(*state);
        VANGUARD_DELETE(state);
        m_impl->sceneStates[scene.index] = nullptr;
        --m_impl->stats.attachedScenes;
        return true;
    }

    bool RenderCameraStorage::RegisterCamera(const RenderCameraDesc& desc, RenderCameraHandle& camera, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        camera = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera registration must run on the main thread", desc.scene);
        Impl::SceneState* const scene = m_impl->FindScene(desc.scene);
        char name[MaximumRenderCameraNameBytes]{};
        if (scene == nullptr || !CopyName(name, desc.name) || !ValidRenderPolicy(desc.renderPolicy) || !ValidCameraState(desc.state))
        {
            static_cast<void>(m_impl->rejectedOperations.Increment());
            return Fail(failure, scene == nullptr ? RenderCameraFailureCode::InvalidHandle : RenderCameraFailureCode::InvalidDescriptor,
                        "invalid RenderCamera registration descriptor", desc.scene);
        }
        if (scene->firstFree == InvalidCameraIndex)
        {
            static_cast<void>(m_impl->rejectedOperations.Increment());
            return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderScene camera capacity exceeded", desc.scene);
        }

        const u32 index = scene->firstFree;
        Impl::CameraSlot& slot = scene->slots[index];
        const u32 nextFree = slot.nextFree;
        const u32 generation = NextGeneration(slot.generation);
        slot = {};
        slot.generation = generation;
        slot.camera.handle = {desc.scene, index, generation};
        slot.camera.state = desc.state;
        slot.camera.renderPolicy = desc.renderPolicy;
        slot.camera.temporalIdentity = m_impl->nextTemporalIdentity++;
        if (m_impl->nextTemporalIdentity == 0)
            m_impl->nextTemporalIdentity = 1;
        slot.camera.enabled = desc.enabled;
        if (!m_impl->CreateCameraCustomData(slot.camera))
        {
            const RenderCameraHandle failedCamera = slot.camera.handle;
            slot = {};
            slot.generation = generation;
            slot.nextFree = nextFree;
            static_cast<void>(m_impl->rejectedOperations.Increment());
            return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCamera custom-data creation failed", desc.scene, failedCamera);
        }
        scene->firstFree = nextFree;
        slot.active = true;
        slot.denseActiveIndex = scene->activeCameras.Size();
        scene->activeCameras.PushBackUnchecked(index);
        CopyNameUnchecked(slot.camera.name, name);
        if (slot.camera.renderPolicy == RenderCameraRenderPolicy::Always)
            m_impl->AddAlwaysCamera(*scene, slot);
        camera = slot.camera.handle;
        ++m_impl->stats.activeCameras;
        ++m_impl->stats.registeredCameras;
        return true;
    }

    bool RenderCameraStorage::UnregisterCamera(const RenderCameraHandle camera, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", camera.scene, camera);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera unregistration must run on the main thread", camera.scene, camera);
        Impl::SceneState* const scene = m_impl->FindScene(camera.scene);
        Impl::CameraSlot* const slot = m_impl->Find(camera);
        if (scene == nullptr || slot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid or stale RenderCamera handle", camera.scene, camera);
        if (slot->camera.pendingPreparedFamilies != 0)
            return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera unregistration waits for its unresolved prepared view family", camera.scene,
                        camera);
        if (slot->camera.dependentCount != 0)
            return Fail(failure, RenderCameraFailureCode::CameraStillReferenced, "RenderCamera is still required by another camera", camera.scene, camera,
                        slot->camera.dependents[0]);

        while (slot->camera.dependencyCount != 0)
        {
            const RenderCameraHandle childHandle = slot->camera.dependencies[slot->camera.dependencyCount - 1u].child;
            Impl::CameraSlot* const child = m_impl->Find(childHandle);
            if (child == nullptr)
                return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera dependency graph is inconsistent", camera.scene, camera, childHandle);
            const u32 dependentIndex = m_impl->FindDependent(child->camera, camera);
            if (dependentIndex == InvalidCameraIndex)
                return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera reverse dependency graph is inconsistent", camera.scene, camera, childHandle);
            child->camera.dependents[dependentIndex] = child->camera.dependents[child->camera.dependentCount - 1u];
            child->camera.dependents[child->camera.dependentCount - 1u] = {};
            --child->camera.dependentCount;
            slot->camera.dependencies[slot->camera.dependencyCount - 1u] = {};
            --slot->camera.dependencyCount;
            --m_impl->stats.dependencies;
        }

        m_impl->RemoveAlwaysCamera(*scene, *slot);
        m_impl->RemoveActiveCamera(*scene, *slot);
        m_impl->DestroyCameraCustomData(slot->camera);
        const u32 generation = slot->generation;
        *slot = {};
        slot->generation = generation;
        slot->nextFree = scene->firstFree;
        scene->firstFree = camera.index;
        --m_impl->stats.activeCameras;
        ++m_impl->stats.unregisteredCameras;
        return true;
    }

    bool RenderCameraStorage::UpdateCamera(const RenderCameraHandle camera, const RenderCameraState& state, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", camera.scene, camera);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera updates must run on the main thread", camera.scene, camera);
        Impl::CameraSlot* const slot = m_impl->Find(camera);
        if (slot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid or stale RenderCamera handle", camera.scene, camera);
        if (!ValidCameraState(state))
            return Fail(failure, RenderCameraFailureCode::InvalidCameraState, "invalid RenderCamera state", camera.scene, camera);
        if (RequiresCameraCut(slot->camera.state, state))
            slot->camera.requestedCutRevision = NextRevision(slot->camera.requestedCutRevision);
        slot->camera.state = state;
        return true;
    }

    bool RenderCameraStorage::RequestCameraCut(const RenderCameraHandle camera, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", camera.scene, camera);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera cuts must be requested on the main thread", camera.scene, camera);
        Impl::CameraSlot* const slot = m_impl->Find(camera);
        if (slot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid or stale RenderCamera handle", camera.scene, camera);
        slot->camera.requestedCutRevision = NextRevision(slot->camera.requestedCutRevision);
        return true;
    }

    bool RenderCameraStorage::SetEnabled(const RenderCameraHandle camera, const bool enabled, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", camera.scene, camera);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera enable changes must run on the main thread", camera.scene, camera);
        Impl::CameraSlot* const slot = m_impl->Find(camera);
        if (slot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid or stale RenderCamera handle", camera.scene, camera);
        slot->camera.enabled = enabled;
        return true;
    }

    bool RenderCameraStorage::SetRenderPolicy(const RenderCameraHandle camera, const RenderCameraRenderPolicy policy,
                                              RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", camera.scene, camera);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera policy changes must run on the main thread", camera.scene, camera);
        Impl::SceneState* const scene = m_impl->FindScene(camera.scene);
        Impl::CameraSlot* const slot = m_impl->Find(camera);
        if (scene == nullptr || slot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid or stale RenderCamera handle", camera.scene, camera);
        if (!ValidRenderPolicy(policy))
            return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "invalid RenderCamera render policy", camera.scene, camera);
        if (slot->camera.renderPolicy == policy)
            return true;
        if (policy == RenderCameraRenderPolicy::Always)
            m_impl->AddAlwaysCamera(*scene, *slot);
        else
            m_impl->RemoveAlwaysCamera(*scene, *slot);
        slot->camera.renderPolicy = policy;
        return true;
    }

    bool RenderCameraStorage::AddDependency(const RenderCameraHandle parent, const RenderCameraHandle child, const RenderCameraDependencyOutputs outputs,
                                            RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", parent.scene, parent, child);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera dependencies must be changed on the main thread", parent.scene, parent,
                        child);
        if (parent.scene != child.scene)
            return Fail(failure, RenderCameraFailureCode::WrongScene, "RenderCamera dependencies cannot cross RenderScenes", parent.scene, parent, child);
        Impl::SceneState* const scene = m_impl->FindScene(parent.scene);
        Impl::CameraSlot* const parentSlot = m_impl->Find(parent);
        Impl::CameraSlot* const childSlot = m_impl->Find(child);
        if (scene == nullptr || parentSlot == nullptr || childSlot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid RenderCamera dependency endpoint", parent.scene, parent, child);
        if (!ValidOutputs(outputs) || parent == child)
            return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "invalid RenderCamera dependency", parent.scene, parent, child);
        const u32 existing = m_impl->FindDependency(parentSlot->camera, child);
        if (existing != InvalidCameraIndex)
        {
            parentSlot->camera.dependencies[existing].outputs = parentSlot->camera.dependencies[existing].outputs | outputs;
            return true;
        }
        if (parentSlot->camera.dependencyCount == MaximumRenderCameraDependencies || childSlot->camera.dependentCount == MaximumRenderCameraDependencies)
            return Fail(failure, RenderCameraFailureCode::DependencyCapacityExceeded, "RenderCamera dependency degree exceeded", parent.scene, parent, child);
        if (m_impl->Reaches(*scene, child.index, parent.index))
            return Fail(failure, RenderCameraFailureCode::DependencyCycle, "RenderCamera dependency would create a cycle", parent.scene, parent, child);

        parentSlot->camera.dependencies[parentSlot->camera.dependencyCount++] = {parent, child, outputs};
        childSlot->camera.dependents[childSlot->camera.dependentCount++] = parent;
        ++m_impl->stats.dependencies;
        return true;
    }

    bool RenderCameraStorage::RemoveDependency(const RenderCameraHandle parent, const RenderCameraHandle child, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", parent.scene, parent, child);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCameraFailureCode::WrongThread, "RenderCamera dependencies must be changed on the main thread", parent.scene, parent,
                        child);
        if (parent.scene != child.scene)
            return Fail(failure, RenderCameraFailureCode::WrongScene, "RenderCamera dependencies cannot cross RenderScenes", parent.scene, parent, child);
        Impl::CameraSlot* const parentSlot = m_impl->Find(parent);
        Impl::CameraSlot* const childSlot = m_impl->Find(child);
        if (parentSlot == nullptr || childSlot == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid RenderCamera dependency endpoint", parent.scene, parent, child);
        const u32 dependencyIndex = m_impl->FindDependency(parentSlot->camera, child);
        const u32 dependentIndex = m_impl->FindDependent(childSlot->camera, parent);
        if (dependencyIndex == InvalidCameraIndex || dependentIndex == InvalidCameraIndex)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "RenderCamera dependency does not exist", parent.scene, parent, child);
        parentSlot->camera.dependencies[dependencyIndex] = parentSlot->camera.dependencies[parentSlot->camera.dependencyCount - 1u];
        parentSlot->camera.dependencies[parentSlot->camera.dependencyCount - 1u] = {};
        --parentSlot->camera.dependencyCount;
        childSlot->camera.dependents[dependentIndex] = childSlot->camera.dependents[childSlot->camera.dependentCount - 1u];
        childSlot->camera.dependents[childSlot->camera.dependentCount - 1u] = {};
        --childSlot->camera.dependentCount;
        --m_impl->stats.dependencies;
        return true;
    }

    bool RenderCameraStorage::HasDependency(const RenderCameraHandle parent, const RenderCameraHandle child,
                                            RenderCameraDependencyOutputs* const outputs) const noexcept
    {
        if (outputs != nullptr)
            *outputs = RenderCameraDependencyOutputs::None;
        if (m_impl == nullptr || parent.scene != child.scene)
            return false;
        const Impl::CameraSlot* const parentSlot = m_impl->Find(parent);
        if (parentSlot == nullptr || m_impl->Find(child) == nullptr)
            return false;
        const u32 dependencyIndex = m_impl->FindDependency(parentSlot->camera, child);
        if (dependencyIndex == InvalidCameraIndex)
            return false;
        if (outputs != nullptr)
            *outputs = parentSlot->camera.dependencies[dependencyIndex].outputs;
        return true;
    }

    bool RenderCameraStorage::BuildDependencyPlan(const RenderSceneHandle sceneHandle, const containers::ArraySpan<const RenderCameraHandle> roots,
                                                  const containers::ArraySpan<RenderCameraHandle> cameraStorage,
                                                  const containers::ArraySpan<RenderCameraDependency> dependencyStorage, RenderCameraDependencyPlan& plan,
                                                  RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        plan = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", sceneHandle);
        Impl::SceneState* const scene = m_impl->FindScene(sceneHandle);
        if (scene == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid RenderScene camera storage", sceneHandle);

        ++scene->traversalStamp;
        if (scene->traversalStamp == 0)
        {
            for (u32 index = 0; index < scene->slots.Size(); ++index)
            {
                scene->discoveredStamps[index] = 0;
                scene->completedStamps[index] = 0;
            }
            scene->traversalStamp = 1;
        }
        const u32 stamp = scene->traversalStamp;
        u32 cameraCount = 0;

        const auto appendRoot = [&](const RenderCameraHandle root) noexcept -> bool
        {
            Impl::CameraSlot* const rootSlot = m_impl->Find(root);
            if (root.scene != sceneHandle || rootSlot == nullptr)
                return Fail(failure, root.scene != sceneHandle ? RenderCameraFailureCode::WrongScene : RenderCameraFailureCode::InvalidHandle,
                            "invalid RenderCamera plan root", sceneHandle, root);
            if (!rootSlot->camera.enabled)
                return true;
            if (scene->completedStamps[root.index] == stamp)
                return true;

            scene->traversal.Clear();
            scene->traversal.PushBackUnchecked({root.index, 0, 0});
            while (!scene->traversal.Empty())
            {
                Impl::TraversalFrame& frame = scene->traversal.Back();
                Impl::RenderCamera& camera = scene->slots[frame.cameraIndex].camera;
                if (scene->discoveredStamps[frame.cameraIndex] != stamp)
                    scene->discoveredStamps[frame.cameraIndex] = stamp;
                if (frame.depth > m_impl->config.maximumDependencyDepth)
                    return Fail(failure, RenderCameraFailureCode::DependencyDepthExceeded, "RenderCamera dependency depth exceeded", sceneHandle,
                                camera.handle);
                if (frame.nextDependency < camera.dependencyCount)
                {
                    const RenderCameraHandle child = camera.dependencies[frame.nextDependency++].child;
                    Impl::CameraSlot* const childSlot = m_impl->Find(child);
                    if (childSlot == nullptr)
                        return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera dependency graph is inconsistent", sceneHandle, camera.handle, child);
                    if (!childSlot->camera.enabled)
                        return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "required RenderCamera dependency is disabled", sceneHandle,
                                    camera.handle, child);
                    if (scene->completedStamps[child.index] == stamp)
                        continue;
                    if (scene->discoveredStamps[child.index] == stamp)
                        return Fail(failure, RenderCameraFailureCode::DependencyCycle, "RenderCamera dependency graph contains a cycle", sceneHandle,
                                    camera.handle, child);
                    if (scene->traversal.Size() == scene->traversal.Capacity())
                        return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCamera traversal storage exhausted", sceneHandle, child);
                    scene->traversal.PushBackUnchecked({child.index, 0, frame.depth + 1u});
                    continue;
                }
                if (cameraCount == cameraStorage.Size())
                    return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCamera plan output storage is too small", sceneHandle,
                                camera.handle);
                cameraStorage[cameraCount++] = camera.handle;
                scene->completedStamps[frame.cameraIndex] = stamp;
                static_cast<void>(scene->traversal.PopBack());
            }
            return true;
        };

        for (const RenderCameraHandle root : roots)
            if (!appendRoot(root))
                return false;
        for (const u32 cameraIndex : scene->alwaysCameras)
            if (!appendRoot(scene->slots[cameraIndex].camera.handle))
                return false;

        u32 dependencyCount = 0;
        for (u32 cameraIndex = 0; cameraIndex < cameraCount; ++cameraIndex)
        {
            const Impl::RenderCamera& camera = scene->slots[cameraStorage[cameraIndex].index].camera;
            for (u32 dependencyIndex = 0; dependencyIndex < camera.dependencyCount; ++dependencyIndex)
            {
                if (dependencyCount == dependencyStorage.Size())
                    return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCamera dependency output storage is too small", sceneHandle,
                                camera.handle);
                dependencyStorage[dependencyCount++] = camera.dependencies[dependencyIndex];
            }
        }

        plan.scene = sceneHandle;
        plan.cameras = containers::ArraySpan<const RenderCameraHandle>(cameraStorage.Data(), cameraCount);
        plan.dependencies = containers::ArraySpan<const RenderCameraDependency>(dependencyStorage.Data(), dependencyCount);
        static_cast<void>(m_impl->preparedPlans.Increment());
        return true;
    }
    bool RenderCameraStorage::PrepareViewFamily(const RenderViewFamilyPrepareRequest& request, PreparedRenderViewFamily& frame,
                                                RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", request.scene);
        if (frame.m_owner != nullptr)
            return Fail(failure, RenderCameraFailureCode::PreparedFamilyUnavailable, "prepared RenderViewFamily output is already occupied", request.scene);
        if (!request.scene.IsValid() || !request.frameExtent.IsValid() || request.frameSerial == 0 ||
            request.roots.Size() > MaximumRenderViewsPerFamily || (!request.roots.Empty() && request.roots.Data() == nullptr) ||
            request.outputRegions.Size() > MaximumRenderViewsPerFamily ||
            (!request.outputRegions.Empty() && (request.outputRegions.Data() == nullptr || !request.outputExtent.IsValid())))
        {
            static_cast<void>(m_impl->rejectedOperations.Increment());
            return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "invalid RenderCamera frame preparation request", request.scene);
        }

        for (u32 index = 0; index < request.outputRegions.Size(); ++index)
        {
            const auto& region = request.outputRegions[index];
            bool isRoot = false;
            for (const auto root : request.roots) isRoot |= root == region.camera;
            if (!isRoot || !region.rect.IsValid() || region.rect.x >= request.outputExtent.width || region.rect.y >= request.outputExtent.height ||
                region.rect.width > request.outputExtent.width - region.rect.x || region.rect.height > request.outputExtent.height - region.rect.y)
                return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "invalid camera output region", request.scene);
            for (u32 previous = 0; previous < index; ++previous)
                if (request.outputRegions[previous].camera == region.camera)
                    return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "duplicate camera output region", request.scene);
        }

        Impl::SceneState* const scene = m_impl->FindScene(request.scene);
        if (scene == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid RenderScene camera storage", request.scene);
        RenderSceneSnapshot sceneSnapshot;
        if (!m_impl->scenes->GetSnapshot(request.scene, sceneSnapshot) || sceneSnapshot.state != RenderSceneState::Alive || sceneSnapshot.createdSerial == 0 ||
            sceneSnapshot.completedMutationEpoch == 0 || !m_impl->scenes->IsGpuPublicationReady(request.scene, sceneSnapshot.completedMutationEpoch))
            return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera preparation requires the exact completed RenderScene processing epoch",
                        request.scene);
        RenderCameraDependencyPlan plan;
        if (!BuildDependencyPlan(request.scene, request.roots, {scene->preparationCameras.TypedData(), scene->preparationCameras.Size()},
                                 {scene->preparationDependencies.TypedData(), scene->preparationDependencies.Size()}, plan, failure))
            return false;
        if (!plan.IsValid())
            return Fail(failure, RenderCameraFailureCode::InvalidDescriptor, "RenderCamera frame contains no enabled cameras", request.scene);
        if (plan.cameras.Size() > MaximumRenderViewsPerFamily)
            return Fail(failure, RenderCameraFailureCode::CapacityExceeded, "RenderCamera frame exceeds the view-family capacity", request.scene);
        for (const RenderCameraHandle cameraHandle : plan.cameras)
        {
            const Impl::CameraSlot* const camera = m_impl->Find(cameraHandle);
            if (camera == nullptr)
                return Fail(failure, RenderCameraFailureCode::InvalidHandle, "RenderCamera dependency plan became stale", request.scene, cameraHandle);
            if (camera->camera.pendingPreparedFamilies != 0)
                return Fail(failure, RenderCameraFailureCode::Busy, "RenderCamera already belongs to an unresolved prepared view family", request.scene,
                            cameraHandle);
        }

        Impl::FrameSlot* preparedFamily = nullptr;
        u32 preparedFamilyIndex = 0;
        for (; preparedFamilyIndex < scene->frameSlots.Size(); ++preparedFamilyIndex)
        {
            Impl::FrameSlot* const candidate = scene->frameSlots[preparedFamilyIndex];
            if (candidate->references.CompareExchange(1u, 0u) == 0u)
            {
                preparedFamily = candidate;
                break;
            }
        }
        if (preparedFamily == nullptr)
        {
            static_cast<void>(m_impl->rejectedOperations.Increment());
            return Fail(failure, RenderCameraFailureCode::PreparedFamilyUnavailable, "prepared RenderViewFamily capacity is exhausted", request.scene);
        }

        preparedFamily->generation = NextGeneration(preparedFamily->generation);
        preparedFamily->frameSerial = request.frameSerial;
        preparedFamily->cameraCount = plan.cameras.Size();
        preparedFamily->dependencyCount = plan.dependencies.Size();
        preparedFamily->disposition.SetValue(static_cast<u32>(Impl::FrameDisposition::Open));
        const RenderViewFamilyId familyId{request.scene.index, request.scene.generation};
        for (u32 index = 0; index < preparedFamily->cameraCount; ++index)
        {
            const RenderCameraHandle cameraHandle = plan.cameras[index];
            Impl::CameraSlot* const camera = m_impl->Find(cameraHandle);
            preparedFamily->cameras[index] = cameraHandle;
            if (camera == nullptr || !m_impl->BuildView(camera->camera, familyId, request, preparedFamily->views[index], preparedFamily->cutRevisions[index]))
            {
                preparedFamily->references.SetValue(0);
                return Fail(failure, RenderCameraFailureCode::InvalidCameraState, "RenderCamera view preparation failed", request.scene, cameraHandle);
            }
        }
        for (u32 index = 0; index < preparedFamily->dependencyCount; ++index)
            preparedFamily->dependencies[index] = plan.dependencies[index];
        for (u32 index = 0; index < preparedFamily->cameraCount; ++index)
        {
            Impl::RenderCamera& camera = m_impl->Find(preparedFamily->cameras[index])->camera;
            ++camera.pendingPreparedFamilies;
            camera.usedInRender = true;
        }

        preparedFamily->family.id = familyId;
        preparedFamily->family.views = preparedFamily->views.TypedData();
        preparedFamily->family.viewCount = preparedFamily->cameraCount;
        preparedFamily->family.frameSerial = request.frameSerial;
        preparedFamily->family.sceneIdentity = sceneSnapshot.createdSerial;
        preparedFamily->family.sceneVersion = sceneSnapshot.completedMutationEpoch;
        static_cast<void>(scene->activePreparedFamilies.Increment());

        frame.m_owner = this;
        frame.m_scene = request.scene;
        frame.m_slot = preparedFamilyIndex;
        frame.m_generation = preparedFamily->generation;
        frame.m_frameSerial = request.frameSerial;
        static_cast<void>(m_impl->preparedFrames.Increment());
        return true;
    }

    bool RenderCameraStorage::PrepareCustomData(RenderNodeImplContext& context, RenderCameraFailure* const failure) noexcept
    {
        ClearFailure(failure);
        const PreparedRenderViewFamily* const family = context.GetViewFamily();
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized");
        if (family == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "custom-data preparation requires a prepared RenderViewFamily");

        Impl::SceneState* const scene = m_impl->FindScene(family->m_scene);
        Impl::FrameSlot* const frame = m_impl->FindFrame(*family);
        if (scene == nullptr || frame == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid prepared RenderViewFamily", family->m_scene);

        const auto failCustomData = [failure, frame](const CustomDataKind kind, const u32 typeIndex, const RenderCameraHandle camera) noexcept
        {
            if (failure != nullptr && failure->code == RenderCameraFailureCode::None)
                static_cast<void>(Fail(failure, RenderCameraFailureCode::CustomDataPreparationFailed, "custom-data preparation failed", frame->scene, camera));
            if (failure != nullptr)
            {
                failure->customDataKind = kind;
                failure->customDataTypeIndex = typeIndex;
            }
            return false;
        };

        context.SetCustomDataView(nullptr, -1);
        for (SceneCustomData* const data : scene->customData)
        {
            if (data == nullptr)
                continue;
            if (!data->Prepare(context, failure))
            {
                context.SetCustomDataView(nullptr, -1);
                return failCustomData(CustomDataKind::Scene, data->GetTypeIndex(), {});
            }
            data->MarkPrepared(frame->frameSerial);
        }

        for (u32 cameraIndex = 0; cameraIndex < frame->cameraCount; ++cameraIndex)
        {
            const RenderCameraHandle cameraHandle = frame->cameras[cameraIndex];
            Impl::CameraSlot* const camera = m_impl->Find(cameraHandle);
            if (camera == nullptr)
            {
                context.SetCustomDataView(nullptr, -1);
                return Fail(failure, RenderCameraFailureCode::InvalidHandle, "prepared RenderCamera became unavailable", frame->scene, cameraHandle);
            }

            context.SetCustomDataView(&frame->views[cameraIndex], static_cast<i32>(cameraIndex));
            for (CameraCustomData* const data : camera->camera.customData)
            {
                if (data == nullptr)
                    continue;
                if (!data->Prepare(context, frame->views[cameraIndex], failure))
                {
                    context.SetCustomDataView(nullptr, -1);
                    return failCustomData(CustomDataKind::Camera, data->GetTypeIndex(), cameraHandle);
                }
                data->MarkPrepared(frame->frameSerial);
            }
        }
        context.SetCustomDataView(nullptr, -1);
        return true;
    }

    bool RenderCameraStorage::CheckCustomDataReadiness(const PreparedRenderViewFamily& family, RenderCameraFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderCameraFailureCode::NotInitialized, "RenderCameraStorage is not initialized", family.m_scene);

        const Impl::SceneState* const scene = m_impl->FindScene(family.m_scene);
        const Impl::FrameSlot* const frame = m_impl->FindFrame(family);
        if (scene == nullptr || frame == nullptr)
            return Fail(failure, RenderCameraFailureCode::InvalidHandle, "invalid prepared RenderViewFamily", family.m_scene);

        const auto failCustomData = [failure, frame](const CustomDataKind kind, const u32 typeIndex, const RenderCameraHandle camera,
                                                     const char* const message) noexcept
        {
            static_cast<void>(Fail(failure, RenderCameraFailureCode::CustomDataNotReady, message, frame->scene, camera));
            if (failure != nullptr)
            {
                failure->customDataKind = kind;
                failure->customDataTypeIndex = typeIndex;
            }
            return false;
        };
        const CustomDataPriority priorities[]{CustomDataPriority::High, CustomDataPriority::Normal, CustomDataPriority::Low};

        for (const CustomDataPriority priority : priorities)
        {
            for (u32 cameraIndex = 0; cameraIndex < frame->cameraCount; ++cameraIndex)
            {
                const RenderCameraHandle cameraHandle = frame->cameras[cameraIndex];
                const Impl::CameraSlot* const camera = m_impl->Find(cameraHandle);
                if (camera == nullptr)
                    return Fail(failure, RenderCameraFailureCode::InvalidHandle, "prepared RenderCamera became unavailable", frame->scene, cameraHandle);

                for (u32 typeIndex = 0; typeIndex < MaximumCameraCustomDataTypes; ++typeIndex)
                {
                    const CameraCustomData* const data = camera->camera.customData[typeIndex];
                    if (data == nullptr)
                        continue;
                    const CustomDataPriority dataPriority = data->GetPriority();
                    if (!ValidCustomDataPriority(dataPriority))
                        return failCustomData(CustomDataKind::Camera, typeIndex, cameraHandle, "camera custom data has an invalid priority");
                    if (dataPriority != priority)
                        continue;
                    if (!data->IsPreparedFor(frame->frameSerial))
                        return failCustomData(CustomDataKind::Camera, typeIndex, cameraHandle,
                                              "camera custom data was not prepared for the current frame");
                    if (const char* const reason = data->GetRenderingBlockReason(); reason != nullptr)
                        return failCustomData(CustomDataKind::Camera, typeIndex, cameraHandle, reason);
                }
            }

            for (u32 typeIndex = 0; typeIndex < MaximumSceneCustomDataTypes; ++typeIndex)
            {
                const SceneCustomData* const data = scene->customData[typeIndex];
                if (data == nullptr)
                    continue;
                const CustomDataPriority dataPriority = data->GetPriority();
                if (!ValidCustomDataPriority(dataPriority))
                    return failCustomData(CustomDataKind::Scene, typeIndex, {}, "scene custom data has an invalid priority");
                if (dataPriority != priority)
                    continue;
                if (!data->IsPreparedFor(frame->frameSerial))
                    return failCustomData(CustomDataKind::Scene, typeIndex, {}, "scene custom data was not prepared for the current frame");
                if (const char* const reason = data->GetRenderingBlockReason(); reason != nullptr)
                    return failCustomData(CustomDataKind::Scene, typeIndex, {}, reason);
            }
        }
        return true;
    }

    const char* RenderCameraStorage::GetRenderingBlockReason(const RenderSceneHandle sceneHandle) const noexcept
    {
        const Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(sceneHandle) : nullptr;
        if (scene == nullptr)
            return "RenderScene camera custom-data storage is unavailable";

        // Preparation is checked before readiness queries are ordered by priority. Vanguard has no
        // final presentation viewport on a camera yet, so enabled cameras used by a family are eligible.
        for (const u32 cameraIndex : scene->activeCameras)
        {
            const Impl::RenderCamera& camera = scene->slots[cameraIndex].camera;
            if (!camera.usedInRender || !camera.enabled)
                continue;
            for (const CameraCustomData* const data : camera.customData)
                if (data != nullptr && !data->HasBeenPrepared())
                    return "Camera custom data not prepared";
        }
        for (const SceneCustomData* const data : scene->customData)
            if (data != nullptr && !data->HasBeenPrepared())
                return "Custom scene data not prepared";

        constexpr CustomDataPriority priorities[]{CustomDataPriority::High, CustomDataPriority::Normal, CustomDataPriority::Low};
        for (const CustomDataPriority priority : priorities)
        {
            for (const u32 cameraIndex : scene->activeCameras)
            {
                const Impl::RenderCamera& camera = scene->slots[cameraIndex].camera;
                if (!camera.usedInRender || !camera.enabled)
                    continue;
                for (const CameraCustomData* const data : camera.customData)
                {
                    if (data == nullptr)
                        continue;
                    const CustomDataPriority dataPriority = data->GetPriority();
                    if (!ValidCustomDataPriority(dataPriority))
                        return "Camera custom data has an invalid priority";
                    if (dataPriority == priority)
                        if (const char* const reason = data->GetRenderingBlockReason(); reason != nullptr)
                            return reason;
                }
            }
            for (const SceneCustomData* const data : scene->customData)
            {
                if (data == nullptr)
                    continue;
                const CustomDataPriority dataPriority = data->GetPriority();
                if (!ValidCustomDataPriority(dataPriority))
                    return "Scene custom data has an invalid priority";
                if (dataPriority == priority)
                    if (const char* const reason = data->GetRenderingBlockReason(); reason != nullptr)
                        return reason;
            }
        }
        return nullptr;
    }

    void RenderCameraStorage::TickWhileLoading(const RenderSceneHandle sceneHandle, const bool isFirstFrame) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(sceneHandle) : nullptr;
        if (scene == nullptr)
            return;

        // Keep the established order and coverage: scene data first, then every registered camera. Loading
        // ticks are progress hooks, so they are deliberately not filtered or priority-sorted.
        for (SceneCustomData* const data : scene->customData)
            if (data != nullptr)
                data->TickWhileLoading(isFirstFrame);
        for (const u32 cameraIndex : scene->activeCameras)
            for (CameraCustomData* const data : scene->slots[cameraIndex].camera.customData)
                if (data != nullptr)
                    data->TickWhileLoading(isFirstFrame);
    }

    const SceneCustomData* RenderCameraStorage::GetFrameSceneCustomData(const PreparedRenderViewFamily& family, const u32 typeIndex) const noexcept
    {
        if (m_impl == nullptr || typeIndex >= MaximumSceneCustomDataTypes)
            return nullptr;
        const Impl::FrameSlot* const frame = m_impl->FindFrame(family);
        const Impl::SceneState* const scene = m_impl->FindScene(family.m_scene);
        if (frame == nullptr || scene == nullptr)
            return nullptr;
        const SceneCustomData* const data = scene->customData[typeIndex];
        return data != nullptr && data->IsPreparedFor(frame->frameSerial) ? data : nullptr;
    }

    const CameraCustomData* RenderCameraStorage::GetFrameCameraCustomData(const PreparedRenderViewFamily& family, const RenderViewId view,
                                                                          const u32 typeIndex) const noexcept
    {
        if (m_impl == nullptr || !view.IsValid() || typeIndex >= MaximumCameraCustomDataTypes)
            return nullptr;
        const Impl::FrameSlot* const frame = m_impl->FindFrame(family);
        if (frame == nullptr)
            return nullptr;

        for (u32 viewIndex = 0; viewIndex < frame->cameraCount; ++viewIndex)
        {
            if (frame->views[viewIndex].id != view)
                continue;
            const Impl::CameraSlot* const camera = m_impl->Find(frame->cameras[viewIndex]);
            if (camera == nullptr)
                return nullptr;
            const CameraCustomData* const data = camera->camera.customData[typeIndex];
            return data != nullptr && data->IsPreparedFor(frame->frameSerial) ? data : nullptr;
        }
        return nullptr;
    }

    bool RenderCameraStorage::IsFrameValid(const PreparedRenderViewFamily& frame) const noexcept
    {
        return frame.m_owner == this && m_impl != nullptr && m_impl->FindFrame(frame) != nullptr;
    }

    const RenderViewFamily& RenderCameraStorage::GetFrameFamily(const PreparedRenderViewFamily& frame) const noexcept
    {
        static const RenderViewFamily invalid;
        const Impl::FrameSlot* const slot = IsFrameValid(frame) ? m_impl->FindFrame(frame) : nullptr;
        return slot != nullptr ? slot->family : invalid;
    }

    containers::ArraySpan<const RenderView> RenderCameraStorage::GetFrameViews(const PreparedRenderViewFamily& frame) const noexcept
    {
        const Impl::FrameSlot* const slot = IsFrameValid(frame) ? m_impl->FindFrame(frame) : nullptr;
        return slot != nullptr ? containers::ArraySpan<const RenderView>(slot->views.TypedData(), slot->cameraCount)
                               : containers::ArraySpan<const RenderView>{};
    }

    containers::ArraySpan<const RenderCameraDependency> RenderCameraStorage::GetFrameDependencies(const PreparedRenderViewFamily& frame) const noexcept
    {
        const Impl::FrameSlot* const slot = IsFrameValid(frame) ? m_impl->FindFrame(frame) : nullptr;
        return slot != nullptr ? containers::ArraySpan<const RenderCameraDependency>(slot->dependencies.TypedData(), slot->dependencyCount)
                               : containers::ArraySpan<const RenderCameraDependency>{};
    }

    bool RenderCameraStorage::RetainFrame(const PreparedRenderViewFamily& frame) noexcept
    {
        Impl::FrameSlot* const slot = frame.m_owner == this && m_impl != nullptr ? m_impl->FindFrame(frame) : nullptr;
        if (slot == nullptr)
            return false;
        u32 references = slot->references.GetValue();
        while (references != 0 && references != ClosingFrameReferences)
        {
            if (references + 1u == ClosingFrameReferences)
                return false;
            const u32 observed = slot->references.CompareExchange(references + 1u, references);
            if (observed == references)
                return true;
            references = observed;
        }
        return false;
    }

    void RenderCameraStorage::CommitFrame(const PreparedRenderViewFamily& frame) noexcept
    {
        if (frame.m_owner != this || m_impl == nullptr)
            return;
        Impl::FrameSlot* const slot = m_impl->FindFrame(frame);
        if (slot == nullptr ||
            slot->disposition.CompareExchange(static_cast<u32>(Impl::FrameDisposition::Committing), static_cast<u32>(Impl::FrameDisposition::Open)) !=
                static_cast<u32>(Impl::FrameDisposition::Open))
            return;
        for (u32 index = 0; index < slot->cameraCount; ++index)
        {
            Impl::CameraSlot* const camera = m_impl->Find(slot->cameras[index]);
            if (camera == nullptr)
                continue;
            Impl::RenderCamera& storedCamera = camera->camera;
            if (slot->frameSerial > storedCamera.lastSubmittedFrameSerial)
            {
                const RenderView& view = slot->views[index];
                storedCamera.historyOrigin = view.origin;
                storedCamera.historyMatrices = view.matrices;
                storedCamera.historyExtent = {view.rect.width, view.rect.height};
                storedCamera.historyJitter[0] = view.jitter[0];
                storedCamera.historyJitter[1] = view.jitter[1];
                storedCamera.lastSubmittedFrameSerial = slot->frameSerial;
                storedCamera.committedCutRevision = slot->cutRevisions[index];
                storedCamera.historyValid = true;
            }
            if (storedCamera.pendingPreparedFamilies != 0)
                --storedCamera.pendingPreparedFamilies;
        }
        slot->disposition.SetValue(static_cast<u32>(Impl::FrameDisposition::Committed));
        static_cast<void>(m_impl->committedFrames.Increment());
    }

    void RenderCameraStorage::ReleaseFrame(PreparedRenderViewFamily& frame) noexcept
    {
        if (frame.m_owner != this || m_impl == nullptr)
        {
            frame.m_owner = nullptr;
            frame.m_scene = {};
            frame.m_slot = ~u32{0};
            frame.m_generation = 0;
            frame.m_frameSerial = 0;
            return;
        }
        Impl::FrameSlot* const slot = m_impl->FindFrame(frame);
        Impl::SceneState* const scene = m_impl->FindScene(frame.m_scene);
        if (slot == nullptr || scene == nullptr)
        {
            frame.m_owner = nullptr;
            frame.m_scene = {};
            frame.m_slot = ~u32{0};
            frame.m_generation = 0;
            frame.m_frameSerial = 0;
            return;
        }
        u32 references = slot->references.GetValue();
        while (references != 0 && references != ClosingFrameReferences)
        {
            const u32 next = references == 1u ? ClosingFrameReferences : references - 1u;
            const u32 observed = slot->references.CompareExchange(next, references);
            if (observed == references)
                break;
            references = observed;
        }
        frame.m_owner = nullptr;
        frame.m_scene = {};
        frame.m_slot = ~u32{0};
        frame.m_generation = 0;
        frame.m_frameSerial = 0;
        if (references == 0 || references == ClosingFrameReferences || references > 1u)
            return;
        if (slot->disposition.GetValue() != static_cast<u32>(Impl::FrameDisposition::Committed))
        {
            for (u32 index = 0; index < slot->cameraCount; ++index)
            {
                Impl::CameraSlot* const camera = m_impl->Find(slot->cameras[index]);
                if (camera != nullptr && camera->camera.pendingPreparedFamilies != 0)
                    --camera->camera.pendingPreparedFamilies;
            }
            static_cast<void>(m_impl->abandonedFrames.Increment());
        }
        static_cast<void>(scene->activePreparedFamilies.Decrement());
        slot->references.SetValue(0);
    }

    PreparedRenderViewFamily::PreparedRenderViewFamily(const PreparedRenderViewFamily& other) noexcept
        : m_owner(other.m_owner), m_scene(other.m_scene), m_slot(other.m_slot), m_generation(other.m_generation), m_frameSerial(other.m_frameSerial)
    {
        if (m_owner != nullptr && !m_owner->RetainFrame(other))
        {
            m_owner = nullptr;
            m_scene = {};
            m_slot = ~u32{0};
            m_generation = 0;
            m_frameSerial = 0;
        }
    }

    PreparedRenderViewFamily::PreparedRenderViewFamily(PreparedRenderViewFamily&& other) noexcept
        : m_owner(other.m_owner), m_scene(other.m_scene), m_slot(other.m_slot), m_generation(other.m_generation), m_frameSerial(other.m_frameSerial)
    {
        other.m_owner = nullptr;
        other.m_scene = {};
        other.m_slot = ~u32{0};
        other.m_generation = 0;
        other.m_frameSerial = 0;
    }

    PreparedRenderViewFamily& PreparedRenderViewFamily::operator=(const PreparedRenderViewFamily& other) noexcept
    {
        if (this == &other)
            return *this;
        PreparedRenderViewFamily retained(other);
        return *this = static_cast<PreparedRenderViewFamily&&>(retained);
    }

    PreparedRenderViewFamily& PreparedRenderViewFamily::operator=(PreparedRenderViewFamily&& other) noexcept
    {
        if (this == &other)
            return *this;
        Release();
        m_owner = other.m_owner;
        m_scene = other.m_scene;
        m_slot = other.m_slot;
        m_generation = other.m_generation;
        m_frameSerial = other.m_frameSerial;
        other.m_owner = nullptr;
        other.m_scene = {};
        other.m_slot = ~u32{0};
        other.m_generation = 0;
        other.m_frameSerial = 0;
        return *this;
    }

    PreparedRenderViewFamily::~PreparedRenderViewFamily()
    {
        Release();
    }

    bool PreparedRenderViewFamily::IsValid() const noexcept
    {
        return m_owner != nullptr && m_owner->IsFrameValid(*this);
    }

    RenderSceneHandle PreparedRenderViewFamily::GetScene() const noexcept
    {
        return IsValid() ? m_scene : RenderSceneHandle{};
    }

    u64 PreparedRenderViewFamily::GetFrameSerial() const noexcept
    {
        return IsValid() ? m_frameSerial : 0;
    }

    const RenderViewFamily& PreparedRenderViewFamily::GetFamily() const noexcept
    {
        static const RenderViewFamily invalid;
        return m_owner != nullptr ? m_owner->GetFrameFamily(*this) : invalid;
    }

    containers::ArraySpan<const RenderView> PreparedRenderViewFamily::GetViews() const noexcept
    {
        return m_owner != nullptr ? m_owner->GetFrameViews(*this) : containers::ArraySpan<const RenderView>{};
    }

    containers::ArraySpan<const RenderCameraDependency> PreparedRenderViewFamily::GetDependencies() const noexcept
    {
        return m_owner != nullptr ? m_owner->GetFrameDependencies(*this) : containers::ArraySpan<const RenderCameraDependency>{};
    }

    void PreparedRenderViewFamily::Commit() const noexcept
    {
        if (m_owner != nullptr)
            m_owner->CommitFrame(*this);
    }

    void PreparedRenderViewFamily::Release() noexcept
    {
        RenderCameraStorage* const owner = m_owner;
        if (owner != nullptr)
            owner->ReleaseFrame(*this);
        else
        {
            m_scene = {};
            m_slot = ~u32{0};
            m_generation = 0;
            m_frameSerial = 0;
        }
    }

    bool FrameCustomData::IsValid() const noexcept
    {
        return m_family.IsValid();
    }

    RenderSceneHandle FrameCustomData::GetScene() const noexcept
    {
        return m_family.GetScene();
    }

    u64 FrameCustomData::GetFrameSerial() const noexcept
    {
        return m_family.GetFrameSerial();
    }

    const SceneCustomData* FrameCustomData::GetSceneData(const u32 typeIndex) const noexcept
    {
        return m_family.m_owner != nullptr ? m_family.m_owner->GetFrameSceneCustomData(m_family, typeIndex) : nullptr;
    }

    const CameraCustomData* FrameCustomData::GetCameraData(const RenderViewId view, const u32 typeIndex) const noexcept
    {
        return m_family.m_owner != nullptr ? m_family.m_owner->GetFrameCameraCustomData(m_family, view, typeIndex) : nullptr;
    }

    bool RenderCameraStorage::IsAlive(const RenderCameraHandle camera) const noexcept
    {
        return m_impl != nullptr && m_impl->Find(camera) != nullptr;
    }

    bool RenderCameraStorage::GetSnapshot(const RenderCameraHandle camera, RenderCameraSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        const Impl::CameraSlot* const slot = m_impl != nullptr ? m_impl->Find(camera) : nullptr;
        if (slot == nullptr)
            return false;
        const Impl::RenderCamera& storedCamera = slot->camera;
        snapshot.handle = storedCamera.handle;
        snapshot.renderPolicy = storedCamera.renderPolicy;
        snapshot.state = storedCamera.state;
        snapshot.dependencyCount = storedCamera.dependencyCount;
        snapshot.dependentCount = storedCamera.dependentCount;
        snapshot.enabled = storedCamera.enabled;
        CopyNameUnchecked(snapshot.name, storedCamera.name);
        return true;
    }

    RenderCameraStorageStats RenderCameraStorage::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        RenderCameraStorageStats stats = m_impl->stats;
        stats.preparedPlans = m_impl->preparedPlans.GetValue();
        stats.preparedFrames = m_impl->preparedFrames.GetValue();
        stats.committedFrames = m_impl->committedFrames.GetValue();
        stats.abandonedFrames = m_impl->abandonedFrames.GetValue();
        stats.rejectedOperations = m_impl->rejectedOperations.GetValue();
        return stats;
    }
} // namespace vanguard::rendering
