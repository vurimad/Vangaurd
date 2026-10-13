#pragma once

#include "repReplicableHandler.h"

class RED_REFLECTION_API ISerializableReplicationHandler : public rep::IReplicableHandler
{
public:
	typedef rep::IReplicableHandler Super;

	void CreateReplicable( const rep::ReplicableContext& ctx, const rep::ObjectPtr ptr, const net::ReadBitBuffer& createData, rep::CreateObjectResult& result );
	void DeleteReplicable( const rep::ReplicableContext& ctx, const rep::ObjectPtr ptr ) override;
};
