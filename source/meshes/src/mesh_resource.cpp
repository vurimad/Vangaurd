#include <vanguard/meshes/mesh_resource.hpp>

namespace vanguard::meshes
{
    namespace
    {
        [[nodiscard]] bool Matches(const resources::ResourceHandle& handle, const resources::ResourceReference reference) noexcept
        {
            return handle.IsValid() && handle.GetPath() == reference.GetPath() && handle.GetType() == reference.ExpectedType();
        }
    } // namespace

    MeshResourceObject::MeshResourceObject() noexcept : m_dependencies(memory::pools::Resources::GetInstance()) {}

    MeshResourceObject::~MeshResourceObject()
    {
        Close();
    }

    resources::ResourceTypeId MeshResourceObject::GetType() const noexcept
    {
        return MeshResourceType;
    }

    Result MeshResourceObject::Open(MeshPageSource&& source, const containers::ArraySpan<const resources::ResourceHandle> dependencies,
                                    const ReadLimits& limits) noexcept
    {
        if (m_open || !source.IsOpen())
            return Result::InvalidState;

        m_source = static_cast<MeshPageSource&&>(source);
        Result result = m_source.ReadMetadata(m_metadata, nullptr, limits);
        if (result == Result::Success && !ValidateDependencies(dependencies))
            result = Result::DependencyMismatch;
        if (result != Result::Success)
        {
            Close();
            return result;
        }

        m_dependencies.Reserve(dependencies.Count());
        for (const resources::ResourceHandle& dependency : dependencies)
            m_dependencies.PushBack(dependency);
        m_open = true;
        return Result::Success;
    }

    Result MeshResourceObject::OpenPrepared(MeshPageSource&& source, MeshFile&& metadata,
                                            const containers::ArraySpan<const resources::ResourceHandle> dependencies) noexcept
    {
        if (m_open || !source.IsOpen() || !metadata.IsOpen())
            return Result::InvalidState;
        m_source = static_cast<MeshPageSource&&>(source);
        m_metadata = static_cast<MeshFile&&>(metadata);
        if (!ValidateDependencies(dependencies))
        {
            Close();
            return Result::DependencyMismatch;
        }
        m_dependencies.Reserve(dependencies.Count());
        for (const resources::ResourceHandle& dependency : dependencies)
            m_dependencies.PushBack(dependency);
        m_open = true;
        return Result::Success;
    }

    void MeshResourceObject::Close() noexcept
    {
        m_dependencies.Clear();
        m_metadata.Close();
        m_source.Close();
        m_open = false;
    }

    bool MeshResourceObject::IsOpen() const noexcept
    {
        return m_open;
    }

    const MeshFile& MeshResourceObject::GetMetadata() const noexcept
    {
        return m_metadata;
    }

    const MeshPageSource& MeshResourceObject::GetPageSource() const noexcept
    {
        return m_source;
    }

    containers::ArraySpan<const resources::ResourceHandle> MeshResourceObject::GetDependencies() const noexcept
    {
        return {m_dependencies.TypedData(), m_dependencies.Size()};
    }

    Result MeshResourceObject::BuildLodUploadSet(const u16 lod, containers::DynamicArray<u32>& pages) const noexcept
    {
        pages.Clear();
        if (!m_open)
            return Result::InvalidState;
        Result result = CollectLodPages(m_metadata, lod, pages);
        if (result != Result::Success)
            return result;
        if (lod == m_metadata.GetLods().Size() - 1u)
        {
            for (const u32 page : pages)
            {
                if (!HasFlag(m_metadata.GetPages()[page].flags, PageFlags::RequiredForLowestLod))
                {
                    pages.Clear();
                    return Result::InvalidLod;
                }
            }
        }
        return Result::Success;
    }

    bool MeshResourceObject::ValidateDependencies(const containers::ArraySpan<const resources::ResourceHandle> dependencies) const noexcept
    {
        for (u32 index = 0; index < dependencies.Count(); ++index)
        {
            if (!dependencies[index].IsValid())
                return false;
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (dependencies[previous].GetPath() == dependencies[index].GetPath() && dependencies[previous].GetType() == dependencies[index].GetType())
                    return false;
            }
        }

        const auto contains = [&dependencies](const resources::ResourceReference reference) noexcept
        {
            for (const resources::ResourceHandle& dependency : dependencies)
            {
                if (Matches(dependency, reference))
                    return true;
            }
            return false;
        };
        const auto expected = [this](const resources::ResourceHandle& dependency) noexcept
        {
            if (m_metadata.GetSkeleton().IsValid() && Matches(dependency, m_metadata.GetSkeleton()))
                return true;
            for (const MaterialSlot& material : m_metadata.GetMaterialSlots())
            {
                if (Matches(dependency, material.material))
                    return true;
            }
            return false;
        };

        if (m_metadata.GetSkeleton().IsValid() && !contains(m_metadata.GetSkeleton()))
            return false;
        for (const MaterialSlot& material : m_metadata.GetMaterialSlots())
        {
            if (!contains(material.material))
                return false;
        }
        for (const resources::ResourceHandle& dependency : dependencies)
        {
            if (!expected(dependency))
                return false;
        }
        return true;
    }
} // namespace vanguard::meshes
