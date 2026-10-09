#include <vanguard/editor/editor_panels.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/pool.hpp>

#include <atomic>

namespace
{
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;

    std::atomic<vanguard::u32> g_panelTypeGeneration{0};
    std::atomic<vanguard::u32> g_panelInstanceGeneration{0};

    void ClearFailure(editor::EditorUiFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] bool Fail(editor::EditorUiFailure* const failure, const editor::EditorUiFailureCode code,
                            const char* const message,
                            const editor::EditorPanelTypeId panelType = editor::InvalidEditorPanelTypeId,
                            const editor::EditorPanelInstanceId panelInstance = editor::InvalidEditorPanelInstanceId,
                            const editor::EditorPanelHandle panel = {}) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = code;
            failure->panelType = panelType;
            failure->panelInstance = panelInstance;
            failure->panel = panel;
            failure->message = message;
        }
        return false;
    }

    [[nodiscard]] bool ValidName(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return false;
        vanguard::u32 length = 0;
        while (name[length] != '\0')
            if (++length >= editor::MaximumEditorPanelNameBytes)
                return false;
        return true;
    }

    [[nodiscard]] constexpr bool ValidStateScope(const editor::EditorPanelStateScope scope) noexcept
    {
        return static_cast<vanguard::u8>(scope) <=
               static_cast<vanguard::u8>(editor::EditorPanelStateScope::Content);
    }

    [[nodiscard]] constexpr bool ValidPersistence(const editor::EditorPanelPersistenceDesc& persistence) noexcept
    {
        if (!persistence.IsEnabled())
            return persistence.capture == nullptr && persistence.restore == nullptr &&
                   persistence.migrate == nullptr && persistence.getRevision == nullptr;
        return persistence.capture != nullptr && persistence.restore != nullptr &&
               persistence.getRevision != nullptr;
    }

    [[nodiscard]] vanguard::u32 ReserveGeneration(std::atomic<vanguard::u32>& counter) noexcept
    {
        vanguard::u32 current = counter.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u32{0})
                return 0;
            if (counter.compare_exchange_weak(current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }
} // namespace

namespace vanguard::editor::detail
{
    EditorPanelGenerationSource GetProductionEditorPanelGenerationSource() noexcept
    {
        return {&g_panelTypeGeneration, &g_panelInstanceGeneration};
    }

    EditorPanelRegistryCore::EditorPanelRegistryCore(const EditorUiState& uiState,
                                                     const EditorPanelGenerationSource generations,
                                                     const EditorPanelInvalidationCallback invalidationCallback,
                                                     void* const invalidationUserData) noexcept
        : m_uiState(&uiState), m_generations(generations), m_invalidationCallback(invalidationCallback),
          m_invalidationUserData(invalidationUserData), m_panelTypes(memory::pools::Editor::GetInstance()),
          m_panels(memory::pools::Editor::GetInstance())
    {
        m_panelTypes.Reserve(MaximumEditorPanelTypes);
        m_panels.Reserve(MaximumEditorPanelInstances);
    }

    EditorPanelRegistryCore::~EditorPanelRegistryCore()
    {
        Reset();
    }

    bool EditorPanelRegistryCore::RegisterType(const EditorPanelTypeDesc& desc, EditorPanelTypeToken& output,
                                               EditorUiFailure* const failure) noexcept
    {
        return RegisterTypeForOwner(nullptr, desc, output, failure);
    }

    bool EditorPanelRegistryCore::RegisterTypeForOwner(const EditorExtensionOwner* const owner, const EditorPanelTypeDesc& desc,
                                                       EditorPanelTypeToken& output, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = {};
        if (!CanMutateTopology(failure))
            return false;
        if (owner != nullptr && !owner->IsValid())
            return Fail(failure, EditorUiFailureCode::OwnerRevoked, "editor panel type owner is invalid", desc.id);
        if (desc.id == InvalidEditorPanelTypeId || !ValidName(desc.name) || desc.draw == nullptr ||
            (desc.create == nullptr) != (desc.destroy == nullptr) || !ValidPersistence(desc.persistence))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor panel type descriptor is invalid", desc.id);

        switch (desc.policy)
        {
        case EditorPanelTypePolicy::Multiple:
            if (desc.defaultInstance != InvalidEditorPanelInstanceId)
                return Fail(failure, EditorUiFailureCode::InvalidPolicy,
                            "multi-instance editor panel type cannot declare a singleton default instance", desc.id);
            break;
        case EditorPanelTypePolicy::Singleton:
            if (desc.defaultInstance == InvalidEditorPanelInstanceId)
                return Fail(failure, EditorUiFailureCode::InvalidPolicy,
                            "singleton editor panel type requires a nonzero default instance", desc.id);
            break;
        default:
            return Fail(failure, EditorUiFailureCode::InvalidPolicy, "editor panel type policy is invalid", desc.id);
        }
        if (static_cast<u32>(desc.initialPlacement) > static_cast<u32>(EditorPanelInitialPlacement::Floating))
            return Fail(failure, EditorUiFailureCode::InvalidPolicy, "editor panel initial placement is invalid", desc.id);

        if (FindPhysicalType(desc.id) != nullptr || HasPhysicalPlaceholderForType(desc.id))
            return Fail(failure, EditorUiFailureCode::DuplicateIdentity, "editor panel type identity is already registered", desc.id);

        const u32 index = FindTypeSlot();
        if (index == ~u32{0})
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor panel type capacity is exhausted", desc.id);

        const u32 generation = ReserveTypeGeneration();
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted, "editor panel type generation space is exhausted", desc.id);

