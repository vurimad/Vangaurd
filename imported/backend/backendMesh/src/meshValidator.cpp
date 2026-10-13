
#include "build.h"
#include "meshValidator.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/renderData/include/renderResource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshValidator, tools );
	RTTI_PARENT_TYPE( IValidator );
RTTI_END_TYPE();

namespace tools
{
	static const CName sGeneralValidationRuleName = RED_NAME_CONSTEXPR( "all" );
	static const CName sNoLODsRuleName = RED_NAME_CONSTEXPR( "noLODs" );
	static const CName sGarmentStreamRuleName = RED_NAME_CONSTEXPR( "garmentStream" );
	static const CName sRedundantLodRuleName = RED_NAME_CONSTEXPR( "redundantLOD" );
	static const CName sDegradationRateRuleName = RED_NAME_CONSTEXPR( "LODdegradationRate" );
	static const CName sMissingLodsRuleName = RED_NAME_CONSTEXPR( "missingLODs" );
	static const CName sNoTrianglesRuleName = RED_NAME_CONSTEXPR( "noTriangles" );
	static const CName sNoLastLODreduction = RED_NAME_CONSTEXPR( "noLastLODreduction" );
	static const CName sDestructionMemorySizeRuleName = RED_NAME_CONSTEXPR( "destructionMemorySize" );

	red::HashMap< CName, Rule > MeshValidator::s_rulesMap;
	red::HashMap< CName, RuleCondition > MeshValidator::s_rulesConditionsMap;

	enum class ConditionType : Uint16
	{
		PrefixPath,
		SuffixPath,
		ContainmentPath,
		PrefixFilename,
		SuffixFilename,
		ContainmentFilename
	};

	class PathCondition
	{
		RTTI_DECLARE_TYPE( PathCondition );

	public:
		PathCondition(){}

		Bool Match( const res::ResourcePath& path ) const
		{
			switch ( m_type )
			{
			case ConditionType::PrefixPath:
				return path.ToStringView().StartsWith( m_path );

			case ConditionType::SuffixPath:
				return red::paths::ExtractParentPath( path.ToStringView() ).EndsWith( m_path );

			case ConditionType::ContainmentPath:
				return red::paths::ExtractParentPath( path.ToStringView() ).Contains( m_path );

			case ConditionType::PrefixFilename:
				return red::paths::ExtractFileName( path.ToStringView() ).BeginsWith( m_path );

			case ConditionType::SuffixFilename:
				return red::paths::ExtractFileName( path.ToStringView() ).EndsWith( m_path );

			case ConditionType::ContainmentFilename:
				return red::paths::ExtractFileName( path.ToStringView() ).Contains( m_path );
			}
			return m_negate;
		}

	private:
		ConditionType m_type;
		String m_path;
		Bool m_negate = false;
	};

	class Rule
	{
	public:
		struct Context
		{
			red::DynArray< Uint32 > lodTriangleCount;
		};

		Rule( red::FixedSizeFunction< Bool( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Context& ctx ) > definition )
			: m_definition( definition )
		{
		}

		Bool operator()( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Context& ctx ) const
		{
			return m_definition( errorHandler, mesh, ctx );
		}

	private:
		red::FixedSizeFunction< Bool( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Context& ctx ) > m_definition;
	};

	class RuleCondition
	{
		RTTI_DECLARE_TYPE( RuleCondition );

	public:
		RuleCondition() = default;

		const CName GetName() const { return m_name; }

		Bool IsIncluded( const res::ResourcePath& path ) const
		{
			if ( m_includePathConditions.Empty() )
			{
				return true;
			}

			for ( const red::DynArray< PathCondition >& conditions : m_includePathConditions )
			{
				Bool matches = true;
				for ( const PathCondition& condition : conditions )
				{
					matches &= condition.Match( path );
				}

				if ( matches )
				{
					return true;
				}
			}
			return false;
		}

		Bool IsExcluded( const res::ResourcePath& path ) const
		{
			for ( const red::DynArray< PathCondition >& conditions : m_excludePathConditions )
			{
				Bool matches = true;
				for ( const PathCondition& condition : conditions )
				{
					matches &= condition.Match( path );
				}

				if ( matches )
				{
					return true;
				}
			}
			return false;
		}

	private:
		CName m_name;
		red::DynArray< red::DynArray< PathCondition > > m_includePathConditions;
		red::DynArray< red::DynArray< PathCondition > > m_excludePathConditions;
	};

	class RulesConditions : public ISerializable
	{
		RTTI_DECLARE_TYPE( RulesConditions );

	public:
		RulesConditions() {}

		const red::DynArray< RuleCondition >& GetRules() const { return m_rules; }

	private:
		red::DynArray< RuleCondition > m_rules;
	};

