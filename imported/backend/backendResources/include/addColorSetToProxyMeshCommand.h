/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class CMesh;

namespace tools
{
	//-------------------------------------------------------------------------------------------------------------------------

	class BACKEND_RESOURCES_API AddColorSetToProxyMeshOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE(AddColorSetToProxyMeshOptions);

	public:
		virtual Bool NeedOptions() const override;

		RED_INLINE const String& GetColorSetName() const { return m_colorSetName; }
		RED_INLINE const String& GetBaseColorPath() const { return m_baseColorPath; }
		RED_INLINE const String& GetNormalPath() const { return m_normalPath; }
		RED_INLINE const String& GetMetalnessPath() const { return m_metalnessPath; }
		RED_INLINE const String& GetRoughnessPath() const { return m_roughnessPath; }
		RED_INLINE const String& GetEmissivePath() const { return m_emissivePath; }

		RED_INLINE void SetColorSetName(const String& var) { m_colorSetName = var; }
		RED_INLINE void SetBaseColorPath(const String& var) { m_baseColorPath = var; }
		RED_INLINE void SetNormalPath(const String& var) { m_normalPath = var; }
		RED_INLINE void SetMetalnessPath(const String& var) { m_metalnessPath = var; }
		RED_INLINE void SetRoughnessPath(const String& var) { m_roughnessPath = var; }
		RED_INLINE void SetEmissivePath(const String& var) { m_emissivePath = var; }
	private:
		String m_colorSetName = String::EMPTY();
		String m_baseColorPath = String::EMPTY();
		String m_normalPath = String::EMPTY();
		String m_metalnessPath = String::EMPTY();
		String m_roughnessPath = String::EMPTY();
		String m_emissivePath = String::EMPTY();
	};

	class BACKEND_RESOURCES_API AddColorSetToProxyMesh : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE(AddColorSetToProxyMesh);

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;
	};

} // tools
