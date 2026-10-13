#include "../../../imported/backend/backendData/src/build.h"

#include "../../../imported/backend/backendData/include/dependencyScannerImpl.h"
#include "../../../imported/backend/backendEngine/include/commandletUtils.h"
#include "../../../imported/backend/backendEngine/include/resourceUtils.h"
#include "../../../imported/common/redFileSystem/include/fileSys.h"
#include "../../../imported/common/redFileSystem/include/fileVersionList.h"
#include "../../../imported/common/redFileSystem/include/memoryFileReader.h"
#include "../../../imported/common/redReflection/include/serializationDecompressor.h"
#include "../../../imported/common/redReflection/include/serializationFileTables.h"
#include "../../../imported/common/renderData/include/renderingMemory.h"
#include "../../../imported/common/resourceMesh/include/resourceMeshApi.h"
#include "../../../imported/common/resourceMesh/include/meshMaterialBuffer.h"

namespace
{
    red::StringView Extension(const res::ResourcePath path)
    {
        return red::paths::GetExtension(path.ToStringView());
    }

    bool IsStage11TextResource(const res::ResourcePath path)
    {
        const red::StringView ext = Extension(path);
        return ext == "csv" || ext == "script" || ext == "xml" || ext == "tweak";
    }

    bool IsStage11JsonResource(const res::ResourcePath path)
    {
        const red::StringView ext = Extension(path);
        return ext == "json" || ext == "widget" || ext == "widgetbrush" || ext == "hudentries" ||
               ext == "inkgamesettings" || ext == "inkbrush" || ext == "inkatlas" || ext == "inkstyle" ||
               ext == "inkhud" || ext == "inkmenu" || ext == "inklayers" || ext == "inkshapecollection" ||
               ext == "inkfontfamily" || ext == "inktypography" || ext == "inkfullscreencomposition" ||
               ext == "mlsetup" || ext == "mltemplate" || ext == "inkcharcustomization" ||
               ext == "terrainsetup" || ext == "garmentlayerparams";
    }

    bool IsStage11SpecialEmbeddedResource(const res::ResourcePath path)
    {
        const red::StringView ext = Extension(path);
        return ext == "ent" || ext == "physicalscene" || ext == "prefab" || ext == "app";
    }

    bool SeekToNameIndex(const Uint16 index, IFile& file, serialization::FileTables& parentTables)
    {
        {
            Uint8 flags = 0;
            file << flags;
        }

        for (;;)
        {
            Uint16 nameIndex = 0;
            file << nameIndex;
            if (nameIndex == 0)
                return false;

            Uint16 typeIndex = 0;
            file << typeIndex;
            Uint32 skipDistance = 0;
            file << skipDistance;
            if (nameIndex == index)
                break;

            skipDistance -= sizeof(skipDistance);
            file.Seek(file.GetOffset() + skipDistance);
        }

        return true;
    }

    bool ReadSerializedDataBuffer(IFile& file, serialization::FileTables& parentTables, red::UniqueBuffer& outDataBuffer)
    {
        Uint32 dataSizeOrBufferIndex = 0;
        file << dataSizeOrBufferIndex;

        red::UniqueBuffer dataBuffer;
        if ((dataSizeOrBufferIndex & DataBuffer::c_bufferIndexFlag) != 0)
        {
            const auto index =
                static_cast<serialization::IMapper::BufferIndex>(dataSizeOrBufferIndex & ~DataBuffer::c_bufferIndexFlag);
            if (index > 0)
            {
                const auto& buffer = parentTables.m_buffers[index - 1];
                const Bool isCompressed = buffer.m_dataSizeInMemory != buffer.m_dataSizeOnDisk;
                file.Seek(buffer.m_dataOffset);

                auto diskBuffer =
                    red::CreateUniqueBuffer<red::PoolBackend>(buffer.m_dataSizeOnDisk, DataBuffer::c_defaultAlignment);
                file.Serialize(diskBuffer.Get(), diskBuffer.Size());
                if (isCompressed)
                {
                    dataBuffer =
                        red::CreateUniqueBuffer<red::PoolBackend>(buffer.m_dataSizeInMemory, DataBuffer::c_defaultAlignment);
                    if (!serialization::Decompressor::DecompressData(
                            "", red::MakeBlobView(diskBuffer), red::MakeBlobSpan(dataBuffer)))
                        return false;
                }
                else
                {
                    dataBuffer = std::move(diskBuffer);
                }
            }
        }
        else
        {
            dataBuffer =
                red::CreateUniqueBuffer<red::PoolBackend>(dataSizeOrBufferIndex, DataBuffer::c_defaultAlignment);
            file.Serialize(dataBuffer.Get(), dataBuffer.Size());
        }

        outDataBuffer = std::move(dataBuffer);
        return true;
    }