	MeshValidator::MeshValidator()
		: IValidator( ClassID< CMesh >() )
	{
	}

	const red::HashMap< CName, Rule >& MeshValidator::GetRulesMap() const
	{
		if ( s_rulesMap.Empty() )
		{
			FillRulesMap();
		}
		return s_rulesMap;
	}

	const red::HashMap< CName, RuleCondition >& MeshValidator::GetConditionsMap() const
	{
		if ( s_rulesConditionsMap.Empty() )
		{
			FillConditionsMap();
		}
		return s_rulesConditionsMap;
	}

	void MeshValidator::FillRulesMap() const
	{
		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				mesh::DataViewRO view( *mesh );
				if ( view.GetNumLODs() == 0 )
				{
					errorHandler.Report( tools::MessageSeverity::Error, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), "There are no LODs in this mesh. A mesh needs LODs" );
					return true;
				}

				return false;
			} );

			s_rulesMap.Insert( sNoLODsRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				Uint32 lastLODTriangleCount = std::numeric_limits< Uint32 >::max();
				for ( Uint32 lodIndex : ctx.lodTriangleCount.Indices() )
				{
					Uint32 lodTriangleCount = ctx.lodTriangleCount[ lodIndex ];
					if ( ceilf( lastLODTriangleCount / 2.f ) < lodTriangleCount )
					{
						errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ),
							String::Printf( "LOD degradation rate is too small (<0.5). LOD %d triangle count %d, LOD %d triangle count %d, degradation rate %f",
								lodIndex - 1,
								lastLODTriangleCount,
								lodIndex,
								lodTriangleCount,
								1.0f - (Float)lodTriangleCount / lastLODTriangleCount
							).AsChar()
						);
					}
					lastLODTriangleCount = ctx.lodTriangleCount[ lodIndex ];
				}
				return false; // no errors here - just warnings
			} );

			s_rulesMap.Insert( sDegradationRateRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				Uint32 lastLODTriangleCount = std::numeric_limits< Uint32 >::max();
				for ( Uint32 lodIndex : ctx.lodTriangleCount.Indices() )
				{
					if ( lastLODTriangleCount < 1200 )
					{
						errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), String::Printf( "LOD %d is not necessary, LOD %d has less than 1200 triangles", lodIndex, lodIndex - 1 ).AsChar() );
					}
					lastLODTriangleCount = ctx.lodTriangleCount[ lodIndex ];
				}
				return false; // no errors here - just warnings
			} );

			s_rulesMap.Insert( sRedundantLodRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				Bool hasErrors = false;
				for ( Uint32 lodIndex : ctx.lodTriangleCount.Indices() )
				{
					if ( ctx.lodTriangleCount[ lodIndex ] == 0 )
					{
						errorHandler.Report( tools::MessageSeverity::Error, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), String::Printf( "There are no triangles in LOD %d", lodIndex ).AsChar() );
						hasErrors = true;
					}
				}
				return hasErrors;
			} );

			s_rulesMap.Insert( sNoTrianglesRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				mesh::DataViewRO view( *mesh );
				for ( const mesh::LODIndex lodIndex : ctx.lodTriangleCount.Indices() )
				{
					red::DynArray< mesh::ChunkIndex > chunkIndices = view.GetLODChunkIndices( lodIndex );
					for ( const mesh::ChunkIndex chunkIndex : chunkIndices )
					{
						const EMeshVertexType type = view.GetChunkVertexType( chunkIndex );
						if ( type != MVT_GarmentSkinnedMesh && type != MVT_ExtGarmentSkinnedMesh )
						{
							errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), "This is a character mesh (located under 'base\\characters\\player' or 'base\\characters\\garment') and it should have garment streams (vertexType = MVT_GarmentSkinnedMesh or vertexType = MVT_ExtGarmentSkinnedMesh)" );
						}
					}
				}
				return false; // no errors here - just warnings
			} );

			s_rulesMap.Insert( sGarmentStreamRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				mesh::DataViewRO view( *mesh );
				if ( view.GetNumLODs() == 1 && ctx.lodTriangleCount.Front() > 1200 )
				{
					errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), String::Printf( "There is only LOD0 with %d triangles in this mesh", ctx.lodTriangleCount.Front() ).AsChar() );
				}
				return false; // no erros!
			} );

			s_rulesMap.Insert( sMissingLodsRuleName, rule );
		}

		{
			Rule rule( []( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				if ( ctx.lodTriangleCount.Size() < 2 || ctx.lodTriangleCount[ 0 ] < 1250 )
				{
					return false;
				}

				if ( ctx.lodTriangleCount[ 0 ] == ctx.lodTriangleCount[ 1 ] )
				{
					errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), "LOD0 and LOD1 have the same amount of triangles" );
				}
				return false; // no errors!
			} );

			s_rulesMap.Insert( sNoLastLODreduction, rule );
		}

		{
			Rule rule( [] ( tools::IMessageReporter& errorHandler, const THandle< CMesh >& mesh, const Rule::Context& ctx )
			{
				const Uint32 gpuMemorySize = mesh->GetRenderResource() ? mesh->GetRenderResource()->GetUsedVideoMemory() : 0u;
				if ( gpuMemorySize > RED_KILO_BYTE( 3 * 1024u ) )	// 3.0MB
				{
					errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), String::Printf( "Destructible mesh uses %.2f MB of memory and is considered very heavy. Available %.2f MB", gpuMemorySize / ( 1024.f * 1024.0f ), 3.0f ).AsChar() );
				}
				else if ( gpuMemorySize > RED_KILO_BYTE( 1844u ) )	// 1.8MB
				{
					errorHandler.Report( tools::MessageSeverity::Warning, mesh->GetPath(), RED_LOCATION( mesh->GetPath() ), String::Printf( "Destructible mesh uses %.2f MB of memory and is considered heavy. Available %.2f MB", gpuMemorySize / ( 1024.f * 1024.0f ), 1.8f ).AsChar() );
				}

				return false; // no errors!
			} );

			s_rulesMap.Insert( sDestructionMemorySizeRuleName, rule );
		}
	}

	void MeshValidator::FillConditionsMap() const
	{
		static const res::ResourcePath sRulesFilePath = res::ResourcePath::Build( "engine\\editor\\mesh_validation_rules.json" );
		TResRef< JsonResource > configurationFile( sRulesFilePath );
		if ( !configurationFile.Get() )
		{
			RED_LOG_ERROR( "Missing engine\\editor\\meshValidationRules.json file. All meshes will be fully validated." );
			return;
		}

		THandle< RulesConditions > rulesExclusionObject = configurationFile->GetRoot< RulesConditions >();
		if ( !rulesExclusionObject )
		{
			RED_LOG_ERROR( "MeshValidationRules file - missing json root node" );
			return;
		}

		for ( const RuleCondition& condition : rulesExclusionObject->GetRules() )
		{
			s_rulesConditionsMap.Insert( condition.GetName(), condition );
		}
	}

	Bool MeshValidator::Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const
	{
		const res::ResourcePath& resourcePath = resource->GetPath();
		if ( GetConditionsMap().KeyExist( sGeneralValidationRuleName ) && GetConditionsMap()[ sGeneralValidationRuleName ].IsExcluded( resourcePath ) )
		{
			return true;
		}

		const THandle< CMesh > mesh = Cast< CMesh >( resource );
		red::StringView path = resourcePath.ToStringView();
		mesh::DataViewRO view( *mesh );

		Rule::Context ctx;

		for ( mesh::LODIndex lodIndex = 0; lodIndex < view.GetNumLODs(); ++lodIndex )
		{
			Uint32 lodTriangleCount = 0;
			red::DynArray< mesh::ChunkIndex > chunkIndices = view.GetLODChunkIndices( lodIndex );
			for ( mesh::ChunkIndex chunkIndex : chunkIndices )
			{
				lodTriangleCount += view.GetChunkTriangleCount( chunkIndex );
			}

			ctx.lodTriangleCount.PushBack( lodTriangleCount );
		}

		Bool hasErrors = false;
		for ( const auto& entry : GetRulesMap() )
		{
			const CName name = entry.Key();
			const Rule& rule = entry.Value();

			const RuleCondition& condition = GetConditionsMap()[ name ];

			if ( condition.IsIncluded( resourcePath ) && !condition.IsExcluded( resourcePath ) )
			{
				hasErrors |= rule( errorHandler, mesh, ctx );
			}
		}

		return !hasErrors;
	}
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( ConditionType, tools );
RTTI_BEGIN_ENUM_IN_NAMESPACE( ConditionType, tools );
	RTTI_ENUM_OPTION( PrefixPath );
	RTTI_ENUM_OPTION( SuffixPath );
	RTTI_ENUM_OPTION( ContainmentPath );
	RTTI_ENUM_OPTION( PrefixFilename );
	RTTI_ENUM_OPTION( SuffixFilename );
	RTTI_ENUM_OPTION( ContainmentFilename );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PathCondition, tools );
	RTTI_PROPERTY( m_type );
	RTTI_PROPERTY( m_path );
	RTTI_PROPERTY( m_negate );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RuleCondition, tools );
	RTTI_PROPERTY( m_name );
	RTTI_PROPERTY( m_includePathConditions );
	RTTI_PROPERTY( m_excludePathConditions );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RulesConditions, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_rules );
RTTI_END_TYPE();