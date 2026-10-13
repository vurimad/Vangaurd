/**
* Copyright © 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// Basic rule:
// 0-3: uint 32-bit : object/property/array hash
// 4-5: uint 16-bit : size in bytes

// Example object binary format:
// 0-3: uint 32-bit : object hash
// 4-5: uint 16-bit : object size (starts after this variable)
//         content with size 'object size':
// 6-9:		uint 32-bit : property (params) count
//		   Property (param) format:
// 10-13:	uint 32-bit : property hash
// 14-15:	uint 16-bit : property size (starts after this variable)
//			   ... property content with size 'property size', let's say it's an array

class COMMCHANNEL_API ProtoWriterBinary : public comm::IProtoWriter
{
public:
	ProtoWriterBinary();

	virtual void BeginObject(const red::AnsiChar* typeName, const Uint32 typeHash) override;
	virtual void EndObject() override;
	virtual void BeginParams(const Uint32 maxParams) override;
	virtual void EndParams(Uint32 numSavedParams) override;
	virtual void BeginParam(const red::AnsiChar* name, const Uint32 nameHash) override;
	virtual void EndParam() override;
	virtual void WriteNull() override;
	virtual void WriteFloat(const Float value) override;
	virtual void WriteDouble(const Double value) override;
	virtual void WriteUint8(const Uint8 value) override;
	virtual void WriteUint16(const Uint16 value) override;
	virtual void WriteUint32(const Uint32 value) override;
	virtual void WriteUint64(const Uint64 value) override;
	virtual void WriteInt8(const Int8 value) override;
	virtual void WriteInt16(const Int16 value) override;
	virtual void WriteInt32(const Int32 value) override;
	virtual void WriteInt64(const Int64 value) override;
	virtual void WriteBool(const Bool value) override;
	virtual void WriteString(const red::String& value) override;
	virtual void BeginArray(const red::AnsiChar* typeName, const Uint32 typeHash, const Uint32 numElements) override;
	virtual void EndArray() override;

	red::UniqueBuffer MoveBuffer();

private:
	void Write( const void* data, Uint32 size );
	void ReallocteBuffer( Uint32 capacity );

	void PushScopedSizeCounter();
	void PopScopedSizeCounter();

	red::UniqueBuffer m_buffer;
	Uint32 m_writePosition;

	ProtoUtils::ProtoStack<Uint32> m_paramsPositionStack;
	ProtoUtils::ProtoStack<Uint32> m_propetryPositionStack;

};