    bool ReadMeshLocalMaterialHeader(
        IFile& file, mesh::LocalMaterialHeader& header, const Uint16 sizeIndex, const Uint16 offsetIndex)
    {
        header.m_offset = 0;
        header.m_size = 0;

        {
            Uint8 flags = 0;
            file << flags;
        }

        for (;;)
        {
            Uint16 nameIndex = 0;
            file << nameIndex;
            if (nameIndex == 0)
                break;

            Uint16 typeIndex = 0;
            file << typeIndex;
            Uint32 skipDistance = 0;
            file << skipDistance;

            if (nameIndex == sizeIndex)
                file << header.m_size;
            else if (nameIndex == offsetIndex)
                file << header.m_offset;
            else
            {
                skipDistance -= sizeof(skipDistance);
                file.Seek(file.GetOffset() + skipDistance);
            }
        }

        return true;
    }

    dependency::prv::ScanResult ScanStage11BinaryFile(
        IFile& file, res::ResourcePath path, red::DynArray<dependency::prv::ScanDependency>& result);

    dependency::prv::ScanResult ScanStage23Mesh(
        red::DynArray<dependency::prv::ScanDependency>& result, IFile& file,
        serialization::FileTables& parentTables)
    {
        Uint16 rawDataIndex = 0;
        Uint16 rawDataHeadersIndex = 0;
        Uint16 localMaterialIndex = 0;
        Uint16 sizeIndex = 0;
        Uint16 offsetIndex = 0;
        for (Uint32 index = 0; index != parentTables.m_names.Size(); ++index)
        {
            const char* const name = &parentTables.m_strings[parentTables.m_names[index].m_string];
            if (red::StrcmpNC(name, "localMaterialBuffer") == 0)
                localMaterialIndex = static_cast<Uint16>(index);
            else if (red::StrcmpNC(name, "rawData") == 0)
                rawDataIndex = static_cast<Uint16>(index);
            else if (red::StrcmpNC(name, "rawDataHeaders") == 0)
                rawDataHeadersIndex = static_cast<Uint16>(index);
            else if (red::StrcmpNC(name, "offset") == 0)
                offsetIndex = static_cast<Uint16>(index);
            else if (red::StrcmpNC(name, "size") == 0)
                sizeIndex = static_cast<Uint16>(index);
        }

        if (localMaterialIndex == 0 || rawDataIndex == 0 || rawDataHeadersIndex == 0)
            return dependency::prv::ScanResult::OK;

        const auto& mainExport = parentTables.m_exports[0];
        file.Seek(mainExport.m_dataOffset);
        if (!SeekToNameIndex(localMaterialIndex, file, parentTables))
            return dependency::prv::ScanResult::DATA_ERROR;

        const auto localMaterialOffset = file.GetOffset();
        if (!SeekToNameIndex(rawDataHeadersIndex, file, parentTables))
            return dependency::prv::ScanResult::DATA_ERROR;

        red::DynArray<mesh::LocalMaterialHeader> headers;
        Uint32 count = 0;
        file << count;
        if (count != 0)
        {
            headers.Resize(count);
            for (Uint32 i = 0; i < count; ++i)
                if (!ReadMeshLocalMaterialHeader(file, headers[i], sizeIndex, offsetIndex))
                    return dependency::prv::ScanResult::DATA_ERROR;
        }

        file.Seek(localMaterialOffset);
        if (!SeekToNameIndex(rawDataIndex, file, parentTables))
            return dependency::prv::ScanResult::DATA_ERROR;

        red::UniqueBuffer buffer;
        if (!ReadSerializedDataBuffer(file, parentTables, buffer))
            return dependency::prv::ScanResult::DATA_ERROR;

        const red::BlobView bufferView(buffer.Data(), buffer.Size());
        for (const auto& header : headers)
        {
            const red::BlobView instanceView = bufferView.Range(header.m_offset, header.m_size);
            CMemoryFileReaderExternalBuffer reader(instanceView.Data(), instanceView.Size());
            red::DynArray<dependency::prv::ScanDependency> instanceResult;
            const auto scanResult = ScanStage11BinaryFile(reader, res::ResourcePath(), instanceResult);
            if (scanResult != dependency::prv::ScanResult::OK)
                return scanResult;

            for (auto& import : instanceResult)
            {
                if (import.inplace)
                    return dependency::prv::ScanResult::DATA_ERROR;
                import.type = dependency::prv::ScanDependency::Type::SOFT;
                result.PushBack(import);
            }
        }

        return dependency::prv::ScanResult::OK;
    }

