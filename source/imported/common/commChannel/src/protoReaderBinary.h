/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class COMMCHANNEL_API ProtoReaderBinary : public comm::IProtoReader
{
public:
	ProtoReaderBinary( const red::UniqueBuffer& buffer );

	virtual comm::EMessageID PeekTypeHash() const override;
	virtual void Error(STATIC_CHECK_PRINTF_MSC const red::AnsiChar* txt, ...) const override;
	virtual Bool BeginObject(const red::AnsiChar* typeName, const Uint32 typeHash) override;
	virtual void EndObject() override;
	virtual Uint32 StartPropsBlock() override;
	virtual Bool BeginProperty(Uint32& outNameHash) override;
	virtual void EndProperty() override;
	virtual Bool ReadFloat(Float& outResult) override;
	virtual Bool ReadDouble(Double& outResult) override;
	virtual Bool ReadUint8(Uint8& outResult) override;
	virtual Bool ReadUint16(Uint16& outResult) override;
	virtual Bool ReadUint32(Uint32& outResult) override;
	virtual Bool ReadUint64(Uint64& outResult) override;
	virtual Bool ReadInt8(Int8& outResult) override;
	virtual Bool ReadInt16(Int16& outResult) override;
	virtual Bool ReadInt32(Int32& outResult) override;
	virtual Bool ReadInt64(Int64& outResult) override;
	virtual Bool ReadBool(Bool& outResult) override;
	virtual Bool ReadString(red::String& outResult) override;
	virtual Uint32 BeginArray() override;
	virtual void EndArray() override;

private:
	Bool Read( void* data, Uint32 size );

	void PushScopedSizeCounter();
	void PopScopedSizeCounter();

	const red::UniqueBuffer& m_buffer;
	Uint32 m_readPosition;

	ProtoUtils::ProtoStack<Uint32> m_nextEndPosition;
};
