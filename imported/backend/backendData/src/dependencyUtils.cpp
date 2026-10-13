/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyUtils.h"

#include "resourceDatabase.h"
#include "dependencyScannerImpl.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "dependencyJsonWriter.h"
#include "dependencyDatabase.h"
#include "dataTaskSystem.h"

#include "../../../common/redJobs2/include/jobRunner.h"
#endif
#include "../../../common/redContainers/include/queue.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/redReflection/include/scriptUtils.h"
#include "../../../common/gameTweakDB/include/tweakDBLoader.h"
#include "../../../common/redReflection/include/scriptVersion.h"
#include "../../../common/engine/include/scriptHelper.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#endif

namespace dependency
{

red::DynArray< res::ResourcePath > LookupRecursive( resdb::Database& database, const red::DynArray< res::ResourcePath >& resourcePaths )
{
	red::DynArray< res::ResourcePath > result = resourcePaths;
	for ( const auto& resourcePath : resourcePaths )
	{
		red::Set< res::ResourcePath > dependencies = database.GetRecursiveForwardPaths( resourcePath );
		result.PushBack( dependencies.Elements() );
	}

	return result;
}

red::DynArray< res::ResourcePath > LookupRecursiveHard( resdb::Database& database, const red::DynArray< res::ResourcePath >& resourcePaths )
{
	red::DynArray< res::ResourcePath > result;
	result.Reserve( resourcePaths.Size() * 2 ); // Guess

	red::HashSet< res::ResourcePath > visited;
	for ( const auto& path : resourcePaths )
	{
		if ( visited.Insert( path ).IsSuccessful() )
		{
			result.PushBack( path );
		}
	}

	for ( Uint32 i = 0; i < result.Size(); ++i )
	{
		const auto resource = result[ i ];
		auto forwardLinks = database.GetForwardLinks( resource );
		for ( const auto& link : forwardLinks )
		{
			if ( link.type == resdb::Link::Type::HARD )
			{
				if ( visited.Insert( link.to ).IsSuccessful() )
				{
					result.PushBack( link.to );
				}
			}
		}
	}

	return result;
}

red::DynArray< res::ResourcePath > LookupRecursiveJustRight_HackForCertainCookedMeshesOnly( resdb::Database& database, const res::ResourcePath& resource )
{
	red::DynArray< res::ResourcePath > result;
	red::HashSet< res::ResourcePath > visited;

	result.PushBack( resource );
	visited.Insert( resource );
	{
		// For this special case insert all resources from the root resource
		auto forwardLinks = database.GetForwardLinks( resource );
		for ( const auto& link : forwardLinks )
		{
			result.PushBack( link.to );
		}
	}

	for ( Uint32 i = 1; i < result.Size(); ++i )
	{
		const auto current = result[ i ];
		if ( visited.Insert( current ).IsSuccessful() )
		{
			auto forwardLinks = database.GetForwardLinks( current );
			for ( const auto& link : forwardLinks )
			{
				if ( link.type == resdb::Link::Type::HARD )
				{
					result.PushBack( link.to );
				}
			}
		}
	}

	return result;
}


red::DynArray< res::ResourcePath > GetNonInplace( resdb::Database& database, const res::ResourcePath path )
{
	return {};
}

//------------------------------------------------------------------------------

#if !defined( VANGUARD_ASSET_HOST )
// HACK: Since I couldn't submit my changelist which does this properly.
// So I just ran it locally and made a list of resources that are used in the game. 
// Then hard coded them here so I could filter out un-needed resources
red::DynArray< res::ResourcePath > GetResourcesFromGame()
{
	red::DynArray< res::ResourcePath > result{
		RED_CONST_RESOURCEPATH( "base\\animations\\anim_motion_database\\action_animation.actionanimdb" ),
		RED_CONST_RESOURCEPATH( "base\\appearances_mappings.json" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\man_base\\man_base.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\man_big\\man_big.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\man_child\\man_child.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\man_fat\\man_fat.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\woman_base\\woman_base.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\base_entities\\woman_big\\woman_big.rig" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\common\\garment_default_params.garmentlayerparams" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\common\\garment_default_params_for_player.garmentlayerparams" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\common\\player_base_bodies\\appearances\\a0_000_base__full.app" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\common\\player_base_bodies\\appearances\\n0_000_base_fpp__neck.app" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\common\\player_base_bodies\\appearances\\t0_000_base__full.app" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\head\\player_base_heads\\appearances\\head\\h0_000__basehead.app" ),
		RED_CONST_RESOURCEPATH( "base\\characters\\head\\wa\\h0_000_wa_c__basehead\\h0_000_wa_c__basehead_rigsetup.facialsetup" ),
		RED_CONST_RESOURCEPATH( "base\\entities\\camera\\photo_mode_rt_camera.ent" ),
		RED_CONST_RESOURCEPATH( "base\\fx\\devices\\_damage\\street_lamps\\d_damage_street_lamp_destruction_01.effect" ),
		RED_CONST_RESOURCEPATH( "base\\fx\\shaders\\parallax_bink.mt" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\ai\\archetypes\\archetype_set.archetypes" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\ai\\senses_tracing.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community\\community_spawnsets.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community\\crowd.mesh" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community\\crowd_presets.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community\\crowd_settings.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community\\population_settings.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\community_system.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\ai\\pathlengthtodirectdistancesratio.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\ai\\senses.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\ai\\threats.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\aim_assist\\aim_assist.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\camera_curves\\sensitivity_mappings.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\damage\\damage.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\devices\\movingplatform.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\difficulty_curves_mapping.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\input\\curves.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\lights\\generic.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\lights\\vehicles.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\locomotion\\player_adjust.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\locomotion\\player_locomotion.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\projectiles\\grenade_preview.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\recoil\\recoil.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\statcurves\\puppet\\puppet_dynamic_scaling.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\curves\\statcurves\\statscurvesset.curveresset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\dynamic_ps_classes.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\factories\\items\\object_pool_budgets.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\focus_mode.envparam" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\game_effects\\static_effects.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\game_time.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\common\\icons\\atlas_inventory_fallback.inkatlas" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\common\\input\\icons_durango.inkatlas" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\common\\input\\icons_keyboard.inkatlas" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\common\\input\\icons_orbis.inkatlas" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\common\\input\\icons_steam.inkatlas" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\cp_2077.inkgamesettings" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fonts\\cp_2077.inktypography" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fullscreen\\circular_blur.envparam" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fullscreen\\main_menu\\gog_qr_code.dtex" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fullscreen\\pause_menu\\tex\\menu_bg_texture.xbm" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\fullscreen\\world_map\\floorplan.inkwidget" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\ui_game.inklayers" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\widgets\\healthbar\\npcnameplate.inkwidget" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\widgets\\healthbar\\npcnameplate_animations.inkanim" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\widgets\\mappins\\gameplay\\gameplay_mappin_animations.inkanim" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\widgets\\minimap\\minimap_device_mappin_animations.inkanim" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\gui\\widgets\\shapes\\shapes.inkshapecollection" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\loot\\materials\\loot_drop_highlight_common_dimmed.mi" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\loot\\materials\\loot_drop_highlight_epic_dimmed.mi" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\loot\\materials\\loot_drop_highlight_legendary_dimmed.mi" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\loot\\materials\\loot_drop_highlight_rare_dimmed.mi" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\loot\\materials\\loot_drop_highlight_uncommon_dimmed.mi" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\smart_objects\\metrics.csv" ),
		RED_CONST_RESOURCEPATH( "base\\gameplay\\vehicles\\curves\\drive_helpers.curveset" ),
		RED_CONST_RESOURCEPATH( "base\\localization\\locdatamap.json" ),
		RED_CONST_RESOURCEPATH( "base\\localization\\volanguagedatamap.json" ),
		RED_CONST_RESOURCEPATH( "base\\localization\\volanguagedatamap_helmet.json" ),
		RED_CONST_RESOURCEPATH( "base\\localization\\volanguagedatamap_holocall.json" ),
		RED_CONST_RESOURCEPATH( "base\\localization\\volanguagedatamap_rewinded.json" ),
		RED_CONST_RESOURCEPATH( "base\\materials\\matlib\\matlib.matlib" ),
		RED_CONST_RESOURCEPATH( "base\\movies\\fullscreen\\logo_splashscreen\\cp_logo_loop_en.bk2" ),
		RED_CONST_RESOURCEPATH( "base\\physics\\foliage_destruction.foldest" ),
		RED_CONST_RESOURCEPATH( "base\\quest\\cyberpunk2077.gamedef" ),
		RED_CONST_RESOURCEPATH( "base\\quest\\primary_characters\\vsets\\vset_v.scene" ),
		RED_CONST_RESOURCEPATH( "base\\quest\\telemetry\\explicit_end_rewards.csv" ),
		RED_CONST_RESOURCEPATH( "base\\quest\\telemetry\\important_quest_facts.csv" ),
		RED_CONST_RESOURCEPATH( "base\\quest\\telemetry\\vr_tutorial_facts.csv" ),
		RED_CONST_RESOURCEPATH( "base\\sound\\event\\eventsmetadata.json" ),
		RED_CONST_RESOURCEPATH( "base\\sound\\event\\soundbanks.json" ),
		RED_CONST_RESOURCEPATH( "base\\sound\\soundbanks\\init.bnk" ),
		RED_CONST_RESOURCEPATH( "base\\sound\\soundbanks\\sfx_container.bnk" ),
		RED_CONST_RESOURCEPATH( "base\\sound\\soundbanks\\sfx_container.opusinfo" ),
		RED_CONST_RESOURCEPATH( "base\\vehicles\\vehicle_detached_part_base.ent" ),
		RED_CONST_RESOURCEPATH( "engine\\animation\\anim_controller_presets.csv" ),
		RED_CONST_RESOURCEPATH( "engine\\animation\\controllers_setups.csv" ),
		RED_CONST_RESOURCEPATH( "engine\\fonts\\arial.fnt" ),
		RED_CONST_RESOURCEPATH( "engine\\fonts\\blender.fnt" ),
		RED_CONST_RESOURCEPATH( "engine\\fonts\\inconsolata.fnt" ),
		RED_CONST_RESOURCEPATH( "engine\\i18n\\icudt65l.dat" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\atlas\\fallback.inkatlas" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\atlas\\white_mask_atlas.inkatlas" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\fonts\\arial.inkfontfamily" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\input\\input_key_icon_dictionary.json" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\textures\\4x4_black.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\textures\\advert_missing_format.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\textures\\advert_wrong_resource_fallback.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\textures\\component_missing_texture.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\textures\\too_many_binks_texture.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\ink\\widgets\\global_tv_bink_player.inkwidget" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\bink_simple.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\debugdraw.remt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\defaults\\defaultfaceregions.regionset" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\font.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\internal\\legacy_multilayer.mi" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\internal\\multilayered_baked.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base.remt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_default_element.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_default_tile_element.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_box_blur.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_color_correction.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_color_fill.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_glitch.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_inner_glow.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_light_sweep.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_linear_wipe.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_mask.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_point_cloud.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_radial_wipe.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_effect_swipe.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_element_depth_texture.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_panel.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\materials\\ui_text_element.mt" ),
		RED_CONST_RESOURCEPATH( "engine\\meshes\\editor\\box_occluder.w2mesh" ),
		RED_CONST_RESOURCEPATH( "engine\\meshes\\global_ocean_patch\\global_ocean_patch.mesh" ),
		RED_CONST_RESOURCEPATH( "engine\\physics\\collision_groups.json" ),
		RED_CONST_RESOURCEPATH( "engine\\preview_panel\\preview_panel_env_base.env" ),
		RED_CONST_RESOURCEPATH( "engine\\scenesystem\\hotspotdefinition.interaction" ),
		RED_CONST_RESOURCEPATH( "engine\\systems\\game_systems_startup.csv" ),
		RED_CONST_RESOURCEPATH( "engine\\systems\\runtime_systems_startup.csv" ),
		RED_CONST_RESOURCEPATH( "engine\\textures\\noise\\bluenoise_64_rg.texarray" ),
		RED_CONST_RESOURCEPATH( "engine\\textures\\normals_best_fit.xbm" ),
		RED_CONST_RESOURCEPATH( "engine\\time_system\\ease_in.curveset" ),
		RED_CONST_RESOURCEPATH( "engine\\time_system\\ease_out.curveset" ),
	};

	std::sort( result.Begin(), result.End() );
	return result;
}


red::AbsolutePath GetDefaultFinalScriptBlobPath()
{
	const char* blobName = script::GetBlobName( script::ScriptVersion_Final );
	red::AbsolutePath primaryBlobPath = ScriptsHelper::GetCompileBlobPath().AddFilePath( blobName );
	return primaryBlobPath;
}

red::DynArray< res::ResourcePath > GetResourcesFromScripts()
{
	auto result = GetResourcePathsFromScriptsBlob( GetDefaultFinalScriptBlobPath() );
	red::alg::RemoveDuplicates( result );
	return result;
}

red::DynArray< res::ResourcePath > GetResourcesFromTweakDB()
{
	red::DynArray< res::ResourcePath > result;

	game::data::TweakDBLoader loader;
	bool success = loader.LoadResourcePaths( GFileManager->GetCacheDirectory(), result );
	RED_ASSERT( success, "Failed to get paths from TweakDB" );

	red::alg::RemoveDuplicates( result );

	return result;
}
#endif



//------------------------------------------------------------------------------


namespace helper
{

static red::DynArray<res::ResourcePath> SortDependenciesInPostLoadOrder( const res::ResourcePath& path, const red::HashSet<res::ResourcePath>& dependencies, resdb::Database& database )
{
	struct ResourceEntry
	{
		res::ResourcePath resourcePath;
		Uint32 order;
	};

	red::Queue<ResourceEntry> dependencyQueue{ red::PoolBackend() };
	red::HashMap<res::ResourcePath, ResourceEntry> seenDependencies{ red::PoolBackend() };

	Uint32 order = 0;
	const ResourceEntry rootEntry{ path, order++ };
	dependencyQueue.Push(rootEntry);
	seenDependencies.Insert(path, rootEntry);

	while (!dependencyQueue.Empty())
	{
		const ResourceEntry dep = dependencyQueue.Pop();
		red::DynArray<res::ResourcePath> depLinks = database.GetForwardResourcePaths( dep.resourcePath );

		// Help ensure some data determinism
		std::sort( depLinks.Begin(), depLinks.End() );

		for ( const auto& it : depLinks )
		{
			ResourceEntry entry{ it, order + 1 };
			if ( seenDependencies.Insert( it, entry ).IsSuccessful() )
			{
				order += 1;
				dependencyQueue.Push( entry );
			}
		}
	}

	red::DynArray<res::ResourcePath> result{ red::PoolBackend() };
	dependencies.GetElements( result );

	std::sort( result.Begin(), result.End(), [&seenDependencies](const res::ResourcePath& a, const res::ResourcePath& b)
	{
		return seenDependencies[a].order < seenDependencies[b].order;
	} );

	return result;
}

// Will be combined in one cooked mlsetup for consoles
static Bool ShouldSkipResourceMerge(const res::ResourcePath& path, data::TargetPlatform platform)
{
	return false;
}

} // helper

red::DynArray<res::ResourcePath> CollectMergeableDependencies( const res::ResourcePath& path, data::TargetPlatform platform, resdb::Database& database, bool includeSoftImports )
{
	red::HashSet<res::ResourcePath> resources{ red::PoolBackend() };
	{
		red::DynArray< res::ResourcePath > dependencies{ red::PoolBackend() };
		if ( includeSoftImports )
		{
			// HACK until can resave meshes or have two-stage cooking!!!
			// When removing this hack, make sure to NOT includeSoftImports for meshes, because they'll have been converted to the proper hard imports
			if ( path.ToStringView().EndsWith( ".mesh" ) || path.ToStringView().EndsWith( ".w2mesh" ) )
			{
				dependencies = dependency::LookupRecursiveJustRight_HackForCertainCookedMeshesOnly( database, path );
			}
			else
			{
				dependencies = dependency::LookupRecursive(database, { path });
			}
		}
		else
		{
			dependencies = dependency::LookupRecursiveHard(database, { path });
		}

		RED_FATAL_ASSERT(dependencies.Exist(path), "%hs", path.ToDebugString());
		red::alg::EraseIf(dependencies, [platform](const auto& it) {
			return helper::ShouldSkipResourceMerge(it, platform);
		});

		resources.Reserve(dependencies.Size() + 1);
		resources.Insert(path);
		for (const auto& it : dependencies)
		{
			resources.Insert(it);
		}
	}

	// Brute force. We can inplace merge all recursive forward dependences as long as they all
	// form a closed set, regardless of the immediate backlink count.
	// #tbd: it.type ==  soft vs hard... matter???
	red::DynArray<res::ResourcePath> toRemove{ red::PoolBackend() };
	toRemove.Reserve(resources.Size());
	for (;;)
	{
		for (const auto& it : resources)
		{
			// Don't remove the root resource from the set
			if (it == path)
			{
				continue;
			}

			for (const auto& backwardLink : database.GetBackwardLinks(it))
			{
				if (!resources.Exist(backwardLink.from))
				{
					toRemove.PushBack(it);
					break;
				}
			}
		}

		if (toRemove.Empty())
		{
			break;
		}

		for (const auto& it : toRemove)
		{
			resources.Remove(it);
		}
		toRemove.Clear();
	}

	resources.Remove(path);

	return helper::SortDependenciesInPostLoadOrder(path, resources, database);
}

//------------------------------------------------------------------------------

#if !defined( VANGUARD_ASSET_HOST )
bool InplaceTopologicalSort( resdb::Database& database, red::DynArray< res::ResourcePath >& resources )
{
	if ( resources.Size() < 2 )
	{
		return true;
	}

	PC_SCOPE( "InplaceTopologicalSort" );

	const Uint32 initialSize = resources.Size();

	// Sort input resources to make them easier to manage
	std::sort( resources.Begin(), resources.End() );

	// Put resources to sort into forward dependencies map
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > forwardDeps;
	forwardDeps.Reserve( resources.Size() );
	for ( const auto path : resources )
	{
		red::DynArray< res::ResourcePath > deps = database.GetForwardResourcePaths( path );

		// Remove dependencies which are not in the given list of resources
		auto iter = red::alg::KeepIfIn( deps.Begin(), deps.End(), resources.Begin(), resources.End() );
		deps.Remove( iter, deps.End() );

		forwardDeps.Insert( path, red::MakeSetFromSortedArray( deps ) );
	}

	// Create initial list of leaf and reverse dependencies map
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > reverseDeps;
	red::DynArray< res::ResourcePath > leafResources;

	auto iter = forwardDeps.Begin(), end = forwardDeps.End();
	while ( iter != end )
	{
		if ( iter.Value().Empty() )
		{
			leafResources.PushBack( iter.Key() );
			auto result = forwardDeps.Remove( iter );
			RED_ASSERT( result.IsSuccessful() );
			iter = result.Iterator();
		}
		else
		{
			for ( const auto path : iter.Value() )
			{
				reverseDeps[ path ].Insert( iter.Key() );
			}
			++iter;
		}
	}

	resources.Clear();

	// Main stage of sorting
	while ( !leafResources.Empty() )
	{
		const auto path = leafResources.PopBack();

		resources.PushBack( path );

		auto& revDeps = reverseDeps[ path ];
		for ( auto revDep : revDeps )
		{
			auto& fwdDeps = forwardDeps[ revDep ];
			fwdDeps.Remove( path );
			if ( fwdDeps.Empty() )
			{
				leafResources.PushBack( revDep );
				forwardDeps.Remove( revDep );
			}
		}
		reverseDeps.Remove( path );
	}

	RED_ASSERT( forwardDeps.Empty(), "Cycles depetected in the dependencies!" );

	std::reverse( resources.Begin(), resources.End() );

	return initialSize == resources.Size();
}

bool InplaceTopologicalSort( resdb::Database& database, red::DynArray< THandle< CResource > >& resources )
{
	red::HashMap< res::ResourcePath, THandle< CResource > > resourceMap{ resources.Size(), red::PoolDefault() };
	for ( const auto& resource : resources )
	{
		resourceMap.Insert( resource->GetPath(), resource );
	}

	auto paths = resourceMap.GetKeys();
	if ( !InplaceTopologicalSort( database, paths ) )
	{
		return false;
	}

	resources.Clear();
	for ( const auto path : paths )
	{
		resources.PushBack( resourceMap[ path ] );
	}

	return true;
}

//------------------------------------------------------------------------------

red::DynArray< res::ResourcePath > GetInplaceResourcePaths( resdb::Database& database, InplaceType type )
{
	const Uint32 resourcesCount = database.GetResourcePaths().Size();
	const auto backwardLinks = database.GetAllBackwardLinks();

	red::DynArray< res::ResourcePath > inplace;
	red::DynArray< res::ResourcePath > normal;
	inplace.Reserve( resourcesCount );
	normal.Reserve( resourcesCount );

	res::ResourcePath lastInplacePath;
	res::ResourcePath lastNormalPath;
	for ( const auto& link : backwardLinks )
	{
		const auto path = link.to;
		if ( link.inplace )
		{
			if ( lastInplacePath != path )
			{
				inplace.PushBack( path );
				lastInplacePath = path;
			}
		}
		else
		{
			if ( lastNormalPath != path )
			{
				normal.PushBack( path );
				lastNormalPath = path;
			}
		}
	}

	RED_ASSERT( std::is_sorted( inplace.Begin(), inplace.End() ) );
	RED_ASSERT( std::is_sorted( normal.Begin(), normal.End() ) );

	red::DynArray< res::ResourcePath > result;

	if ( type == InplaceType::AnyInplace )
	{
		result.PushBack( inplace );
	}
	else if ( type == InplaceType::OnlyInplace )
	{
		auto iter = red::alg::RemoveIfIn( inplace.Begin(), inplace.End(), normal.Begin(), normal.End() );
		result.PushBack( red::MakeArraySpan( inplace.Begin(), iter ) );
	}
	else if ( type == InplaceType::NeverInplace )
	{
		auto iter = red::alg::RemoveIfIn( normal.Begin(), normal.End(), inplace.Begin(), inplace.End() );
		result.PushBack( red::MakeArraySpan( normal.Begin(), iter ) );
	}

	return result;
}

//------------------------------------------------------------------------------

namespace prv
{

red::String ComputeRelativeHashPath( const res::ResourcePath& resourcePath, const char* extension )
{
	const Uint64 resourceHash = resourcePath.GetHash();
	return String::Printf( "%.2x\\%.2x\\%.16llx.%hs", static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), resourceHash, extension );
}

red::AbsolutePath ComputeDepsOutputPath( const red::AbsolutePath& basePath, const res::ResourcePath& resourcePath )
{
	const String filePath = ComputeRelativeHashPath( resourcePath, "deps" );
	return basePath.AddFilePath( filePath );
}

//------------------------------------------------------------------------------

bool DebugDump_WriteFile( const red::AbsolutePath& path, const ResourceInfo& info )
{
	WriterScope writer = CreateJsonWriter( path );
	writer->WriteNumber( "version", ResourceInfo::CURRENT_VERSION );
	return info.DumpDebugInfo( *writer );
}

bool DumpDebugInfo( const data::TaskSystem& taskSystem, const red::AbsolutePath& basePath )
{
	red::Atomic< bool > allDumped = true;

	job::Counter noWait;
	job::Counter counter;

	const auto allTasks = taskSystem.Internal_GetAllTasksInOrder();

	job::DispatchParallelForJob< red::PoolBackend >( "DependencyDatabase/DumpDebugInfo", noWait, counter, { allTasks.Size() }, 
		[&allTasks, &allDumped, &basePath]( const Uint32 index, const job::RunContext& )
	{
		const auto& info = allTasks[ index ]->Internal_GetDependencyResourceInfo();
		red::AbsolutePath dumpPath = ComputeDepsOutputPath( basePath, info.GetPath() );
		if ( ! DebugDump_WriteFile( dumpPath, info ) )
		{
			allDumped.SetValue( false );
		}
	} );

	job::FlushCounter( counter );

	return allDumped.GetValue();
}

bool DumpDebugInfo( const Database& database, const red::AbsolutePath& basePath )
{
	red::Atomic< bool > allDumped = true;

	job::Counter noWait;
	job::Counter counter;

	const auto allResourceInfos = database.GetAllResourceInfos();

	job::DispatchParallelForJob< red::PoolBackend >( "DependencyDatabase/DumpDebugInfo", noWait, counter, { allResourceInfos.Size() }, 
		[&allResourceInfos, &allDumped, &basePath]( const Uint32 index, const job::RunContext& )
	{
		const auto* info = allResourceInfos[ index ];
		red::AbsolutePath dumpPath = ComputeDepsOutputPath( basePath, info->GetPath() );
		if ( ! DebugDump_WriteFile( dumpPath, *info ) )
		{
			allDumped.SetValue( false );
		}
	} );

	job::FlushCounter( counter );

	return allDumped.GetValue();
}

//------------------------------------------------------------------------------

bool Json_WriteFile( const red::AbsolutePath& path, const ResourceInfo& info )
{
	WriterScope writer = CreateJsonWriter( path );
	writer->WriteNumber( "version", ResourceInfo::CURRENT_VERSION );
	return info.Save( *writer );
}

bool ExportDependenciesToJson( const Database& database, const red::AbsolutePath& basePath, const red::DynArray< res::ResourcePath >& paths )
{
	red::Atomic< bool > allExported = true;

	job::Counter noWait;
	job::Counter counter;

	job::DispatchParallelForJob< red::PoolBackend >( "DependencyDatabase/ExportJson", noWait, counter, { paths.Size() }, 
		[&database, &paths, &allExported, &basePath]( const Uint32 index, const job::RunContext& )
	{
		const auto* info = database.GetResourceInfo( paths[ index ] );
		if ( ! info )
		{
			allExported.SetValue( false );
			return;
		}

		red::AbsolutePath savePath = ComputeDepsOutputPath( basePath, info->GetPath() );
		if ( ! Json_WriteFile( savePath, *info ) )
		{
			allExported.SetValue( false );
		}
	} );

	job::FlushCounter( counter );

	return allExported.GetValue();
}

} // prv
#endif

} // dependency
