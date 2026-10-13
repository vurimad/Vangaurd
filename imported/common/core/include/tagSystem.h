/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include <type_traits>                // For std::is_base_of.
#include "tagList.h"
#include "../../redReflection/include/weakHandle.h"

class ISerializable;

namespace red
{
	/// Tag matching operator, used when dealing with multiple tags (in a TagList).
	enum class TagMatch : Uint8
	{
		All,             ///< Match all tags (in a TagList).
		Any              ///< Match any of the tags (in a TagList).
	};

	/// Listener for tag/object register/unregister events
	class RED_CORE_API ITagSystemListener
	{
	public:
		virtual ~ITagSystemListener() {}

		virtual void OnTagRegistered() = 0;
		virtual void OnTagUnregistered() = 0;
		virtual void OnTaggedObjectRegistered() = 0;
		virtual void OnTaggedObjectUnregistered() = 0;
	};

	/// Tag system keeping registry of objects (ISerialize) associated with their tags.
	/// The registry does not own the objects, just keeps weak pointers to them.
	///
	/// All objects registered here must be used with THandle/WeakHandle, it is impossible
	/// to register any object here that does not have THandle pointing to it.
	class RED_CORE_API TagSystem
	{
		RTTI_DECLARE_TYPE( TagSystem );

	public:
		/// Functor used on ForEach methods, executed on each matching object.
		//typedef std::function< void(ISerializable* obj) > IteratorLambda;
		typedef Uint64 IdHash;
		typedef std::function< void( IdHash hash ) > IteratorLambda;

		TagSystem();
		~TagSystem();

		/// Reset tag manager, remove all tags etc.
		void Clear();
		void ClearListeners();

		/// Register tagged object.
		/// Tag system will keep week handles to the registered object.
		void Register( const IdHash hash, const CName& tag );
		void Register( const IdHash hash, const TagList& tags );

		/// Unregister tagged object.
		/// Tag system will remove week handles to the unregistered object.
		void Unregister( const IdHash hash, const CName& tag );
		void Unregister( const IdHash hash, const TagList& tags );
		void Unregister( const IdHash hash );

		/// Register listener for a tag(s).
		void RegisterListener( const SharedPtr< ITagSystemListener >& listener, CName tag );
		void RegisterListener( const SharedPtr< ITagSystemListener >& listener, const TagList& tags );

		/// Unregister listener for a tag(s).
		void UnregisterListener( const SharedPtr< ITagSystemListener >& listener, CName tag );
		void UnregisterListener( const SharedPtr< ITagSystemListener >& listener, const TagList& tags );

		/// Get any object that has matching tag(s).
		// @TODO It might be needed to change the logic as ultimately always returns first one that matches
		Bool GetAnyMatching( const CName& tag, IdHash& foundElem ) const;
		Bool GetAnyMatching( const TagList& tags, TagMatch matchOption, IdHash& foundElem ) const;

		/// Get all objects with matching tag(s).
		/// The output array is not cleared, found objects are just pushed back.
		void GetAllMatching( const CName& tag, red::DynArray< IdHash >& gos ) const;
		void GetAllMatching( const TagList& tags, TagMatch matchOption, red::DynArray< IdHash >& gos ) const;

		/// Iterate over every object with matching tag and call a functor on it.
		/// Be careful when using this method, it can be slow.
		/// @return was any fitting object found?
		Bool ForEach( const CName& tag, const IteratorLambda& iteratorLambda ) const;
		/// Iterate over every object with matching tags and call a functor on it.
		/// Be careful when using this method, it can be slow.
		/// @return was any fitting object found?
		Bool ForEach( const TagList& tags, TagMatch matchOption, const IteratorLambda& iteratorLambda ) const;


		/// Get all used tags.
		/// This will most likely be used only by editor.
		void GetAllTags( red::DynArray< CName >& outTags ) const;

	private:
		/// Notify listeners about tag being registered
		void NotifyTagRegisterListeners( CName tag ) const;

		/// Notify listeners about tag being unregistered
		void NotifyTagUnregisterListeners( CName tag ) const;

		/// Notify listeners about tagged object being registered
		void NotifyTaggedObjectRegisterListeners( CName tag ) const;

		/// Notify listeners about tagged object being unregistered
		void NotifyTaggedObjectUnregisterListeners( CName tag ) const;

	private:
		/// A structure used per each tag in the system, keeps the list of all objects registered with that tag.
		struct TagEntry
		{
			red::DynArray< IdHash > m_hashList;

			/// @todo DP: make thread safety faster by locking for shorter periods of time
//mutable red::RWLock m_lock;                         /// Lock used when modifying this tag entry (adding, removing objects).

			TagEntry() : m_hashList( red::PoolEngine() ) {}
		};

		typedef red::HashMap< CName, TagEntry > TagToEntriesMap;
		typedef red::HashMap< CName, red::DynArray< red::SharedPtr< ITagSystemListener > > > TagToListenersMap;

		TagToEntriesMap m_tagToObjList;
		TagToListenersMap m_tagToListenersList;				// PMTODO maybe it would be better to keep listeners in TagToObjList?
		mutable red::RWSpinLock m_tagsLock;                 /// Lock used when modifying tags (adding, removing).
		mutable red::RWSpinLock m_listenersLock;            /// Lock used when modifying listeners (adding, removing).
	};

} // red
