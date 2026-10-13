/*
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/editorObjectIdPath.h"
#include "messageFwd.h"
#include "messageResourcePathWrapper.h"

namespace tools
{
	class RED_CORE_API IMessageLocationGotoFacilitator
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IMessageLocationGotoFacilitator();

		virtual void Goto( const EditorObjectIDPath& path ) = 0;
		virtual void Goto( const String& weblink ) = 0;
	};

	// represents a generic location that user can inspect
	class RED_CORE_API IMessageLocation : public ISerializable
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( IMessageLocation );

	public:
		static MessageLocationPtr Build( const res::ResourcePath& resourcePath );
		static MessageLocationPtr Build( const res::ResourcePath& resourcePath, const math::Box& boundingBox );
		static MessageLocationPtr Build( const res::ResourcePath& resourcePath, const math::Vector3& point );
		static MessageLocationPtr Build( const EditorObjectIDPath& editorObjectPath );
		static MessageLocationPtr Build( const res::ResourcePath& resourcePath, const String& link, const String& text = String::EMPTY() );

		static MessageLocationPtr BuildFromString( red::StringView location );

		virtual ~IMessageLocation();
		virtual String ToString() const = 0;
		virtual String SerializeToString() const = 0;
		virtual Bool DeserializeFromString( red::StringView string ) = 0;
		virtual res::ResourcePath GetResourcePath() const = 0;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) = 0;
	};

	// resource - clicking on it will open editor for that resource
	class RED_CORE_API MessageLocation_Resource final : public IMessageLocation
	{
		RTTI_DECLARE_TYPE( MessageLocation_Resource );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MessageLocation_Resource();
		explicit MessageLocation_Resource( const res::ResourcePath& resourcePath );

		virtual String ToString() const override;
		virtual String SerializeToString() const override;
		virtual Bool DeserializeFromString( red::StringView string ) override;
		virtual res::ResourcePath GetResourcePath() const override;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) override;

	private:
		MessageResourcePath m_path;
	};

	// editor object - clicking on it will open editor and focus camera on the object
	class RED_CORE_API MessageLocation_EditorObject final : public IMessageLocation
	{
		RTTI_DECLARE_TYPE( MessageLocation_EditorObject );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MessageLocation_EditorObject();
		explicit MessageLocation_EditorObject( const EditorObjectIDPath& objectPath );
		explicit MessageLocation_EditorObject( const EditorObjectID& objectID );

		virtual String ToString() const override;
		virtual String SerializeToString() const override;
		virtual Bool DeserializeFromString( red::StringView string ) override;
		virtual res::ResourcePath GetResourcePath() const override;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) override;

		const EditorObjectIDPath &GetEditorObjectIDPath() const;

	private:
		EditorObjectIDPath m_path;
	};

	// 3D point in a resource - clicking on it will open editor and focus camera on the coordinates
	class RED_CORE_API MessageLocation_Point final : public IMessageLocation
	{
		RTTI_DECLARE_TYPE( MessageLocation_Point );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MessageLocation_Point();
		explicit MessageLocation_Point( const res::ResourcePath& resourcePath, const math::Vector3& point );

		virtual String ToString() const override;
		virtual String SerializeToString() const override;
		virtual Bool DeserializeFromString( red::StringView string ) override;
		virtual res::ResourcePath GetResourcePath() const override;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) override;

	private:
		MessageResourcePath m_resourcePath;
		math::Vector3 m_point;
	};

	// bounding box in a resource token - clicking on it will open editor and focus camera on the bounds
	class RED_CORE_API MessageLocation_BoundingBox final : public IMessageLocation
	{
		RTTI_DECLARE_TYPE( MessageLocation_BoundingBox );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MessageLocation_BoundingBox();
		explicit MessageLocation_BoundingBox( const res::ResourcePath& resourcePath, const math::Box& boundingBox );

		virtual String ToString() const override;
		virtual String SerializeToString() const override;
		virtual Bool DeserializeFromString( red::StringView string ) override;
		virtual res::ResourcePath GetResourcePath() const override;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) override;

	private:
		MessageResourcePath m_resourcePath;
		math::Box m_box;
	};

	// open web browser with given link
	class RED_CORE_API MessageLocation_Webpage final : public IMessageLocation
	{
		RTTI_DECLARE_TYPE( MessageLocation_Webpage );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MessageLocation_Webpage();
		explicit MessageLocation_Webpage( const res::ResourcePath& resourcePath, const String& link, const String& text = String::EMPTY() );

		virtual String ToString() const override;
		virtual String SerializeToString() const override;
		virtual Bool DeserializeFromString( red::StringView string ) override;
		virtual res::ResourcePath GetResourcePath() const override;
		virtual void Goto( IMessageLocationGotoFacilitator* facilitator ) override;

	private:
		MessageResourcePath m_resourcePath;
		String m_link;
		String m_text;
	};

	// TODO more location types: e.g. GlobalNodeID, point, camera position+orientation

} // tools

#define RED_NULL_LOCATION			tools::MessageLocationPtr()
#define RED_LOCATION( path, ... )	tools::IMessageLocation::Build( path, ##__VA_ARGS__ )
