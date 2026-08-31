#include <vanguard/rendering/custom_data.hpp>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(CustomDataCatalogFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(CustomDataCatalogFailure* const failure, const CustomDataCatalogFailureCode code,
                                const CustomDataKind kind, const u32 typeIndex, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->kind = kind;
                failure->typeIndex = typeIndex;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool ValidateCameraTypes(const containers::ArraySpan<const CameraCustomDataDescriptor> descriptors,
                                               CustomDataCatalogFailure* const failure) noexcept
        {
            bool occupied[MaximumCameraCustomDataTypes]{};
            for (const CameraCustomDataDescriptor& descriptor : descriptors)
            {
                if (descriptor.name == nullptr || descriptor.name[0] == '\0' || descriptor.create == nullptr || descriptor.destroy == nullptr)
                    return Fail(failure, CustomDataCatalogFailureCode::InvalidDescriptor, CustomDataKind::Camera, descriptor.typeIndex,
                                "invalid camera custom-data descriptor");
                if (descriptor.typeIndex >= MaximumCameraCustomDataTypes)
                    return Fail(failure, CustomDataCatalogFailureCode::TypeIndexOutOfRange, CustomDataKind::Camera, descriptor.typeIndex,
                                "camera custom-data type index is out of range");
                if (occupied[descriptor.typeIndex])
                    return Fail(failure, CustomDataCatalogFailureCode::DuplicateTypeIndex, CustomDataKind::Camera, descriptor.typeIndex,
                                "camera custom-data type index is already registered");
                occupied[descriptor.typeIndex] = true;
            }
            return true;
        }

        [[nodiscard]] bool ValidateSceneTypes(const containers::ArraySpan<const SceneCustomDataDescriptor> descriptors,
                                              CustomDataCatalogFailure* const failure) noexcept
        {
            bool occupied[MaximumSceneCustomDataTypes]{};
            for (const SceneCustomDataDescriptor& descriptor : descriptors)
            {
                if (descriptor.name == nullptr || descriptor.name[0] == '\0' || descriptor.create == nullptr || descriptor.destroy == nullptr)
                    return Fail(failure, CustomDataCatalogFailureCode::InvalidDescriptor, CustomDataKind::Scene, descriptor.typeIndex,
                                "invalid scene custom-data descriptor");
                if (descriptor.typeIndex >= MaximumSceneCustomDataTypes)
                    return Fail(failure, CustomDataCatalogFailureCode::TypeIndexOutOfRange, CustomDataKind::Scene, descriptor.typeIndex,
                                "scene custom-data type index is out of range");
                if (occupied[descriptor.typeIndex])
                    return Fail(failure, CustomDataCatalogFailureCode::DuplicateTypeIndex, CustomDataKind::Scene, descriptor.typeIndex,
                                "scene custom-data type index is already registered");
                occupied[descriptor.typeIndex] = true;
            }
            return true;
        }
    } // namespace

    bool ValidateCustomDataCatalog(const CustomDataCatalog& catalog, CustomDataCatalogFailure* const failure) noexcept
    {
        ClearFailure(failure);
        return ValidateCameraTypes(catalog.cameraTypes, failure) && ValidateSceneTypes(catalog.sceneTypes, failure);
    }
} // namespace vanguard::rendering
