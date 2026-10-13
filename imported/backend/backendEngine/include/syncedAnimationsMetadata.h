/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/backendData.h"

namespace res
{


	struct SyncedAnimationData
	{
		RTTI_DECLARE_TYPE( SyncedAnimationData )

		CName m_animationName;
		String m_animsetPath;
		Int32 m_startFrame;
		Int32 m_endFrame;
	};
	//
	// This class holds metadata related to syncing animations with this resource.
	// Position of Root bone in source scene is stored here. Root bone of all actors should be at the center of scene,
	// If there are multiple actors in scene, their positions in source file can be recreated based on that information.
	// Names of synced animations and their animsets are also stored, together with start time, end time and length of them.
	//

	class BACKEND_ENGINE_API SyncedAnimationsMetadata : public IBackendData
	{ 
		RTTI_DECLARE_TYPE( SyncedAnimationsMetadata );

	public:
		SyncedAnimationsMetadata();

		RED_INLINE const Transform& GetOriginTransform() const { return m_originTransform; }

		RED_INLINE const Uint32 GetSyncedAnimationsCount() const { return m_syncedAnimations.Size(); }
		RED_INLINE const CName& GetSyncedAnimationName(const Uint32& idx) const { return m_syncedAnimations[idx].m_animationName; }
		RED_INLINE const String& GetSyncedAnimationAnimsetPath( const Uint32& idx ) const { return m_syncedAnimations[ idx ].m_animsetPath; }
		RED_INLINE const Int32& GetSyncedAnimationStart(const Uint32& idx) const { return m_syncedAnimations[idx].m_startFrame; }
		RED_INLINE const Int32& GetSyncedAnimationEnd( const Uint32& idx ) const { return m_syncedAnimations[idx].m_endFrame; }
		RED_INLINE Float GetSyncedAnimationStartSeconds( const Uint32& idx ) const { return GetSyncedAnimationStart( idx ) * ( 1.f / 30 ); }	// Conversion should be done by exporter/importer, but to avoid big code changes we go for this hackfix
		RED_INLINE Float GetSyncedAnimationEndSeconds( const Uint32& idx ) const { return GetSyncedAnimationEnd( idx ) * (1.f / 30); }			// Conversion should be done by exporter/importer, but to avoid big code changes we go for this hackfix

		void AddSyncedAnimation( const String& name, const String& animsetName, const Int32& start, const Int32& end);
		void SetOriginTransform( const Transform& originTransform ) { m_originTransform = originTransform; };

	private:
		Transform m_originTransform;
		red::DynArray< SyncedAnimationData > m_syncedAnimations;
	};

} // namespace res