    dependency::prv::ScanResult ScanStage11BinaryFile(
        IFile& file, const res::ResourcePath path,
        red::DynArray<dependency::prv::ScanDependency>& result)
    {
        Uint32 version = 0;
        serialization::FileTables tables;
        if (!tables.Load(file, version))
            return dependency::prv::ScanResult::TABLE_FAILURE;
        if (version < VER_MINIMAL || version > VER_CURRENT)
            return dependency::prv::ScanResult::INVALID_VERSION;

        result.Reserve(result.Size() + tables.m_imports.Size());
        for (const auto& import : tables.m_imports)
        {
            const char* const text = &tables.m_strings[import.m_path];
            const auto type = (import.m_flags & serialization::FileTables::eImportFlags_Soft) != 0
                                  ? dependency::prv::ScanDependency::Type::SOFT
                                  : dependency::prv::ScanDependency::Type::HARD;
            const bool inplace = (import.m_flags & serialization::FileTables::eImportFlags_Inplace) != 0;
            result.PushBack({res::ResourcePath::Build(text), type,
                             dependency::prv::ScanDependency::Location::PRIMARY, inplace});
        }

        const red::StringView ext = Extension(path);
        if (ext == "mesh" || ext == "w2mesh")
            return ScanStage23Mesh(result, file, tables);

        // These remaining domains require embedded-package scans that are outside the
        // retained closure. Fail closed rather than publish an incomplete dependency graph.
        return IsStage11SpecialEmbeddedResource(path) ? dependency::prv::ScanResult::DATA_ERROR
                                                       : dependency::prv::ScanResult::OK;
    }

    dependency::prv::ScanResult ScanStage11Binary(
        red::SharedPtr<IFile> file, const res::ResourcePath path,
        red::DynArray<dependency::prv::ScanDependency>& result)
    {
        return file ? ScanStage11BinaryFile(*file, path, result) : dependency::prv::ScanResult::FILE_NOT_FOUND;
    }

    dependency::prv::ScanResult ScanStage11Csv(
        IFile& file, red::DynArray<dependency::prv::ScanDependency>& result)
    {
        red::Blob buffer;
        if (!red::LoadFileToBuffer(file, buffer) || buffer.Size() < 4u)
            return dependency::prv::ScanResult::IO_ERROR;

        // Stage 1.1 accepts only text CSV input. Binary CR2W CSVs require the
        // full resource-loading path, so reject them rather than publishing
        // incomplete dependency links from opaque bytes.
        const auto* const bytes = buffer.Pointer<const Uint8>();
        if (bytes[0] == 'C' && bytes[1] == 'R' && bytes[2] == '2' && bytes[3] == 'W')
            return dependency::prv::ScanResult::DATA_ERROR;

        red::StringView csv(reinterpret_cast<const char*>(buffer.Data()), buffer.Size());
        const char* iter = csv.Data();
        const char* const end = iter + csv.Length();
        const char* start = iter;
        while (iter <= end)
        {
            if (iter == end || *iter == ';' || *iter == '\r' || *iter == '\n' || *iter == '\0')
            {
                const red::StringView element(start, iter - start);
                if (!element.Empty() && dependency::prv::LooksLikeResourcePath(element))
                {
                    result.PushBack({res::ResourcePath::Build(element),
                                     dependency::prv::ScanDependency::Type::SOFT,
                                     dependency::prv::ScanDependency::Location::PRIMARY, false});
                }
                start = iter + 1;
            }
            ++iter;
        }
        return dependency::prv::ScanResult::OK;
    }

    dependency::prv::ScanResult ScanStage11Tweak(
        IFile& file, red::DynArray<dependency::prv::ScanDependency>& result)
    {
        String textData;
        if (!red::LoadFileToString(file, textData))
            return dependency::prv::ScanResult::IO_ERROR;

        const char* iter = textData.AsChar();
        const char* const end = iter + textData.Length();
        const char* start = nullptr;
        while (iter < end)
        {
            if (*iter == '"')
            {
                if (start == nullptr)
                {
                    start = iter + 1;
                }
                else
                {
                    const red::StringView value(start, iter - start);
                    if (!value.Empty() && dependency::prv::LooksLikeResourcePath(value))
                    {
                        result.PushBack({res::ResourcePath::Build(value),
                                         dependency::prv::ScanDependency::Type::SOFT,
                                         dependency::prv::ScanDependency::Location::PRIMARY, false});
                    }
                    start = nullptr;
                }
            }
            ++iter;
        }

        return dependency::prv::ScanResult::OK;
    }
}

