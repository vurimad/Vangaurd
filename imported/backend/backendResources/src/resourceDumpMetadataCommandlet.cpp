#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/resourceImportMetadata.h"
#include "../../backendEngine/include/importer.h"

#include "../../../../external/rapidjson/include/rapidjson/writer.h"
#include "../../backendEngine/include/resourceMetadata.h"


class CResourceDumpMetadataCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(CResourceDumpMetadataCommandlet);

public:
	CResourceDumpMetadataCommandlet();
	~CResourceDumpMetadataCommandlet();

	// interface
	virtual Bool Execute(const red::CommandLine& options) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	void SerializeResourceMetadata(rapidjson::Writer<rapidjson::StringBuffer>& writer, res::ResourcePath resourcePath);
};

RTTI_BEGIN_TYPE(CResourceDumpMetadataCommandlet);
RTTI_PARENT_TYPE(ICommandlet);
RTTI_END_TYPE();

CResourceDumpMetadataCommandlet::CResourceDumpMetadataCommandlet()
	: ICommandlet(RED_NAME_CONSTEXPR("resourceDumpMetadata"))
{
}

CResourceDumpMetadataCommandlet::~CResourceDumpMetadataCommandlet()
{
}

Bool CResourceDumpMetadataCommandlet::Execute(const red::CommandLine& options)
{
	red::AbsolutePath filePath;
	String resourcePathString;
	String outputPath;

	if (!options.GetFirstParam("resource", resourcePathString))
	{
		RED_LOG_ERROR("Missing resource path");
		return false;
	}
	
	options.GetFirstParam("output", outputPath);

	res::ResourcePath resourcePath = res::ResourcePath::Build(resourcePathString);
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

	SerializeResourceMetadata(writer, resourcePath);

	FILE* fp;
	if (!outputPath.Empty())
	{
		fopen_s(&fp, outputPath.AsChar(), "w");
		if (fp == nullptr)
		{
			RED_LOG_ERROR("Failed to open file %hs", outputPath.AsChar());
			return false;
		}
	} else {
		fp = stdout;
	}

	fwrite(buffer.GetString(), sizeof(char), buffer.GetSize(), fp);

	if (fp != stdout)
	{
		fclose(fp);
	}

	RED_LOG(buffer.GetString());

	return true;
}

void CResourceDumpMetadataCommandlet::SerializeResourceMetadata(rapidjson::Writer<rapidjson::StringBuffer>& writer, res::ResourcePath resourcePath)
{
	THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get(resourcePath, true);
	if (!meta)
		return;

	THandle<res::ResourceImportMetadata> metadata = meta->GetImportData();
	if (!metadata)
		return;

	writer.StartObject();
	writer.String("m_intermediateHost");
	writer.String(metadata->GetIntermediateHost().AsChar());
	writer.String("m_intermediatePath");
	writer.String(metadata->GetIntermediatePath().AsChar());
	writer.String("m_intermediateAuthor");
	writer.String(metadata->GetIntermediateAuthor().AsChar());
	writer.String("m_assetHost");
	writer.String(metadata->GetAssetHost().AsChar());
	writer.String("m_assetPath");
	writer.String(metadata->GetAssetPath().AsChar());
	writer.String("m_assetVersionControlPath");
	writer.String(metadata->GetAssetVersionControlPath().AsChar());
	writer.String("m_assetVersionControlRevision");
	writer.Int(metadata->GetAssetVersionControlRevision());

	writer.EndObject();
}


const char* CResourceDumpMetadataCommandlet::GetOneLiner() const
{
	return "Dump an animset animations metadata to a JSON file";
}

void CResourceDumpMetadataCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG(" animsetDumpMetadata -animset=<inputfile>");
	RED_LOG("Will read the metadata from all animations in the animset and will write their metadata into the output file.");
}
