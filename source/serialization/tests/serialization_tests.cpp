#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <array>
#include <cstdio>

namespace
{
    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[serializationTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    constexpr vanguard::u32 TestMagic = vanguard::serialization::MakeFourCC('V', 'G', 'T', 'S');
    constexpr vanguard::u32 DataSection = vanguard::serialization::MakeFourCC('D', 'A', 'T', 'A');
    constexpr std::array<vanguard::u8, 16> Payload{0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    bool BuildDocument(ByteArray& bytes, vanguard::serialization::DocumentHeader& header, vanguard::serialization::SectionDescriptor& section)
    {
        namespace filesystem = vanguard::filesystem;
        namespace serialization = vanguard::serialization;

        filesystem::MemoryFileWriter memoryWriter(bytes);
        serialization::BinaryWriter writer(memoryWriter);

        header.magic = TestMagic;
        header.version = {1, 2};
        header.flags = serialization::DocumentFlags::Deterministic;
        header.fileSize = 128;
        header.sectionTableOffset = 80;
        header.sectionCount = 1;

        section.id = DataSection;
        section.version = {1, 0};
        section.flags = serialization::SectionFlags::Streamable;
        section.codec = serialization::Codec::None;
        section.alignmentLog2 = 6;
        section.offset = 64;
        section.storedSize = Payload.size();
        section.logicalSize = Payload.size();
        section.storedCrc64 = serialization::Crc64(Payload.data(), Payload.size());

        if (serialization::WriteDocumentHeader(writer, header) != serialization::Result::Success || !writer.Align(64) ||
            !writer.WriteBytes(Payload.data(), Payload.size()) || !writer.Seek(header.sectionTableOffset) ||
            serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success || !writer.Flush())
        {
            return false;
        }
        return bytes.Size() == header.fileSize;
    }

    vanguard::serialization::Result ReadHeader(ByteArray& bytes, vanguard::serialization::DocumentHeader& header)
    {
        vanguard::filesystem::MemoryFileReader memoryReader(bytes, 0);
        vanguard::serialization::BinaryReader reader(memoryReader);
        return vanguard::serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, {}, header);
    }

    void RefreshHeaderChecksum(ByteArray& bytes)
    {
        const vanguard::u32 checksum = vanguard::serialization::Crc32(bytes.Data(), 36);
        bytes[36] = static_cast<vanguard::u8>(checksum);
        bytes[37] = static_cast<vanguard::u8>(checksum >> 8u);
        bytes[38] = static_cast<vanguard::u8>(checksum >> 16u);
        bytes[39] = static_cast<vanguard::u8>(checksum >> 24u);
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;
    namespace serialization = vanguard::serialization;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "serializationTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    {
        ByteArray bytes(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter memoryWriter(bytes);
        serialization::BinaryWriter writer(memoryWriter);

        Check(writer.WriteBool(true), "write bool");
        Check(writer.WriteU16(0x1234u), "write u16");
        Check(writer.WriteU32(0x89abcdefu), "write u32");
        Check(writer.WriteU64(0x0123456789abcdefull), "write u64");
        Check(writer.WriteI64(-0x102030405060708ll), "write i64");
        Check(writer.WriteF32(123.25f), "write f32");
        Check(writer.WriteF64(-9876.5), "write f64");
        Check(writer.WriteVarUInt(0), "write zero varuint");
        Check(writer.WriteVarUInt(127), "write one-byte varuint");
        Check(writer.WriteVarUInt(128), "write two-byte varuint");
        Check(writer.WriteVarUInt(~vanguard::u64{0}), "write maximum varuint");
        Check(writer.WriteVarInt(-1), "write negative varint");
        Check(writer.WriteVarInt(static_cast<vanguard::i64>(0x7fffffffffffffffll)), "write maximum varint");
        Check(writer.Align(64), "write canonical alignment");
        Check((writer.Position() & 63u) == 0, "writer alignment");

        filesystem::MemoryFileReader memoryReader(bytes, 0);
        serialization::BinaryReader reader(memoryReader);
        bool boolean = false;
        vanguard::u16 u16Value = 0;
        vanguard::u32 u32Value = 0;
        vanguard::u64 u64Value = 0;
        vanguard::i64 i64Value = 0;
        vanguard::f32 f32Value = 0;
        vanguard::f64 f64Value = 0;
        vanguard::u64 variable = 0;
        vanguard::i64 signedVariable = 0;

        Check(reader.ReadBool(boolean) && boolean, "read bool");
        Check(reader.ReadU16(u16Value) && u16Value == 0x1234u, "little-endian u16");
        Check(reader.ReadU32(u32Value) && u32Value == 0x89abcdefu, "little-endian u32");
        Check(reader.ReadU64(u64Value) && u64Value == 0x0123456789abcdefull, "little-endian u64");
        Check(reader.ReadI64(i64Value) && i64Value == -0x102030405060708ll, "signed integer round trip");
        Check(reader.ReadF32(f32Value) && f32Value == 123.25f, "f32 round trip");
        Check(reader.ReadF64(f64Value) && f64Value == -9876.5, "f64 round trip");
        Check(reader.ReadVarUInt(variable) && variable == 0, "zero varuint");
        Check(reader.ReadVarUInt(variable) && variable == 127, "one-byte varuint");
        Check(reader.ReadVarUInt(variable) && variable == 128, "two-byte varuint");
        Check(reader.ReadVarUInt(variable) && variable == ~vanguard::u64{0}, "maximum varuint");
        Check(reader.ReadVarInt(signedVariable) && signedVariable == -1, "negative varint");
        Check(reader.ReadVarInt(signedVariable) && signedVariable == static_cast<vanguard::i64>(0x7fffffffffffffffll), "maximum varint");
        Check(reader.Align(64), "read canonical alignment");
        Check(reader.GetRemaining() == 0, "primitive stream consumed");

        Check(bytes[1] == 0x34 && bytes[2] == 0x12, "canonical little-endian bytes");
    }

    {
        ByteArray bytes(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter writerFile(bytes);
        serialization::BinaryReader wrongReader(writerFile);
        Check(wrongReader.GetStatus() == serialization::Result::WrongStreamMode, "reader rejects writer stream");

        filesystem::MemoryFileReader readerFile(bytes, 0);
        serialization::BinaryWriter wrongWriter(readerFile);
        Check(wrongWriter.GetStatus() == serialization::Result::WrongStreamMode, "writer rejects reader stream");
    }

    {
        ByteArray bytes(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter writerFile(bytes);
        serialization::BinaryWriter writer(writerFile);
        Check(!writer.Seek(1) && writer.GetStatus() == serialization::Result::InvalidLayout, "writer rejects sparse seek");
    }

    {
        ByteArray bytes(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter writerFile(bytes);
        serialization::BinaryWriter writer(writerFile);
        Check(writer.WriteU8(7) && writer.Align(4), "write padding fixture");
        bytes[2] = 1;

        filesystem::MemoryFileReader readerFile(bytes, 0);
        serialization::BinaryReader reader(readerFile);
        vanguard::u8 value = 0;
        Check(reader.ReadU8(value), "read padding fixture");
        Check(!reader.Align(4) && reader.GetStatus() == serialization::Result::InvalidEncoding, "nonzero padding rejection");
    }

    ByteArray document(memory::pools::Serialization::GetInstance());
    serialization::DocumentHeader expectedHeader;
    serialization::SectionDescriptor expectedSection;
    Check(BuildDocument(document, expectedHeader, expectedSection), "build Vanguard document");
    Check(document[0] == 'V' && document[1] == 'G' && document[2] == 'T' && document[3] == 'S', "Vanguard format-specific magic");
    Check(document[4] == serialization::DocumentHeader::LittleEndian && document[5] == serialization::DocumentHeader::EncodingVersion && document[6] == 40 &&
              document[7] == 0,
          "stable document prefix");

    {
        filesystem::MemoryFileReader memoryReader(document, 0);
        serialization::BinaryReader reader(memoryReader);
        serialization::DocumentHeader header;
        Check(serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, {}, header) == serialization::Result::Success, "read valid header");
        Check(header.magic == expectedHeader.magic && header.version == expectedHeader.version && header.fileSize == expectedHeader.fileSize &&
                  header.sectionTableOffset == expectedHeader.sectionTableOffset && header.sectionCount == 1,
              "header values");

        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        Check(serialization::ReadSectionTable(reader, header, {}, sections) == serialization::Result::Success, "read valid section table");
        Check(sections.Size() == 1 && sections[0].id == expectedSection.id && sections[0].offset == expectedSection.offset &&
                  sections[0].storedSize == Payload.size(),
              "section descriptor values");

        vanguard::u8 scratch[7];
        Check(serialization::ValidateSectionChecksum(reader, sections[0], scratch, sizeof(scratch)) == serialization::Result::Success,
              "streaming section checksum");
    }

    Check(serialization::Crc32("123456789", 9) == 0xcbf43926u, "standard CRC32 vector");
    Check(serialization::Crc64("123456789", 9) == 0x995dc9bbdf1939faull, "standard CRC64/XZ vector");
    {
        const vanguard::u64 whole = serialization::Crc64(Payload.data(), Payload.size());
        vanguard::u64 incremental = serialization::Crc64(Payload.data(), 5);
        incremental = serialization::Crc64(Payload.data() + 5, Payload.size() - 5, incremental);
        Check(whole == incremental, "incremental CRC64");
    }

    {
        filesystem::MemoryFileReader memoryReader(document, 0);
        serialization::BinaryReader reader(memoryReader);
        serialization::DocumentHeader header;
        serialization::ReadLimits limits;
        limits.maximumFileSize = document.Size() - 1;
        Check(serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, limits, header) == serialization::Result::LimitExceeded, "file-size limit");
    }

    {
        filesystem::MemoryFileReader memoryReader(document, 0);
        serialization::BinaryReader reader(memoryReader);
        serialization::DocumentHeader header;
        Check(serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, {}, header) == serialization::Result::Success, "header for section limit");
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        serialization::ReadLimits limits;
        limits.maximumSections = 0;
        Check(serialization::ReadSectionTable(reader, header, limits, sections) == serialization::Result::LimitExceeded, "section-count limit");
    }

    {
        document[0] ^= 0xffu;
        serialization::DocumentHeader header;
        Check(ReadHeader(document, header) == serialization::Result::IntegrityFailure, "corrupt header checksum");
        document[0] ^= 0xffu;
    }

    {
        document[4] = 2;
        RefreshHeaderChecksum(document);
        serialization::DocumentHeader header;
        Check(ReadHeader(document, header) == serialization::Result::UnsupportedByteOrder, "unsupported byte order rejection");
        document[4] = serialization::DocumentHeader::LittleEndian;
        RefreshHeaderChecksum(document);
    }

    {
        document[12] = 0x80;
        RefreshHeaderChecksum(document);
        serialization::DocumentHeader header;
        Check(ReadHeader(document, header) == serialization::Result::InvalidEncoding, "unknown document flag rejection");
        document[12] = static_cast<vanguard::u8>(serialization::DocumentFlags::Deterministic);
        RefreshHeaderChecksum(document);
    }

    {
        filesystem::MemoryFileWriter memoryWriter(document);
        serialization::BinaryWriter writer(memoryWriter);
        Check(writer.Seek(0), "seek for wrong magic");
        Check(writer.WriteU32(serialization::MakeFourCC('B', 'A', 'D', '!')), "patch wrong magic");
        Check(writer.Seek(36) && writer.WriteU32(serialization::Crc32(document.Data(), 36)), "patch header checksum");
        serialization::DocumentHeader header;
        Check(ReadHeader(document, header) == serialization::Result::InvalidMagic, "wrong magic rejection");
        Check(writer.Seek(0) && writer.WriteU32(TestMagic), "restore magic");
        Check(writer.Seek(36) && writer.WriteU32(serialization::Crc32(document.Data(), 36)), "restore header checksum");
    }

    {
        serialization::DocumentHeader incompatible = expectedHeader;
        incompatible.version = {2, 0};
        filesystem::MemoryFileWriter memoryWriter(document);
        serialization::BinaryWriter writer(memoryWriter);
        Check(writer.Seek(0), "seek incompatible header");
        Check(serialization::WriteDocumentHeader(writer, incompatible) == serialization::Result::Success, "write incompatible header");
        serialization::DocumentHeader header;
        Check(ReadHeader(document, header) == serialization::Result::UnsupportedVersion, "major version rejection");
        Check(writer.Seek(0), "seek compatible header");
        Check(serialization::WriteDocumentHeader(writer, expectedHeader) == serialization::Result::Success, "restore compatible header");
    }

    {
        filesystem::MemoryFileWriter memoryWriter(document);
        serialization::BinaryWriter writer(memoryWriter);
        Check(writer.Seek(expectedHeader.sectionTableOffset + 16), "seek invalid section");
        Check(writer.WriteU64(120), "patch invalid section offset");

        filesystem::MemoryFileReader memoryReader(document, 0);
        serialization::BinaryReader reader(memoryReader);
        serialization::DocumentHeader header;
        Check(serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, {}, header) == serialization::Result::Success, "header survives invalid section");
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        Check(serialization::ReadSectionTable(reader, header, {}, sections) == serialization::Result::InvalidLayout, "out-of-bounds section rejection");

        Check(writer.Seek(expectedHeader.sectionTableOffset + 16), "seek valid section");
        Check(writer.WriteU64(expectedSection.offset), "restore valid section offset");
    }

    {
        const auto payloadOffset = static_cast<vanguard::u32>(expectedSection.offset);
        document[payloadOffset] ^= 1u;
        filesystem::MemoryFileReader memoryReader(document, 0);
        serialization::BinaryReader reader(memoryReader);
        vanguard::u8 scratch[16];
        Check(serialization::ValidateSectionChecksum(reader, expectedSection, scratch, sizeof(scratch)) == serialization::Result::IntegrityFailure,
              "payload corruption rejection");
        document[payloadOffset] ^= 1u;
    }

    {
        constexpr vanguard::u8 malformedVarUInt[]{0x80, 0x00};
        filesystem::MemoryFileReader readerFile(malformedVarUInt, sizeof(malformedVarUInt), 0);
        serialization::BinaryReader reader(readerFile);
        vanguard::u64 value = 0;
        Check(!reader.ReadVarUInt(value) && reader.GetStatus() == serialization::Result::InvalidEncoding, "non-canonical varuint rejection");
    }

    {
        constexpr vanguard::u8 overflowingVarUInt[]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02};
        filesystem::MemoryFileReader readerFile(overflowingVarUInt, sizeof(overflowingVarUInt), 0);
        serialization::BinaryReader reader(readerFile);
        vanguard::u64 value = 0;
        Check(!reader.ReadVarUInt(value) && reader.GetStatus() == serialization::Result::Overflow, "overflowing varuint rejection");
    }

    {
        filesystem::MemoryFileReader shortFile(static_cast<const vanguard::u8*>(document.Data()), 12, 0);
        serialization::BinaryReader reader(shortFile);
        serialization::DocumentHeader header;
        Check(serialization::ReadDocumentHeader(reader, TestMagic, {1, 0, 3}, {}, header) == serialization::Result::EndOfStream, "truncated header rejection");
    }

    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[serializationTests] Vanguard format primitives passed");
    }
    return g_failures == 0 ? 0 : 1;
}