bool IsBinkResource(const res::ResourcePath& path)
{
    return Extension(path) == "bk2";
}

bool IsICUResource(const res::ResourcePath& path)
{
    static const res::ResourcePath icu = res::ResourcePath::Build("engine\\i18n\\icudt65l.dat");
    return path == icu;
}

bool IsAudioResource(const res::ResourcePath& path)
{
    const red::StringView ext = Extension(path);
    return ext == "opusinfo" || ext == "opuswem" || ext == "wem" || ext == "bnk" || ext == "opuspak";
}

bool CanBinaryScanResource(const res::ResourcePath& path)
{
    return !IsStage11TextResource(path) && !IsStage11JsonResource(path) && !IsBinkResource(path) &&
           !IsICUResource(path) && !IsAudioResource(path);
}

namespace commandlet
{
red::DynArray<res::ResourcePath> ScanResourceDepotDirectories(
    const res::IBackendResourceDepot& depot, const String& dirPath, const char* const pattern)
{
    red::DynArray<String> patterns;
    if (pattern != nullptr)
        patterns.EmplaceBack(pattern);
    return ScanResourceDepotDirectories(depot, dirPath, patterns);
}

red::DynArray<res::ResourcePath> ScanResourceDepotDirectories(
    const res::IBackendResourceDepot& depot, const String& dirPath, const red::DynArray<String>& patterns)
{
    red::DynArray<res::ResourcePath> result;
    red::String top = dirPath;
    if (!top.Empty() && !top.EndsWith("\\"))
        top += "\\";

    red::DynArray<red::String> pending;
    pending.PushBack(top);
    red::DynArray<String> files;
    red::DynArray<String> subdirs;
    for (Uint32 index = 0; index < pending.Size(); ++index)
    {
        files.Clear();
        if (patterns.Empty())
            depot.FindDepotFiles(pending[index], nullptr, files);
        else
            for (const String& pattern : patterns)
                depot.FindDepotFiles(pending[index], pattern.AsChar(), files);

        for (const String& file : files)
        {
            String path = pending[index] + file;
            if (path.Length() < res::ResourcePath::MAX_LENGTH)
                result.PushBack(res::ResourcePath::Build(path));
        }

        subdirs.Clear();
        depot.FindDepotSubDirectories(pending[index], subdirs);
        for (const String& subdir : subdirs)
            pending.PushBack(pending[index] + subdir + "\\");
    }
    return result;
}
}

namespace dependency::prv
{
bool LooksLikeResourcePath(const red::StringView& path)
{
    if (path.Find('.') == red::StringView::npos || path.Find('\\') == red::StringView::npos)
        return false;
    const red::DynArray<red::StringView> parts = red::paths::SplitPath(path);
    if (parts.Size() < 2u)
        return false;
    const red::StringView first = parts.Front();
    return first == "base" || first == "engine" || first == "test" || first == "user" ||
           first == "temp" || first == "gyms" || first == "multi" || first == "dlc" ||
           first.StartsWith("ep");
}

ScanResult ScanResource(
    red::SharedPtr<IFile> file, const res::ResourcePath path, red::DynArray<ScanDependency>& result)
{
    if (!file)
        return ScanResult::FILE_NOT_FOUND;
    if (file->GetSize() == 0u)
        return ScanResult::EMPTY;

    const red::StringView ext = Extension(path);
    if (ext == "csv")
        return ScanStage11Csv(*file, result);
    if (ext == "tweak")
        return ScanStage11Tweak(*file, result);
    if (ext == "script" || ext == "xml")
        return ScanResult::OK;
    if (ext == "link")
    {
        String target;
        if (!red::LoadFileToString(*file, target))
            return ScanResult::IO_ERROR;
        if (target.Length() >= res::ResourcePath::MAX_LENGTH || !LooksLikeResourcePath(target))
            return ScanResult::LINK_ERROR;
        result.PushBack({res::ResourcePath::Build(target), ScanDependency::Type::HARD,
                         ScanDependency::Location::PRIMARY, false});
        return ScanResult::OK;
    }
    if (IsStage11JsonResource(path))
        return ScanResult::NOT_A_RESOURCE;
    if (IsBinkResource(path) || IsICUResource(path) || IsAudioResource(path))
        return ScanResult::NOT_A_RESOURCE;
    return ScanStage11Binary(std::move(file), path, result);
}
}