        if (index == m_panelTypes.Size())
            static_cast<void>(m_panelTypes.EmplaceBack());
        PanelTypeRecord& type = m_panelTypes[index];
        type.token = {index, generation};
        type.desc = desc;
        type.owner = owner != nullptr ? *owner : EditorExtensionOwner{};
        type.name.Set(desc.name);
        type.desc.name = type.name.AsChar();
        type.active = true;
        type.reachable = true;
        output = type.token;
        return true;
    }

    bool EditorPanelRegistryCore::UnregisterType(const EditorPanelTypeToken token, EditorUiFailure* const failure) noexcept
    {
        return UnregisterTypeForOwner(nullptr, token, failure);
    }

    bool EditorPanelRegistryCore::UnregisterTypeForOwner(const EditorExtensionOwner* const owner, const EditorPanelTypeToken token,
                                                         EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!CanMutateTopology(failure))
            return false;
        PanelTypeRecord* const type = FindType(token);
        if (type == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor panel type token is invalid");
        const bool owned = type->owner.IsValid();
        if ((owner == nullptr && owned) || (owner != nullptr && (!owner->IsValid() || !owned || !(type->owner == *owner))))
            return Fail(failure, EditorUiFailureCode::OwnerRevoked, "editor panel type token owner does not match the registration", type->desc.id);
        if (owner == nullptr)
        {
            for (const PanelRecord& panel : m_panels)
                if (panel.active && !panel.placeholder && panel.type == type->desc.id)
                    return Fail(failure, EditorUiFailureCode::InUse, "editor panel type still owns panel instances",
                                type->desc.id, panel.id, panel.handle);
        }
        else
        {
            for (const PanelRecord& panel : m_panels)
                if (panel.active && !panel.placeholder && panel.type == type->desc.id && !(panel.owner == *owner))
                    return Fail(failure, EditorUiFailureCode::InUse, "editor panel type contains an instance owned by another extension",
                                type->desc.id, panel.id, panel.handle);
            for (PanelRecord& panel : m_panels)
                if (panel.active && !panel.placeholder && panel.type == type->desc.id && panel.owner == *owner)
                    DestroyPanelState(panel);
        }
        type->active = false;
        type->reachable = false;
        type->token = {};
        type->desc = {};
        type->owner = {};
        type->name.Clear();
        return true;
    }

    bool EditorPanelRegistryCore::CanRevokeOwner(const EditorExtensionOwner& owner) const noexcept
    {
        return concurrency::IsMainThread() && owner.IsValid() && m_persistenceCallbackDepth == 0;
    }

    void EditorPanelRegistryCore::RevokeOwner(const EditorExtensionOwner& owner) noexcept
    {
        if (!CanRevokeOwner(owner))
            return;
        for (PanelTypeRecord& type : m_panelTypes)
            if (type.active && type.owner == owner)
                type.reachable = false;
        for (PanelRecord& panel : m_panels)
            if (panel.active && panel.owner == owner)
            {
                if (panel.reachable)
                    AdvanceUserStateRevision();
                panel.reachable = false;
                panel.pendingVisibility = false;
                if (m_invalidationCallback != nullptr)
                    static_cast<void>(m_invalidationCallback(panel.handle, EditorPanelInvalidationReason::OwnerRevoke,
                                                             m_invalidationUserData, nullptr));
            }
    }

    bool EditorPanelRegistryCore::CreatePanel(const EditorPanelDesc& desc, EditorPanelHandle& output,
                                              EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = {};
        if (!CanMutateTopology(failure))
            return false;
        PanelTypeRecord* const type = FindType(desc.type);
        if (desc.id == InvalidEditorPanelInstanceId || type == nullptr || !ValidName(desc.title))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor panel instance descriptor is invalid", desc.type, desc.id);

        PanelRecord* const duplicate = FindPhysicalPanelById(desc.id);
        if (type->desc.policy == EditorPanelTypePolicy::Singleton)
        {
            if (desc.id != type->desc.defaultInstance)
                return Fail(failure, EditorUiFailureCode::SingletonViolation,
                            "singleton editor panel creation must use the type default instance", desc.type, desc.id);
            if (duplicate != nullptr)
            {
                if (!duplicate->placeholder && duplicate->type == desc.type)
                {
                    const bool reopened = !duplicate->open;
                    if (reopened)
                        AdvanceUserStateRevision();
                    duplicate->open = true;
                    duplicate->pendingVisibility = false;
                    output = duplicate->handle;
                    if (reopened && type->desc.visibilityChanged != nullptr)
                        type->desc.visibilityChanged(duplicate->state, true, type->desc.factoryData);
                    return true;
                }
                return Fail(failure, EditorUiFailureCode::DuplicateIdentity,
                            "editor panel instance identity is already registered", desc.type, desc.id, duplicate->handle);
            }
            for (const PanelRecord& panel : m_panels)
                if (panel.active && !panel.placeholder && panel.type == desc.type)
                    return Fail(failure, EditorUiFailureCode::SingletonViolation,
                                "singleton editor panel type already owns a live instance", desc.type, panel.id, panel.handle);
        }
        else if (duplicate != nullptr)
        {
            return Fail(failure, EditorUiFailureCode::DuplicateIdentity,
                        "editor panel instance identity is already registered", desc.type, desc.id, duplicate->handle);
        }

        const u32 index = FindPanelSlot();
        if (index == ~u32{0})
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor panel instance capacity is exhausted", desc.type, desc.id);

        const u32 generation = ReserveInstanceGeneration();
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                        "editor panel instance generation space is exhausted", desc.type, desc.id);

        void* const state = type->desc.create != nullptr ? type->desc.create(desc.id, type->desc.factoryData) : nullptr;
        if (type->desc.create != nullptr && state == nullptr)
            return Fail(failure, EditorUiFailureCode::FactoryFailure, "editor panel instance factory failed", desc.type, desc.id);

        if (index == m_panels.Size())
            static_cast<void>(m_panels.EmplaceBack());
        PanelRecord& panel = m_panels[index];
        panel.handle = {index, generation};
        panel.id = desc.id;
        panel.type = desc.type;
        panel.owner = type->owner;
        panel.title.Set(desc.title);
        panel.state = state;
        panel.open = desc.open;
        panel.active = true;
        panel.reachable = true;
        panel.placeholder = false;
        panel.pendingVisibility = false;
        panel.pendingOpen = false;
        panel.persistentStateRevision = 0;
        panel.persistentStateRevisionFaulted = false;
        AdvanceUserStateRevision();
        output = panel.handle;
        return true;
    }

    bool EditorPanelRegistryCore::CreatePlaceholder(const EditorPanelPlaceholderDesc& desc, EditorPanelHandle& output,
                                                    EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = {};
        if (!CanMutateTopology(failure))
            return false;
        if (desc.unresolvedType == InvalidEditorPanelTypeId || desc.id == InvalidEditorPanelInstanceId || !ValidName(desc.title))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor panel placeholder descriptor is invalid",
                        desc.unresolvedType, desc.id);
        if (FindPhysicalType(desc.unresolvedType) != nullptr)
            return Fail(failure, EditorUiFailureCode::DuplicateIdentity,
                        "editor panel placeholder cannot alias a registered panel type", desc.unresolvedType, desc.id);
        if (const PanelRecord* const duplicate = FindPhysicalPanelById(desc.id); duplicate != nullptr)
            return Fail(failure, EditorUiFailureCode::DuplicateIdentity,
                        "editor panel instance identity is already registered", desc.unresolvedType, desc.id, duplicate->handle);

        const u32 index = FindPanelSlot();
        if (index == ~u32{0})
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor panel instance capacity is exhausted",
                        desc.unresolvedType, desc.id);
        const u32 generation = ReserveInstanceGeneration();
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                        "editor panel instance generation space is exhausted", desc.unresolvedType, desc.id);

        if (index == m_panels.Size())
            static_cast<void>(m_panels.EmplaceBack());
        PanelRecord& panel = m_panels[index];
        panel.handle = {index, generation};
        panel.id = desc.id;
        panel.type = desc.unresolvedType;
        panel.owner = {};
        panel.title.Set(desc.title);
        panel.state = nullptr;
        panel.open = desc.open;
        panel.active = true;
        panel.reachable = true;
        panel.placeholder = true;
        panel.pendingVisibility = false;
        panel.pendingOpen = false;
        panel.persistentStateRevision = 0;
        panel.persistentStateRevisionFaulted = false;
        AdvanceUserStateRevision();
        output = panel.handle;
        return true;
    }

    bool EditorPanelRegistryCore::DestroyPanel(const EditorPanelHandle handle, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!CanMutateTopology(failure))
            return false;
        PanelRecord* const panel = FindPanel(handle);
        if (panel == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor panel handle is invalid",
                        InvalidEditorPanelTypeId, InvalidEditorPanelInstanceId, handle);
        if (m_invalidationCallback != nullptr &&
            !m_invalidationCallback(handle, EditorPanelInvalidationReason::Destroy, m_invalidationUserData, failure))
            return false;
        DestroyPanelState(*panel);
        return true;
    }

    bool EditorPanelRegistryCore::SetOpen(const EditorPanelHandle handle, const bool open, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!CanMutateVisibility(failure))
            return false;
        PanelRecord* const panel = FindPanel(handle);
        if (panel == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor panel handle is invalid",
                        InvalidEditorPanelTypeId, InvalidEditorPanelInstanceId, handle);
        if (*m_uiState == EditorUiState::BuildingFrame)
        {
            panel->pendingVisibility = true;
            panel->pendingOpen = open;
        }
        else
        {
            if (!open && panel->open && m_invalidationCallback != nullptr &&
                !m_invalidationCallback(handle, EditorPanelInvalidationReason::Close, m_invalidationUserData, failure))
                return false;
            const bool changed = panel->open != open;
            if (changed)
                AdvanceUserStateRevision();
            panel->open = open;
            panel->pendingVisibility = false;
            if (changed && !panel->placeholder)
                if (PanelTypeRecord* const type = FindPhysicalType(panel->type);
                    type != nullptr && type->desc.visibilityChanged != nullptr)
                    type->desc.visibilityChanged(panel->state, open, type->desc.factoryData);
        }
        return true;
    }

    bool EditorPanelRegistryCore::IsOpen(const EditorPanelHandle handle) const noexcept
    {
        const PanelRecord* const panel = FindPanel(handle);
        return panel != nullptr && panel->open;
    }

    void EditorPanelRegistryCore::VisitPanels(const EditorPanelVisitor visitor, void* const userData) const noexcept
    {
        if (visitor == nullptr)
            return;
        for (const PanelRecord& panel : m_panels)
            if (panel.active && panel.reachable)
                visitor({panel.handle, panel.id, panel.type, panel.title.AsChar(), panel.open, panel.placeholder}, userData);
    }

    void EditorPanelRegistryCore::VisitOpenForAuthoring(const EditorPanelAuthoringVisitor visitor, void* const userData) noexcept
    {
        if (visitor == nullptr)
            return;
        for (PanelRecord& panel : m_panels)
        {
            if (!panel.active || !panel.reachable || !panel.open)
                continue;
            if (panel.placeholder)
            {
                visitor({panel.handle, panel.owner, panel.id, panel.type, panel.title.AsChar(), nullptr, nullptr, nullptr, true,
                         EditorPanelInitialPlacement::Center}, userData);
                continue;
            }
            PanelTypeRecord* const type = FindType(panel.type);
            if (type == nullptr || type->desc.draw == nullptr)
                continue;
            visitor({panel.handle, panel.owner, panel.id, panel.type, panel.title.AsChar(), panel.state, type->desc.draw,
                     type->desc.factoryData, false, type->desc.initialPlacement}, userData);
        }
    }

    bool EditorPanelRegistryCore::ResolveFocusReceiver(const EditorPanelHandle handle, void*& receiverData) const noexcept
    {
        receiverData = nullptr;
        const PanelRecord* const panel = FindPanel(handle);
        if (panel == nullptr)
            return false;
        const bool effectivelyOpen = panel->pendingVisibility ? panel->pendingOpen : panel->open;
        if (!effectivelyOpen)
            return false;
        receiverData = panel->state;
        return true;
    }

    bool EditorPanelRegistryCore::CaptureUserState(EditorPanelUserStateRecord* const records, const u32 capacity,
                                                   u32& count, EditorUiFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        count = 0;
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread,
                        "editor panel user-state capture requires the main thread");
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor panel user-state capture requires an idle ready UI service");
        if (m_userStateRevision == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                        "editor panel user-state revision space is exhausted");

        for (const PanelRecord& panel : m_panels)
            if (panel.active && panel.reachable)
                ++count;
        if (count > capacity || (count != 0 && records == nullptr))
            return Fail(failure, count > capacity ? EditorUiFailureCode::CapacityExceeded : EditorUiFailureCode::InvalidDescriptor,
                        "editor panel user-state capture destination is insufficient");

        u32 write = 0;
        for (const PanelRecord& panel : m_panels)
        {
            if (!panel.active || !panel.reachable)
                continue;
            EditorPanelUserStateRecord& record = records[write++];
            record = {};
            record.type = panel.type;
            record.instance = panel.id;
            record.open = panel.open;
            const char* const title = panel.title.AsChar();
            while (record.fallbackTitleByteCount + 1u < MaximumEditorPanelNameBytes &&
                   title[record.fallbackTitleByteCount] != '\0')
            {
                record.fallbackTitle[record.fallbackTitleByteCount] = title[record.fallbackTitleByteCount];
                ++record.fallbackTitleByteCount;
            }
            record.fallbackTitle[record.fallbackTitleByteCount] = '\0';
        }

        for (u32 index = 1; index < count; ++index)
        {
            const EditorPanelUserStateRecord value = records[index];
            u32 insertion = index;
            while (insertion != 0)
            {
                const EditorPanelUserStateRecord& previous = records[insertion - 1u];
                if (previous.instance < value.instance ||
                    (previous.instance == value.instance && previous.type <= value.type))
                    break;
                records[insertion] = previous;
                --insertion;
            }
            records[insertion] = value;
        }
        return true;
    }

    bool EditorPanelRegistryCore::RestoreUserState(
        const containers::ArraySpan<const EditorPanelUserStateRecord> records,
        EditorPanelUserStateRestoreStatus& status, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        status = {};
        if (!CanMutateTopology(failure))
            return false;
        if (records.Size() > MaximumEditorPanelInstances || (!records.Empty() && records.Data() == nullptr))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                        "editor panel user-state restore input is invalid");

        EditorPanelInstanceId previousInstance = InvalidEditorPanelInstanceId;
        for (u32 index = 0; index < records.Size(); ++index)
        {
            const EditorPanelUserStateRecord& record = records[index];
            if (record.type == InvalidEditorPanelTypeId || record.instance == InvalidEditorPanelInstanceId ||
                record.fallbackTitleByteCount == 0 || record.fallbackTitleByteCount >= MaximumEditorPanelNameBytes ||
                record.fallbackTitle[0] == '\0' ||
                (index != 0 && record.instance <= previousInstance))
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                            "editor panel user-state restore records are invalid or non-canonical",
                            record.type, record.instance);
            previousInstance = record.instance;
        }

        const auto skip = [&status](const EditorUiFailure& skippedFailure) noexcept {
            ++status.skipped;
            if (status.firstSkippedFailure.code == EditorUiFailureCode::None)
                status.firstSkippedFailure = skippedFailure;
        };

        for (const EditorPanelUserStateRecord& record : records)
        {
            if (PanelRecord* const live = FindPanelById(record.instance); live != nullptr)
            {
                if (live->type != record.type)
                {
                    EditorUiFailure skippedFailure;
                    static_cast<void>(Fail(&skippedFailure, EditorUiFailureCode::DuplicateIdentity,
                                           "persisted panel instance identity belongs to a different live type",
                                           record.type, record.instance, live->handle));
                    skip(skippedFailure);
                    continue;
                }

                EditorUiFailure setOpenFailure;
                if (!SetOpen(live->handle, record.open, &setOpenFailure))
                {
                    skip(setOpenFailure);
                    continue;
                }
                ++status.restored;
                continue;
            }

            if (PanelTypeRecord* const type = FindType(record.type); type != nullptr)
            {
                EditorPanelHandle created;
                EditorUiFailure createFailure;
                if (!CreatePanel({record.instance, record.type, type->desc.name, record.open}, created, &createFailure))
                {
                    skip(createFailure);
                    continue;
                }
                ++status.created;
                continue;
            }

            if (FindPhysicalType(record.type) != nullptr)
            {
                EditorUiFailure skippedFailure;
                static_cast<void>(Fail(&skippedFailure, EditorUiFailureCode::OwnerRevoked,
                                       "persisted panel type is registered but not reachable",
                                       record.type, record.instance));
                skip(skippedFailure);
                continue;
            }

            char fallbackTitle[MaximumEditorPanelNameBytes]{};
            for (u32 byte = 0; byte < record.fallbackTitleByteCount; ++byte)
                fallbackTitle[byte] = record.fallbackTitle[byte];
            fallbackTitle[record.fallbackTitleByteCount] = '\0';

            EditorPanelHandle placeholder;
            EditorUiFailure placeholderFailure;
            if (!CreatePlaceholder({record.type, record.instance, fallbackTitle, record.open},
                                   placeholder, &placeholderFailure))
            {
                skip(placeholderFailure);
                continue;
            }
            ++status.placeholders;
        }
        return true;
    }

    bool EditorPanelRegistryCore::ReconcilePresentationUserState(
        const containers::ArraySpan<const EditorPanelUserStateRecord> records,
        EditorPanelUserStateRestoreStatus& status, EditorUiFailure* const failure) noexcept
    {
        EditorPanelUserStateRecord backup[MaximumEditorPanelInstances]{};
        u32 backupCount = 0;
        if (!CaptureUserState(backup, MaximumEditorPanelInstances, backupCount, failure))
            return false;
        const auto rollback = [&]() noexcept {
            const auto existedBefore = [&](const EditorPanelInstanceId instance) noexcept {
                for (u32 index = 0; index < backupCount; ++index)
                    if (backup[index].instance == instance)
                        return true;
                return false;
            };
            // Instances created solely by a failed presentation transaction have never been
            // exposed to a committed UI frame. Tear them down through their real owner before
            // restoring visibility so a failed switch cannot leak hidden topology into the next
            // automatic session capture.
            for (PanelRecord& panel : m_panels)
                if (panel.active && panel.reachable && !existedBefore(panel.id))
                    DestroyPanelState(panel);
            for (u32 index = 0; index < backupCount; ++index)
                if (PanelRecord* const panel = FindPanelById(backup[index].instance);
                    panel != nullptr && panel->open != backup[index].open)
                {
                    EditorUiFailure ignored;
                    static_cast<void>(SetOpen(panel->handle, backup[index].open, &ignored));
                }
        };

        // RestoreUserState performs the complete canonical validation before it mutates any target
        // records, creates missing instances through their real registered owners and preserves
        // unavailable plugin identities as placeholders. Named-layout application then differs
        // from startup restore in one important way: live instances absent from the target
        // presentation are hidden, never destroyed, so their content/local state remains resident.
        if (!RestoreUserState(records, status, failure))
            return false;
        if (status.skipped != 0)
        {
            if (failure != nullptr)
                *failure = status.firstSkippedFailure;
            rollback();
            return false;
        }

        const auto containsInstance = [&](const EditorPanelInstanceId instance) noexcept {
            u32 begin = 0;
            u32 end = records.Size();
            while (begin < end)
            {
                const u32 middle = begin + (end - begin) / 2u;
                if (records[middle].instance < instance)
                    begin = middle + 1u;
                else
                    end = middle;
            }
            return begin < records.Size() && records[begin].instance == instance;
        };

        for (PanelRecord& panel : m_panels)
        {
            if (!panel.active || !panel.reachable || containsInstance(panel.id) || !panel.open)
                continue;
            if (!SetOpen(panel.handle, false, failure))
            {
                rollback();
                return false;
            }
        }
        return true;
    }

    EditorPanelPersistentStateCapture EditorPanelRegistryCore::CapturePersistentState(
        const EditorPanelTypeId type, const EditorPanelInstanceId instance,
        const EditorPanelStateScope scope, void* const destination, const u32 capacity,
        EditorUiFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        EditorPanelPersistentStateCapture output;
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongThread,
                                   "editor panel persistent-state capture requires the main thread",
                                   type, instance));
            output.result = EditorPanelStateResult::CallbackFailure;
            return output;
        }
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongPhase,
                                   "editor panel persistent-state capture requires an idle ready UI service",
                                   type, instance));
            output.result = EditorPanelStateResult::CallbackFailure;
            return output;
        }
        if (m_persistenceCallbackDepth != 0)
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongPhase,
                                   "editor panel persistent-state callbacks cannot be reentered",
                                   type, instance));
            output.result = EditorPanelStateResult::CallbackFailure;
            return output;
        }
        if (type == InvalidEditorPanelTypeId || instance == InvalidEditorPanelInstanceId ||
            !ValidStateScope(scope) || capacity > MaximumEditorPanelStateBytes ||
            (capacity != 0 && destination == nullptr))
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                                   "editor panel persistent-state capture input is invalid", type, instance));
            output.result = EditorPanelStateResult::InvalidData;
            return output;
        }

        const PanelRecord* const panel = FindPanelById(instance);
        if (panel == nullptr || panel->type != type || panel->placeholder)
        {
            output.result = EditorPanelStateResult::ProviderUnavailable;
            return output;
        }
        const PanelTypeRecord* const panelType = FindType(type);
        if (panelType == nullptr || !panelType->desc.persistence.IsEnabled())
        {
            output.result = EditorPanelStateResult::ProviderUnavailable;
            return output;
        }
        if (panel->persistentStateRevisionFaulted)
        {
            output.version = panelType->desc.persistence.stateVersion;
            output.result = EditorPanelStateResult::CallbackFailure;
            return output;
        }

        output.version = panelType->desc.persistence.stateVersion;
        u32 byteCount = 0;
        ++m_persistenceCallbackDepth;
        output.result = panelType->desc.persistence.capture(
            panel->id, panel->state, scope, destination, capacity, byteCount, panelType->desc.factoryData);
        --m_persistenceCallbackDepth;
        if (output.result == EditorPanelStateResult::Success)
        {
            if (byteCount > capacity || byteCount > MaximumEditorPanelStateBytes)
            {
                output.result = EditorPanelStateResult::CapacityExceeded;
                return output;
            }
            output.byteCount = byteCount;
        }
        return output;
    }

    EditorPanelStateResult EditorPanelRegistryCore::RestorePersistentState(
        const EditorPanelTypeId type, const EditorPanelInstanceId instance,
        const EditorPanelStateScope scope, const u32 version,
        const containers::ArraySpan<const u8> bytes, void* const migrationDestination,
        const u32 migrationCapacity, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongThread,
                                   "editor panel persistent-state restore requires the main thread",
                                   type, instance));
            return EditorPanelStateResult::CallbackFailure;
        }
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongPhase,
                                   "editor panel persistent-state restore requires an idle ready UI service",
                                   type, instance));
            return EditorPanelStateResult::CallbackFailure;
        }
        if (m_persistenceCallbackDepth != 0)
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::WrongPhase,
                                   "editor panel persistent-state callbacks cannot be reentered",
                                   type, instance));
            return EditorPanelStateResult::CallbackFailure;
        }
        if (type == InvalidEditorPanelTypeId || instance == InvalidEditorPanelInstanceId ||
            !ValidStateScope(scope) || version == 0 || bytes.Size() > MaximumEditorPanelStateBytes ||
            (!bytes.Empty() && bytes.Data() == nullptr) || migrationCapacity > MaximumEditorPanelStateBytes ||
            (migrationCapacity != 0 && migrationDestination == nullptr))
        {
            static_cast<void>(Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                                   "editor panel persistent-state restore input is invalid", type, instance));
            return EditorPanelStateResult::InvalidData;
        }

        PanelRecord* const panel = FindPanelById(instance);
        if (panel == nullptr || panel->type != type || panel->placeholder)
            return EditorPanelStateResult::ProviderUnavailable;
        PanelTypeRecord* const panelType = FindType(type);
        if (panelType == nullptr || !panelType->desc.persistence.IsEnabled())
            return EditorPanelStateResult::ProviderUnavailable;

        const EditorPanelPersistenceDesc& persistence = panelType->desc.persistence;
        containers::ArraySpan<const u8> restoreBytes = bytes;
        u32 restoreVersion = version;
        if (version > persistence.stateVersion)
            return EditorPanelStateResult::UnsupportedVersion;
        ++m_persistenceCallbackDepth;
        if (version < persistence.stateVersion && persistence.migrate != nullptr)
        {
            u32 migratedByteCount = 0;
            const EditorPanelStateResult migrated = persistence.migrate(
                panel->id, panel->state, scope, version, bytes, migrationDestination, migrationCapacity,
                migratedByteCount, panelType->desc.factoryData);
            if (migrated != EditorPanelStateResult::Success)
            {
                --m_persistenceCallbackDepth;
                return migrated;
            }
            if (migratedByteCount > migrationCapacity || migratedByteCount > MaximumEditorPanelStateBytes)
            {
                --m_persistenceCallbackDepth;
                return EditorPanelStateResult::CapacityExceeded;
            }
            restoreBytes = {static_cast<const u8*>(migrationDestination), migratedByteCount};
            restoreVersion = persistence.stateVersion;
        }
        const EditorPanelStateResult restored = persistence.restore(
            panel->id, panel->state, scope, restoreVersion, restoreBytes, panelType->desc.factoryData);
        --m_persistenceCallbackDepth;
        return restored;
    }

    bool EditorPanelRegistryCore::RefreshPersistentStateRevisions(EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread,
                        "editor panel persistent-state revision refresh requires the main thread");
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor panel persistent-state revision refresh requires an idle ready UI service");
        if (m_persistenceCallbackDepth != 0)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor panel persistent-state callbacks cannot be reentered");
        if (m_userStateRevision == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                        "editor panel user-state revision space is exhausted");

        for (PanelRecord& panel : m_panels)
        {
            if (!panel.active || !panel.reachable || panel.placeholder)
                continue;
            PanelTypeRecord* const type = FindType(panel.type);
            if (type == nullptr || !type->desc.persistence.IsEnabled())
                continue;
            ++m_persistenceCallbackDepth;
            const u64 current = type->desc.persistence.getRevision(
                panel.id, panel.state, type->desc.factoryData);
            --m_persistenceCallbackDepth;
            // Revision 0 is the provider's soft-unavailable signal. A regressed nonzero revision is
            // likewise isolated to this provider instead of freezing all session persistence. Keep
            // the last good baseline/canonical blob until the provider reports a monotonic revision.
            if (current == 0 ||
                (panel.persistentStateRevision != 0 && current < panel.persistentStateRevision))
            {
                panel.persistentStateRevisionFaulted = true;
                continue;
            }
            panel.persistentStateRevisionFaulted = false;
            if (current != panel.persistentStateRevision)
            {
                panel.persistentStateRevision = current;
                AdvanceUserStateRevision();
                if (m_userStateRevision == 0)
                    return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                                "editor panel user-state revision space is exhausted",
                                panel.type, panel.id, panel.handle);
            }
        }
        return true;
    }

    void EditorPanelRegistryCore::CommitPendingVisibility() noexcept
    {
        for (PanelRecord& panel : m_panels)
        {
            if (!panel.active || !panel.pendingVisibility)
                continue;
            const bool changed = panel.open != panel.pendingOpen;
            if (changed)
                AdvanceUserStateRevision();
            panel.open = panel.pendingOpen;
            panel.pendingVisibility = false;
            if (changed && !panel.placeholder)
                if (PanelTypeRecord* const type = FindPhysicalType(panel.type);
                    type != nullptr && type->desc.visibilityChanged != nullptr)
                    type->desc.visibilityChanged(panel.state, panel.open, type->desc.factoryData);
        }
    }

    void EditorPanelRegistryCore::DiscardPendingVisibility() noexcept
    {
        for (PanelRecord& panel : m_panels)
            if (panel.active)
                panel.pendingVisibility = false;
    }

    void EditorPanelRegistryCore::Reset() noexcept
    {
        for (PanelRecord& panel : m_panels)
            if (panel.active)
                DestroyPanelState(panel);
        m_panels.Clear();
        for (PanelTypeRecord& type : m_panelTypes)
        {
            type.active = false;
            type.reachable = false;
            type.token = {};
            type.desc = {};
            type.owner = {};
            type.name.Clear();
        }
        m_panelTypes.Clear();
    }

    bool EditorPanelRegistryCore::CanMutateTopology(EditorUiFailure* const failure) const noexcept
    {
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread, "editor panel registry mutation requires the main thread");
        if (m_persistenceCallbackDepth != 0)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor panel topology cannot change during a persistence callback");
        if (m_uiState != nullptr && *m_uiState == EditorUiState::Ready)
            return true;
        return Fail(failure,
                    m_uiState != nullptr && *m_uiState == EditorUiState::BuildingFrame ? EditorUiFailureCode::WrongPhase
                                                                                      : EditorUiFailureCode::NotReady,
                    "editor panel topology mutation requires a ready UI service");
    }

    bool EditorPanelRegistryCore::CanMutateVisibility(EditorUiFailure* const failure) const noexcept
    {
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread, "editor panel visibility mutation requires the main thread");
        if (m_persistenceCallbackDepth != 0)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor panel visibility cannot change during a persistence callback");
        if (m_uiState != nullptr && (*m_uiState == EditorUiState::Ready || *m_uiState == EditorUiState::BuildingFrame))
            return true;
        return Fail(failure, EditorUiFailureCode::NotReady, "editor panel visibility mutation requires an active UI service");
    }

    u32 EditorPanelRegistryCore::ReserveTypeGeneration() noexcept
    {
        return m_generations.IsValid() ? ReserveGeneration(*m_generations.typeCounter) : 0u;
    }

    u32 EditorPanelRegistryCore::ReserveInstanceGeneration() noexcept
    {
        return m_generations.IsValid() ? ReserveGeneration(*m_generations.instanceCounter) : 0u;
    }

    EditorPanelRegistryCore::PanelTypeRecord* EditorPanelRegistryCore::FindType(const EditorPanelTypeId id) noexcept
    {
        return const_cast<PanelTypeRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindType(id));
    }

    const EditorPanelRegistryCore::PanelTypeRecord* EditorPanelRegistryCore::FindType(const EditorPanelTypeId id) const noexcept
    {
        for (const PanelTypeRecord& type : m_panelTypes)
            if (type.active && type.reachable && type.desc.id == id)
                return &type;
        return nullptr;
    }

    EditorPanelRegistryCore::PanelTypeRecord* EditorPanelRegistryCore::FindPhysicalType(const EditorPanelTypeId id) noexcept
    {
        return const_cast<PanelTypeRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindPhysicalType(id));
    }

    const EditorPanelRegistryCore::PanelTypeRecord* EditorPanelRegistryCore::FindPhysicalType(const EditorPanelTypeId id) const noexcept
    {
        for (const PanelTypeRecord& type : m_panelTypes)
            if (type.active && type.desc.id == id)
                return &type;
        return nullptr;
    }

    EditorPanelRegistryCore::PanelTypeRecord* EditorPanelRegistryCore::FindType(const EditorPanelTypeToken token) noexcept
    {
        return token.IsValid() && token.index < m_panelTypes.Size() && m_panelTypes[token.index].active &&
                       m_panelTypes[token.index].token == token
                   ? &m_panelTypes[token.index]
                   : nullptr;
    }

    EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPanel(const EditorPanelHandle handle) noexcept
    {
        return const_cast<PanelRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindPanel(handle));
    }

    const EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPanel(const EditorPanelHandle handle) const noexcept
    {
        return handle.IsValid() && handle.index < m_panels.Size() && m_panels[handle.index].active &&
                       m_panels[handle.index].reachable &&
                       m_panels[handle.index].handle == handle
                   ? &m_panels[handle.index]
                   : nullptr;
    }

    EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPhysicalPanel(const EditorPanelHandle handle) noexcept
    {
        return const_cast<PanelRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindPhysicalPanel(handle));
    }

    const EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPhysicalPanel(const EditorPanelHandle handle) const noexcept
    {
        return handle.IsValid() && handle.index < m_panels.Size() && m_panels[handle.index].active &&
                       m_panels[handle.index].handle == handle
                   ? &m_panels[handle.index]
                   : nullptr;
    }

    EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPanelById(const EditorPanelInstanceId id) noexcept
    {
        return const_cast<PanelRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindPanelById(id));
    }

    const EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPanelById(const EditorPanelInstanceId id) const noexcept
    {
        for (const PanelRecord& panel : m_panels)
            if (panel.active && panel.reachable && panel.id == id)
                return &panel;
        return nullptr;
    }

    EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPhysicalPanelById(const EditorPanelInstanceId id) noexcept
    {
        return const_cast<PanelRecord*>(static_cast<const EditorPanelRegistryCore*>(this)->FindPhysicalPanelById(id));
    }

    const EditorPanelRegistryCore::PanelRecord* EditorPanelRegistryCore::FindPhysicalPanelById(const EditorPanelInstanceId id) const noexcept
    {
        for (const PanelRecord& panel : m_panels)
            if (panel.active && panel.id == id)
                return &panel;
        return nullptr;
    }

    bool EditorPanelRegistryCore::HasPhysicalPlaceholderForType(const EditorPanelTypeId id) const noexcept
    {
        for (const PanelRecord& panel : m_panels)
            if (panel.active && panel.placeholder && panel.type == id)
                return true;
        return false;
    }

    u32 EditorPanelRegistryCore::FindTypeSlot() const noexcept
    {
        for (u32 index = 0; index < m_panelTypes.Size(); ++index)
            if (!m_panelTypes[index].active)
                return index;
        return m_panelTypes.Size() < MaximumEditorPanelTypes ? m_panelTypes.Size() : ~u32{0};
    }

    u32 EditorPanelRegistryCore::FindPanelSlot() const noexcept
    {
        for (u32 index = 0; index < m_panels.Size(); ++index)
            if (!m_panels[index].active)
                return index;
        return m_panels.Size() < MaximumEditorPanelInstances ? m_panels.Size() : ~u32{0};
    }

    void EditorPanelRegistryCore::AdvanceUserStateRevision() noexcept
    {
        if (m_userStateRevision == 0)
            return;
        if (m_userStateRevision == ~u64{0})
        {
            m_userStateRevision = 0;
            return;
        }
        ++m_userStateRevision;
    }

    void EditorPanelRegistryCore::DestroyPanelState(PanelRecord& panel) noexcept
    {
        if (panel.reachable)
            AdvanceUserStateRevision();
        if (!panel.placeholder)
            if (PanelTypeRecord* const type = FindPhysicalType(panel.type); type != nullptr && type->desc.destroy != nullptr)
                type->desc.destroy(panel.state, type->desc.factoryData);
        panel.handle = {};
        panel.id = InvalidEditorPanelInstanceId;
        panel.type = InvalidEditorPanelTypeId;
        panel.owner = {};
        panel.title.Clear();
        panel.state = nullptr;
        panel.open = false;
        panel.active = false;
        panel.reachable = false;
        panel.placeholder = false;
        panel.pendingVisibility = false;
        panel.pendingOpen = false;
        panel.persistentStateRevision = 0;
        panel.persistentStateRevisionFaulted = false;
    }
} // namespace vanguard::editor::detail
