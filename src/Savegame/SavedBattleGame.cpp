/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <vector>
#include "BattleItem.h"
#include "ItemContainer.h"
#include "SavedBattleGame.h"
#include "SavedGame.h"
#include "Tile.h"
#include "Node.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/MCDPatch.h"
#include "../Battlescape/Pathfinding.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Battlescape/Inventory.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Engine/Game.h"
#include "../Mod/RuleInventory.h"
#include "../Battlescape/AIModule.h"
#include "../Engine/RNG.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include "../Engine/ScriptBind.h"
#include "SerializationHelper.h"

namespace OpenXcom
{

/**
 * Initializes a brand new battlescape saved game.
 */
SavedBattleGame::SavedBattleGame(Mod *rule) :
	_battleState(0), _rule(rule), _mapsize_x(0), _mapsize_y(0), _mapsize_z(0), _selectedUnit(0),
	_lastSelectedUnit(0), _pathfinding(0), _tileEngine(0), _globalShade(0), _side(FACTION_PLAYER), _turn(1), _bughuntMinTurn(20), _animFrame(0),
	_debugMode(false), _bughuntMode(false), _aborted(false), _itemId(0), _objectiveType(-1), _objectivesDestroyed(0), _objectivesNeeded(0), _unitsFalling(false),
	_cheating(false), _tuReserved(BA_NONE), _kneelReserved(false), _depth(0), _ambience(-1), _ambientVolume(0.5),
	_turnLimit(0), _cheatTurn(20), _chronoTrigger(FORCE_LOSE), _beforeGame(true)
{
	_tileSearch.resize(11*11);
	for (int i = 0; i < 121; ++i)
	{
		_tileSearch[i].x = ((i%11) - 5);
		_tileSearch[i].y = ((i/11) - 5);
	}
	_baseItems = new ItemContainer();

	setRandomHiddenMovementBackground(0);
}

/**
 * Deletes the game content from memory.
 */
SavedBattleGame::~SavedBattleGame()
{
	for (std::vector<MapDataSet*>::iterator i = _mapDataSets.begin(); i != _mapDataSets.end(); ++i)
	{
		(*i)->unloadData();
	}

	for (std::vector<Node*>::iterator i = _nodes.begin(); i != _nodes.end(); ++i)
	{
		delete *i;
	}

	for (std::vector<BattleUnit*>::iterator i = _units.begin(); i != _units.end(); ++i)
	{
		delete *i;
	}

	for (std::vector<BattleItem*>::iterator i = _items.begin(); i != _items.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<BattleItem*>::iterator i = _recoverGuaranteed.begin(); i != _recoverGuaranteed.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<BattleItem*>::iterator i = _recoverConditional.begin(); i != _recoverConditional.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<BattleItem*>::iterator i = _deleted.begin(); i != _deleted.end(); ++i)
	{
		delete *i;
	}

	delete _pathfinding;
	delete _tileEngine;
	delete _baseItems;
}

/**
 * Loads the saved battle game from a YAML file.
 * @param node YAML node.
 * @param mod for the saved game.
 * @param savedGame Pointer to saved game.
 */
void SavedBattleGame::load(const YAML::Node &node, Mod *mod, SavedGame* savedGame)
{
	int mapsize_x = node["width"].as<int>(_mapsize_x);
	int mapsize_y = node["length"].as<int>(_mapsize_y);
	int mapsize_z = node["height"].as<int>(_mapsize_z);
	initMap(mapsize_x, mapsize_y, mapsize_z);

	_missionType = node["missionType"].as<std::string>(_missionType);
	_startingConditionType = node["startingConditionType"].as<std::string>(_startingConditionType);
	_alienCustomDeploy = node["alienCustomDeploy"].as<std::string>(_alienCustomDeploy);
	_alienCustomMission = node["alienCustomMission"].as<std::string>(_alienCustomMission);
	_globalShade = node["globalshade"].as<int>(_globalShade);
	_turn = node["turn"].as<int>(_turn);
	_bughuntMinTurn = node["bughuntMinTurn"].as<int>(_bughuntMinTurn);
	_bughuntMode = node["bughuntMode"].as<bool>(_bughuntMode);
	_depth = node["depth"].as<int>(_depth);
	_animFrame = node["animFrame"].as<int>(_animFrame);
	int selectedUnit = node["selectedUnit"].as<int>();

	for (YAML::const_iterator i = node["mapdatasets"].begin(); i != node["mapdatasets"].end(); ++i)
	{
		std::string name = i->as<std::string>();
		MapDataSet *mds = mod->getMapDataSet(name);
		_mapDataSets.push_back(mds);
	}

	if (!node["tileTotalBytesPer"])
	{
		// binary tile data not found, load old-style text tiles :(
		for (YAML::const_iterator i = node["tiles"].begin(); i != node["tiles"].end(); ++i)
		{
			Position pos = (*i)["position"].as<Position>();
			getTile(pos)->load((*i));
		}
	}
	else
	{
		// load key to how the tile data was saved
		Tile::SerializationKey serKey;
		size_t totalTiles = node["totalTiles"].as<size_t>();

		memset(&serKey, 0, sizeof(Tile::SerializationKey));
		serKey.index = node["tileIndexSize"].as<Uint8>(serKey.index);
		serKey.totalBytes = node["tileTotalBytesPer"].as<Uint32>(serKey.totalBytes);
		serKey._fire = node["tileFireSize"].as<Uint8>(serKey._fire);
		serKey._smoke = node["tileSmokeSize"].as<Uint8>(serKey._smoke);
		serKey._mapDataID = node["tileIDSize"].as<Uint8>(serKey._mapDataID);
		serKey._mapDataSetID = node["tileSetIDSize"].as<Uint8>(serKey._mapDataSetID);
		serKey.boolFields = node["tileBoolFieldsSize"].as<Uint8>(1); // boolean flags used to be stored in an unmentioned byte (Uint8) :|

		// load binary tile data!
		YAML::Binary binTiles = node["binTiles"].as<YAML::Binary>();

		Uint8 *r = (Uint8*)binTiles.data();
		Uint8 *dataEnd = r + totalTiles * serKey.totalBytes;

		while (r < dataEnd)
		{
			int index = unserializeInt(&r, serKey.index);
			assert (index >= 0 && index < _mapsize_x * _mapsize_z * _mapsize_y);
			_tiles[index].loadBinary(r, serKey); // loadBinary's privileges to advance *r have been revoked
			r += serKey.totalBytes-serKey.index; // r is now incremented strictly by totalBytes in case there are obsolete fields present in the data
		}
	}
	if (_missionType == "STR_BASE_DEFENSE")
	{
		if (node["moduleMap"])
		{
			_baseModules = node["moduleMap"].as<std::vector< std::vector<std::pair<int, int> > > >();
		}
		else
		{
			// backwards compatibility: imperfect solution, modules that were completely destroyed
			// prior to saving and updating builds will be counted as indestructible.
			calculateModuleMap();
		}
	}
	for (YAML::const_iterator i = node["nodes"].begin(); i != node["nodes"].end(); ++i)
	{
		Node *n = new Node();
		n->load(*i);
		_nodes.push_back(n);
	}

	for (YAML::const_iterator i = node["units"].begin(); i != node["units"].end(); ++i)
	{
		UnitFaction faction = (UnitFaction)(*i)["faction"].as<int>();
		UnitFaction originalFaction = (UnitFaction)(*i)["originalFaction"].as<int>(faction);
		int id = (*i)["id"].as<int>();
		BattleUnit *unit;
		if (id < BattleUnit::MAX_SOLDIER_ID) // Unit is linked to a geoscape soldier
		{
			// look up the matching soldier
			unit = new BattleUnit(savedGame->getSoldier(id), _depth, mod->getMaxViewDistance());
		}
		else
		{
			std::string type = (*i)["genUnitType"].as<std::string>();
			std::string armor = (*i)["genUnitArmor"].as<std::string>();
			// create a new Unit.
			if(!mod->getUnit(type) || !mod->getArmor(armor)) continue;
			unit = new BattleUnit(mod->getUnit(type), originalFaction, id, mod->getArmor(armor), mod->getStatAdjustment(savedGame->getDifficulty()), _depth, mod->getMaxViewDistance());
		}
		unit->load(*i, this->getMod()->getScriptGlobal());
		unit->setSpecialWeapon(this);
		_units.push_back(unit);
		if (faction == FACTION_PLAYER)
		{
			if ((unit->getId() == selectedUnit) || (_selectedUnit == 0 && !unit->isOut()))
				_selectedUnit = unit;
		}
		if (unit->getStatus() != STATUS_DEAD && unit->getStatus() != STATUS_IGNORE_ME)
		{
			if (const YAML::Node &ai = (*i)["AI"])
			{
				AIModule *aiModule;
				if (faction != FACTION_PLAYER)
				{
					aiModule = new AIModule(this, unit, 0);
				}
				else
				{
					continue;
				}
				aiModule->load(ai);
				unit->setAIModule(aiModule);
			}
		}
	}
	// matches up tiles and units
	resetUnitTiles();

	std::string fromContainer[3] = { "items", "recoverConditional", "recoverGuaranteed" };
	std::vector<BattleItem*> *toContainer[3] = {&_items, &_recoverConditional, &_recoverGuaranteed};
	for (int pass = 0; pass != 3; ++pass)
	{
		for (YAML::const_iterator i = node[fromContainer[pass]].begin(); i != node[fromContainer[pass]].end(); ++i)
		{
			std::string type = (*i)["type"].as<std::string>();
			_itemId = (*i)["id"].as<int>(_itemId);
			if (mod->getItem(type))
			{
				BattleItem *item = new BattleItem(mod->getItem(type), &_itemId);
				item->load(*i, this->getMod()->getScriptGlobal());
				type = (*i)["inventoryslot"].as<std::string>("NULL");
				if (type != "NULL")
				{
					if (mod->getInventory(type))
					{
						item->setSlot(mod->getInventory(type));

					}
					else
					{
						item->setSlot(mod->getInventory("STR_GROUND"));
					}
				}
				int owner = (*i)["owner"].as<int>();
				int prevOwner = (*i)["previousOwner"].as<int>(-1);
				int unit = (*i)["unit"].as<int>();

				// match up items and units
				for (std::vector<BattleUnit*>::iterator bu = _units.begin(); bu != _units.end(); ++bu)
				{
					if ((*bu)->getId() == owner)
					{
						item->moveToOwner(*bu);
					}
					if ((*bu)->getId() == unit)
					{
						item->setUnit(*bu);
					}
				}
				for (std::vector<BattleUnit*>::iterator bu = _units.begin(); bu != _units.end(); ++bu)
				{
					if ((*bu)->getId() == prevOwner)
					{
						item->setPreviousOwner(*bu);
					}
				}

				// match up items and tiles
				if (item->getSlot() && item->getSlot()->getType() == INV_GROUND)
				{
					Position pos = (*i)["position"].as<Position>();
					if (pos.x != -1)
						getTile(pos)->addItem(item, mod->getInventory("STR_GROUND", true));
				}
				toContainer[pass]->push_back(item);
			}
			else
			{
				Log(LOG_ERROR) << "Failed to load item " << type;
			}
		}
	}

	// tie ammo items to their weapons, running through the items again
	std::vector<BattleItem*>::iterator weaponi = _items.begin();
	for (YAML::const_iterator i = node["items"].begin(); i != node["items"].end(); ++i)
	{
		if (mod->getItem((*i)["type"].as<std::string>()))
		{
			auto setItem = [&](int slot, const YAML::Node& n)
			{
				if (n)
				{
					int ammoId = n.as<int>();
					if (ammoId != -1)
					{
						if (ammoId == (*weaponi)->getId())
						{
							(*weaponi)->setAmmoForSlot(slot, (*weaponi));
						}
						else
						{
							for (auto item : _items)
							{
								if (item->getId() == ammoId)
								{
									(*weaponi)->setAmmoForSlot(slot, item);
									break;
								}
							}
						}
					}
				}
			};

			if (const YAML::Node& ammoSlots = (*i)["ammoItemSlots"])
			{
				for (int slot = 0; slot < RuleItem::AmmoSlotMax; ++slot)
				{
					setItem(slot, ammoSlots[slot]);
				}
			}
			else
			{
				setItem(0, (*i)["ammoItem"]);
			}
			++weaponi;
		}
	}
	_objectiveType = node["objectiveType"].as<int>(_objectiveType);
	_objectivesDestroyed = node["objectivesDestroyed"].as<int>(_objectivesDestroyed);
	_objectivesNeeded = node["objectivesNeeded"].as<int>(_objectivesNeeded);
	_tuReserved = (BattleActionType)node["tuReserved"].as<int>(_tuReserved);
	_kneelReserved = node["kneelReserved"].as<bool>(_kneelReserved);
	_ambience = node["ambience"].as<int>(_ambience);
	_ambientVolume = node["ambientVolume"].as<double>(_ambientVolume);
	_music = node["music"].as<std::string>(_music);
	_baseItems->load(node["baseItems"]);
	_turnLimit = node["turnLimit"].as<int>(_turnLimit);
	_chronoTrigger = ChronoTrigger(node["chronoTrigger"].as<int>(_chronoTrigger));
	_cheatTurn = node["cheatTurn"].as<int>(_cheatTurn);
	_scriptValues.load(node, _rule->getScriptGlobal());
}

/**
 * Loads the resources required by the map in the battle save.
 * @param mod Pointer to the mod.
 */
void SavedBattleGame::loadMapResources(Mod *mod)
{
	for (std::vector<MapDataSet*>::const_iterator i = _mapDataSets.begin(); i != _mapDataSets.end(); ++i)
	{
		(*i)->loadData();
		if (mod->getMCDPatch((*i)->getName()))
		{
			mod->getMCDPatch((*i)->getName())->modifyData(*i);
		}
	}

	int mdsID, mdID;

	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		for (int part = 0; part < 4; ++part)
		{
			_tiles[i].getMapData(&mdID, &mdsID, part);
			if (mdID != -1 && mdsID != -1)
			{
				_tiles[i].setMapData(_mapDataSets[mdsID]->getObjects()->at(mdID), mdID, mdsID, part);
			}
		}
	}

	initUtilities(mod);
	getTileEngine()->calculateLighting(LL_AMBIENT, TileEngine::invalid, 0, true);
	getTileEngine()->recalculateFOV();
}

/**
 * Saves the saved battle game to a YAML file.
 * @return YAML node.
 */
YAML::Node SavedBattleGame::save() const
{
	YAML::Node node;
	if (_objectivesNeeded)
	{
		node["objectivesDestroyed"] = _objectivesDestroyed;
		node["objectivesNeeded"] = _objectivesNeeded;
		node["objectiveType"] = _objectiveType;
	}
	node["width"] = _mapsize_x;
	node["length"] = _mapsize_y;
	node["height"] = _mapsize_z;
	node["missionType"] = _missionType;
	node["startingConditionType"] = _startingConditionType;
	node["alienCustomDeploy"] = _alienCustomDeploy;
	node["alienCustomMission"] = _alienCustomMission;
	node["globalshade"] = _globalShade;
	node["turn"] = _turn;
	node["bughuntMinTurn"] = _bughuntMinTurn;
	node["animFrame"] = _animFrame;
	node["bughuntMode"] = _bughuntMode;
	node["selectedUnit"] = (_selectedUnit?_selectedUnit->getId():-1);
	for (std::vector<MapDataSet*>::const_iterator i = _mapDataSets.begin(); i != _mapDataSets.end(); ++i)
	{
		node["mapdatasets"].push_back((*i)->getName());
	}
#if 0
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		if (!_tiles[i].isVoid())
		{
			node["tiles"].push_back(_tiles[i].save());
		}
	}
#else
	// first, write out the field sizes we're going to use to write the tile data
	node["tileIndexSize"] = Tile::serializationKey.index;
	node["tileTotalBytesPer"] = Tile::serializationKey.totalBytes;
	node["tileFireSize"] = Tile::serializationKey._fire;
	node["tileSmokeSize"] = Tile::serializationKey._smoke;
	node["tileIDSize"] = Tile::serializationKey._mapDataID;
	node["tileSetIDSize"] = Tile::serializationKey._mapDataSetID;
	node["tileBoolFieldsSize"] = Tile::serializationKey.boolFields;

	size_t tileDataSize = Tile::serializationKey.totalBytes * _mapsize_z * _mapsize_y * _mapsize_x;
	Uint8* tileData = (Uint8*) calloc(tileDataSize, 1);
	Uint8* w = tileData;

	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		if (!_tiles[i].isVoid())
		{
			serializeInt(&w, Tile::serializationKey.index, i);
			_tiles[i].saveBinary(&w);
		}
		else
		{
			tileDataSize -= Tile::serializationKey.totalBytes;
		}
	}
	node["totalTiles"] = tileDataSize / Tile::serializationKey.totalBytes; // not strictly necessary, just convenient
	node["binTiles"] = YAML::Binary(tileData, tileDataSize);
	free(tileData);
#endif
	for (std::vector<Node*>::const_iterator i = _nodes.begin(); i != _nodes.end(); ++i)
	{
		node["nodes"].push_back((*i)->save());
	}
	if (_missionType == "STR_BASE_DEFENSE")
	{
		node["moduleMap"] = _baseModules;
	}
	for (std::vector<BattleUnit*>::const_iterator i = _units.begin(); i != _units.end(); ++i)
	{
		node["units"].push_back((*i)->save(this->getMod()->getScriptGlobal()));
	}
	for (std::vector<BattleItem*>::const_iterator i = _items.begin(); i != _items.end(); ++i)
	{
		node["items"].push_back((*i)->save(this->getMod()->getScriptGlobal()));
	}
	node["tuReserved"] = (int)_tuReserved;
	node["kneelReserved"] = _kneelReserved;
	node["depth"] = _depth;
	node["ambience"] = _ambience;
	node["ambientVolume"] = _ambientVolume;
	for (std::vector<BattleItem*>::const_iterator i = _recoverGuaranteed.begin(); i != _recoverGuaranteed.end(); ++i)
	{
		node["recoverGuaranteed"].push_back((*i)->save(this->getMod()->getScriptGlobal()));
	}
	for (std::vector<BattleItem*>::const_iterator i = _recoverConditional.begin(); i != _recoverConditional.end(); ++i)
	{
		node["recoverConditional"].push_back((*i)->save(this->getMod()->getScriptGlobal()));
	}
	node["music"] = _music;
	node["baseItems"] = _baseItems->save();
	node["turnLimit"] = _turnLimit;
	node["chronoTrigger"] = int(_chronoTrigger);
	node["cheatTurn"] = _cheatTurn;
	_scriptValues.save(node, _rule->getScriptGlobal());

	return node;
}

/**
 * Initializes the array of tiles and creates a pathfinding object.
 * @param mapsize_x
 * @param mapsize_y
 * @param mapsize_z
 */
void SavedBattleGame::initMap(int mapsize_x, int mapsize_y, int mapsize_z, bool resetTerrain)
{
	// Clear old map data
		for (std::vector<Node*>::iterator i = _nodes.begin(); i != _nodes.end(); ++i)
		{
			delete *i;
		}

		_nodes.clear();

	if (resetTerrain)
	{
		_mapDataSets.clear();
	}

	// Create tile objects
	_mapsize_x = mapsize_x;
	_mapsize_y = mapsize_y;
	_mapsize_z = mapsize_z;

	_tiles.clear();
	_tiles.reserve(_mapsize_z * _mapsize_y * _mapsize_x);
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		Position pos;
		getTileCoords(i, &pos.x, &pos.y, &pos.z);
		_tiles.push_back(Tile(pos));
	}

}

/**
 * Initializes the map utilities.
 * @param mod Pointer to mod.
 */
void SavedBattleGame::initUtilities(Mod *mod)
{
	delete _pathfinding;
	delete _tileEngine;
	_pathfinding = new Pathfinding(this);
	_tileEngine = new TileEngine(this, mod);
}

/**
 * Sets the mission type.
 * @param missionType The mission type.
 */
void SavedBattleGame::setMissionType(const std::string &missionType)
{
	_missionType = missionType;
}

/**
 * Gets the mission type.
 * @return The mission type.
 */
const std::string &SavedBattleGame::getMissionType() const
{
	return _missionType;
}

/**
* Returns the list of items in the base storage rooms BEFORE the mission.
* Does NOT return items assigned to craft or in transfer.
* @return Pointer to the item list.
*/
ItemContainer *SavedBattleGame::getBaseStorageItems()
{
	return _baseItems;
}

/**
 * Sets the starting condition type.
 * @param startingConditionType The starting condition type.
 */
void SavedBattleGame::setStartingConditionType(const std::string &startingConditionType)
{
	_startingConditionType = startingConditionType;
}

/**
 * Gets the starting condition type.
 * @return The starting condition type.
 */
const std::string &SavedBattleGame::getStartingConditionType() const
{
	return _startingConditionType;
}

/**
 *  Sets the custom alien data.
 */
void SavedBattleGame::setAlienCustom(const std::string &deploy, const std::string &mission)
{
	_alienCustomDeploy = deploy;
	_alienCustomMission = mission;
}

/**
 *  Gets the custom alien deploy.
 */
const std::string &SavedBattleGame::getAlienCustomDeploy() const
{
	return _alienCustomDeploy;
}

/**
 *  Gets the custom mission definition
 */
const std::string &SavedBattleGame::getAlienCustomMission() const
{
	return _alienCustomMission;
}

/**
 * Sets the global shade.
 * @param shade The global shade.
 */
void SavedBattleGame::setGlobalShade(int shade)
{
	_globalShade = shade;
}

/**
 * Gets the global shade.
 * @return The global shade.
 */
int SavedBattleGame::getGlobalShade() const
{
	return _globalShade;
}

/**
 * Gets the map width.
 * @return The map width (Size X) in tiles.
 */
int SavedBattleGame::getMapSizeX() const
{
	return _mapsize_x;
}

/**
 * Gets the map length.
 * @return The map length (Size Y) in tiles.
 */
int SavedBattleGame::getMapSizeY() const
{
	return _mapsize_y;
}

/**
 * Gets the map height.
 * @return The map height (Size Z) in layers.
 */
int SavedBattleGame::getMapSizeZ() const
{
	return _mapsize_z;
}

/**
 * Gets the map size in tiles.
 * @return The map size.
 */
int SavedBattleGame::getMapSizeXYZ() const
{
	return _mapsize_x * _mapsize_y * _mapsize_z;
}

/**
 * Converts a tile index to coordinates.
 * @param index The (unique) tileindex.
 * @param x Pointer to the X coordinate.
 * @param y Pointer to the Y coordinate.
 * @param z Pointer to the Z coordinate.
 */
void SavedBattleGame::getTileCoords(int index, int *x, int *y, int *z) const
{
	*z = index / (_mapsize_y * _mapsize_x);
	*y = (index % (_mapsize_y * _mapsize_x)) / _mapsize_x;
	*x = (index % (_mapsize_y * _mapsize_x)) % _mapsize_x;
}

/**
 * Gets the currently selected unit
 * @return Pointer to BattleUnit.
 */
BattleUnit *SavedBattleGame::getSelectedUnit() const
{
	return _selectedUnit;
}

/**
 * Sets the currently selected unit.
 * @param unit Pointer to BattleUnit.
 */
void SavedBattleGame::setSelectedUnit(BattleUnit *unit)
{
	_selectedUnit = unit;
}

/**
 * Selects the previous player unit.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 * @sa selectPlayerUnit
 */
BattleUnit *SavedBattleGame::selectPreviousPlayerUnit(bool checkReselect, bool setReselect, bool checkInventory)
{
	return selectPlayerUnit(-1, checkReselect, setReselect, checkInventory);
}

/**
 * Selects the next player unit.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 * @sa selectPlayerUnit
 */
BattleUnit *SavedBattleGame::selectNextPlayerUnit(bool checkReselect, bool setReselect, bool checkInventory)
{
	return selectPlayerUnit(+1, checkReselect, setReselect, checkInventory);
}

/**
 * Selects the next player unit in a certain direction.
 * @param dir Direction to select, eg. -1 for previous and 1 for next.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 */
BattleUnit *SavedBattleGame::selectPlayerUnit(int dir, bool checkReselect, bool setReselect, bool checkInventory)
{
	if (_selectedUnit != 0 && setReselect)
	{
		_selectedUnit->dontReselect();
	}
	if (_units.empty())
	{
		return 0;
	}

	std::vector<BattleUnit*>::iterator begin, end;
	if (dir > 0)
	{
		begin = _units.begin();
		end = _units.end()-1;
	}
	else if (dir < 0)
	{
		begin = _units.end()-1;
		end = _units.begin();
	}

	std::vector<BattleUnit*>::iterator i = std::find(_units.begin(), _units.end(), _selectedUnit);
	do
	{
		// no unit selected
		if (i == _units.end())
		{
			i = begin;
			continue;
		}
		if (i != end)
		{
			i += dir;
		}
		// reached the end, wrap-around
		else
		{
			i = begin;
		}
		// back to where we started... no more units found
		if (*i == _selectedUnit)
		{
			if (checkReselect && !_selectedUnit->reselectAllowed())
				_selectedUnit = 0;
			return _selectedUnit;
		}
		else if (_selectedUnit == 0 && i == begin)
		{
			return _selectedUnit;
		}
	}
	while (!(*i)->isSelectable(_side, checkReselect, checkInventory));

	_selectedUnit = (*i);
	return _selectedUnit;
}

/**
 * Selects the unit at the given position on the map.
 * @param pos Position.
 * @return Pointer to a BattleUnit, or 0 when none is found.
 */
BattleUnit *SavedBattleGame::selectUnit(Position pos)
{
	BattleUnit *bu = getTile(pos)->getUnit();

	if (bu && bu->isOut())
	{
		return 0;
	}
	else
	{
		return bu;
	}
}

/**
 * Gets the list of nodes.
 * @return Pointer to the list of nodes.
 */
std::vector<Node*> *SavedBattleGame::getNodes()
{
	return &_nodes;
}

/**
 * Gets the list of units.
 * @return Pointer to the list of units.
 */
std::vector<BattleUnit*> *SavedBattleGame::getUnits()
{
	return &_units;
}

/**
 * Gets the list of items.
 * @return Pointer to the list of items.
 */
std::vector<BattleItem*> *SavedBattleGame::getItems()
{
	return &_items;
}

/**
 * Gets the pathfinding object.
 * @return Pointer to the pathfinding object.
 */
Pathfinding *SavedBattleGame::getPathfinding() const
{
	return _pathfinding;
}

/**
 * Gets the terrain modifier object.
 * @return Pointer to the terrain modifier object.
 */
TileEngine *SavedBattleGame::getTileEngine() const
{
	return _tileEngine;
}

/**
 * Gets the array of mapblocks.
 * @return Pointer to the array of mapblocks.
 */
std::vector<MapDataSet*> *SavedBattleGame::getMapDataSets()
{
	return &_mapDataSets;
}

/**
 * Gets the side currently playing.
 * @return The unit faction currently playing.
 */
UnitFaction SavedBattleGame::getSide() const
{
	return _side;
}

/**
 * Test if weapon is usable by unit.
 * @param weapon
 * @param unit
 * @return Unit can shoot/use it.
 */
bool SavedBattleGame::canUseWeapon(const BattleItem* weapon, const BattleUnit* unit, bool isBerserking) const
{
	if (!weapon || !unit) return false;

	const RuleItem *rule = weapon->getRules();

	if (unit->getOriginalFaction() == FACTION_HOSTILE && getTurn() < rule->getAIUseDelay(getMod()))
	{
		return false;
	}
	if (unit->getOriginalFaction() == FACTION_PLAYER && !_battleState->getGame()->getSavedGame()->isResearched(rule->getRequirements()))
	{
		return false;
	}
	if (rule->isPsiRequired() && unit->getBaseStats()->psiSkill <= 0)
	{
		return false;
	}
	if (getDepth() == 0 && rule->isWaterOnly())
	{
		return false;
	}
	if (rule->isBlockingBothHands() && unit->getFaction() == FACTION_PLAYER && !isBerserking && unit->getLeftHandWeapon() != 0 && unit->getRightHandWeapon() != 0)
	{
		return false;
	}
	return true;
}

/**
 * Gets the current turn number.
 * @return The current turn.
 */
int SavedBattleGame::getTurn() const
{
	return _turn;
}

/**
* Sets the bug hunt turn number.
*/
void SavedBattleGame::setBughuntMinTurn(int bughuntMinTurn)
{
	_bughuntMinTurn = bughuntMinTurn;
}

/**
* Gets the bug hunt turn number.
* @return The bug hunt turn.
*/
int SavedBattleGame::getBughuntMinTurn() const
{
	return _bughuntMinTurn;
}

/**
 * Ends the current turn and progresses to the next one.
 */
void SavedBattleGame::endTurn()
{
	// reset turret direction for all hostile and neutral units (as it may have been changed during reaction fire)
	for (std::vector<BattleUnit*>::iterator i = _units.begin(); i != _units.end(); ++i)
	{
		if ((*i)->getOriginalFaction() != FACTION_PLAYER)
		{
			(*i)->setDirection((*i)->getDirection()); // this is not pointless, the method sets more than just the direction
		}
	}

	if (_side == FACTION_PLAYER)
	{
		if (_selectedUnit && _selectedUnit->getOriginalFaction() == FACTION_PLAYER)
			_lastSelectedUnit = _selectedUnit;
		_selectedUnit =  0;
		if (Options::battleExtenedCivilians) 
		{ 
			_side = FACTION_NEUTRAL; //Give next turn to civilians instead of aliens
		}
		else 
		{
			_side = FACTION_HOSTILE;
		}
	}
	else if (_side == FACTION_HOSTILE)
	{
		if (Options::battleExtenedCivilians) 
		{
			_side = FACTION_PLAYER; //Give next to player instead of civilians
		}
		else
		{
			_side = FACTION_NEUTRAL;
		}
		// if there is no neutral team, we skip this and instantly prepare the new turn for the player
		if (selectNextPlayerUnit() == 0)
		{
			prepareNewTurn();
			_turn++;
			_side = FACTION_PLAYER;
			if (_lastSelectedUnit && _lastSelectedUnit->isSelectable(FACTION_PLAYER, false, false))
				_selectedUnit = _lastSelectedUnit;
			else
				selectNextPlayerUnit();
			while (_selectedUnit && _selectedUnit->getFaction() != FACTION_PLAYER)
				selectNextPlayerUnit();
		}
	}
	else if (_side == FACTION_NEUTRAL)
	{
		prepareNewTurn();
		_turn++;
		if (Options::battleExtenedCivilians) 
		{
			_side = FACTION_HOSTILE; //Give next turn to aliens instead of player
		}
		else
		{
			_side = FACTION_PLAYER;
		}
		if (_lastSelectedUnit && _lastSelectedUnit->isSelectable(FACTION_PLAYER, false, false))
			_selectedUnit = _lastSelectedUnit;
		else
			selectNextPlayerUnit();
		while (_selectedUnit && _selectedUnit->getFaction() != FACTION_PLAYER)
			selectNextPlayerUnit();
	}
	int liveSoldiers, liveAliens;

	_battleState->getBattleGame()->tallyUnits(liveAliens, liveSoldiers);

	if ((_turn > _cheatTurn / 2 && liveAliens <= 2) || _turn > _cheatTurn)
	{
		_cheating = true;
	}

	if (_side == FACTION_PLAYER)
	{
		// update the "number of turns since last spotted" and the number of turns left sniper AI knows about player units
		for (std::vector<BattleUnit*>::iterator i = _units.begin(); i != _units.end(); ++i)
		{
			if ((*i)->getTurnsSinceSpotted() < 255)
			{
				(*i)->setTurnsSinceSpotted((*i)->getTurnsSinceSpotted() +	1);
			}
			if (_cheating && (*i)->getFaction() == FACTION_PLAYER && !(*i)->isOut())
			{
				(*i)->setTurnsSinceSpotted(0);
			}

			if ((*i)->getTurnsLeftSpottedForSnipers() != 0)
			{
				(*i)->setTurnsLeftSpottedForSnipers((*i)->getTurnsLeftSpottedForSnipers() - 1);
			}
		}
	}
	// hide all aliens (VOF calculations below will turn them visible again)
	for (std::vector<BattleUnit*>::iterator i = _units.begin(); i != _units.end(); ++i)
	{
		if ((*i)->getFaction() == _side)
		{
			(*i)->prepareNewTurn();
		}
		else if ((*i)->getOriginalFaction() == _side)
		{
			(*i)->updateUnitStats(false, true);
		}
		if ((*i)->getFaction() != FACTION_PLAYER)
		{
			(*i)->setVisible(false);
		}

		ModScript::NewTurnUnit::Output arg{};
		ModScript::NewTurnUnit::Worker work{ (*i), this, this->getTurn(), _side };

		work.execute((*i)->getArmor()->getScript<ModScript::NewTurnUnit>(), arg);
	}

	for (auto& item : _items)
	{
		ModScript::NewTurnItem::Output arg{};
		ModScript::NewTurnItem::Worker work{ item, this, this->getTurn(), _side };

		work.execute(item->getRules()->getScript<ModScript::NewTurnItem>(), arg);
	}

	
	if (Options::battleExtenedCivilians) // From "Extend civilians behaviour" by Xtendo-com.
	{// Manual control of civilians by yrizoud
		for (std::vector<BattleUnit*>::iterator i = getUnits()->begin(); i != getUnits()->end(); ++i)
		{ //find civilian from array of units
			if ((*i)->getOriginalFaction() != FACTION_NEUTRAL || (*i)->isOut())
				continue; //skip unit if not civilian
			Position origin = (*i)->getPosition(); //get X,Y,Z coordinats of civilian
			Position originVoxel = getTileEngine()->getSightOriginVoxel(*i);
			originVoxel.z -= 4;

			for (std::vector<BattleUnit*>::const_iterator j = getUnits()->begin(); j != getUnits()->end(); ++j)
			{//find x-com operative from array of units
				if ( (*j)->isOut() || (*j)->getFaction() != FACTION_PLAYER || (*j)->getOriginalFaction() != FACTION_PLAYER )
					continue; //skip unit if not x-com operative
				if ( ((*j)->getStatus() == STATUS_BERSERK || (*j)->getStatus() == STATUS_PANICKING))
					continue; //skip if x-com operative panicked in that turn
				Position target = (*j)->getPosition(); //get X,Y,Z coordinats of x-com operative

				//check for distance from x-com operative to civilian, but do not care about obstacles like walls
				const int horiz_dist = 8; //X and Y distance
				const int vert_dist = 2;  //Z distance
				if (target.x >= origin.x - horiz_dist && target.x <= origin.x + horiz_dist
				&&  target.y >= origin.y - horiz_dist && target.y <= origin.y + horiz_dist
				&&  target.z >= origin.z - vert_dist  && target.z <= origin.z + vert_dist)
				{
					Position dummy;
					//check for visability from x-com to civilian (check for obstacles like walls)
					if (getTileEngine()->visible((*i), (*j)->getTile()))
					{
						// if passed distance and visability check
						// get control of civilian like you use a mind-control
						(*i)->convertToFaction(FACTION_PLAYER);
						getTileEngine()->calculateFOV(origin);
						//getTileEngine()->calculateUnitLighting(); // can't use, it's private now
						(*i)->setTimeUnits((*i)->getBaseStats()->tu);
						(*i)->setEnergy((*i)->getBaseStats()->stamina);
						//(*i)->recoverTimeUnits(); //don't use, freezes game after civilian move
						(*i)->allowReselect();
						// civilian will not panic if x-com operative is two tile away and in the same floor
						if (target.x >= origin.x - 2 && target.x <= origin.x + 2
						&&  target.y >= origin.y - 2 && target.y <= origin.y + 2
						&&  target.z >= origin.z - 1 && target.z <= origin.z + 1)
						{
							if ((*i)->getStatus() == STATUS_PANICKING || (*i)->getStatus() == STATUS_BERSERK) (*i)->abortTurn(); // _status = STATUS_STANDING;
						}
						continue;
					// Another way to check visibility is to calculate shoot trajectory in voxel world
					// but this fails when an other civilian is in the way
					// || getTileEngine()->canTargetUnit(&originVoxel, (*j)->getTile(), &dummy, *i, *j))
					}
				}
			}
		}
	}
	//--



	// re-run calculateFOV() *after* all aliens have been set not-visible
	_tileEngine->recalculateFOV();

	if (_side != FACTION_PLAYER)
		selectNextPlayerUnit();
}

/**
 * Get current animation frame number.
 * @return Numer of frame.
 */
int SavedBattleGame::getAnimFrame() const
{
	return _animFrame;
}

/**
 * Increase animation frame with warparound 705600.
 */
void SavedBattleGame::nextAnimFrame()
{
	_animFrame = (_animFrame + 1) % (64 * 3*3 * 5*5 * 7*7);
}

/**
 * Turns on debug mode.
 */
void SavedBattleGame::setDebugMode()
{
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		_tiles[i].setDiscovered(true, 2);
	}

	_debugMode = true;
}

/**
 * Gets the current debug mode.
 * @return Debug mode.
 */
bool SavedBattleGame::getDebugMode() const
{
	return _debugMode;
}

/**
* Sets the bug hunt mode.
*/
void SavedBattleGame::setBughuntMode(bool bughuntMode)
{
	_bughuntMode = bughuntMode;
}

/**
* Gets the current bug hunt mode.
* @return Bug hunt mode.
*/
bool SavedBattleGame::getBughuntMode() const
{
	return _bughuntMode;
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
BattlescapeState *SavedBattleGame::getBattleState()
{
	return _battleState;
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
BattlescapeGame *SavedBattleGame::getBattleGame()
{
	return _battleState->getBattleGame();
}

/**
 * Sets the BattlescapeState.
 * @param bs A Pointer to a BattlescapeState.
 */
void SavedBattleGame::setBattleState(BattlescapeState *bs)
{
	_battleState = bs;
}

/**
 * Resets all the units to their current standing tile(s).
 */
void SavedBattleGame::resetUnitTiles()
{
	for (std::vector<BattleUnit*>::iterator i = _units.begin(); i != _units.end(); ++i)
	{
		if (!(*i)->isOut())
		{
			int size = (*i)->getArmor()->getSize() - 1;
			if ((*i)->getTile() && (*i)->getTile()->getUnit() == (*i))
			{
				for (int x = size; x >= 0; x--)
				{
					for (int y = size; y >= 0; y--)
					{
						getTile((*i)->getTile()->getPosition() + Position(x,y,0))->setUnit(0);
					}
				}
			}
			for (int x = size; x >= 0; x--)
			{
				for (int y = size; y >= 0; y--)
				{
					Tile *t = getTile((*i)->getPosition() + Position(x,y,0));
					t->setUnit((*i), getTile(t->getPosition() + Position(0,0,-1)));
				}
			}

		}
		if ((*i)->getFaction() == FACTION_PLAYER)
		{
			(*i)->setVisible(true);
		}
	}
	_beforeGame = false;
}

/**
 * Gives access to the "storage space" vector, for distribution of items in base defense missions.
 * @return Vector of storage positions.
 */
std::vector<Position> &SavedBattleGame::getStorageSpace()
{
	return _storageSpace;
}

/**
 * Move all the leftover items in base defense missions to random locations in the storage facilities
 * @param t the tile where all our goodies are initially stored.
 */
void SavedBattleGame::randomizeItemLocations(Tile *t)
{
	if (!_storageSpace.empty())
	{
		for (std::vector<BattleItem*>::iterator it = t->getInventory()->begin(); it != t->getInventory()->end();)
		{
			if ((*it)->getSlot()->getId() == "STR_GROUND")
			{
				getTile(_storageSpace.at(RNG::generate(0, _storageSpace.size() -1)))->addItem(*it, (*it)->getSlot());
				it = t->getInventory()->erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

/**
 * Add item to delete list, usually when removing item form game or build in weapons
 * @param item Item to delete after game end.
 */
void SavedBattleGame::deleteList(BattleItem* item)
{
	_deleted.push_back(item);
}

/**
 * Removes an item from the game. Eg. when ammo item is depleted.
 * @param item The Item to remove.
 */
void SavedBattleGame::removeItem(BattleItem *item)
{
	bool find = false;
	for (std::vector<BattleItem*>::iterator i = _items.begin(); i != _items.end(); ++i)
	{
		if (*i == item)
		{
			find = true;
			_items.erase(i);
			break;
		}
	}
	if (!find)
	{
		return;
	}

	// due to strange design, the item has to be removed from the tile it is on too (if it is on a tile)
	Tile *t = item->getTile();
	BattleUnit *b = item->getOwner();
	if (t)
	{
		for (std::vector<BattleItem*>::iterator it = t->getInventory()->begin(); it != t->getInventory()->end(); ++it)
		{
			if ((*it) == item)
			{
				t->getInventory()->erase(it);
				break;
			}
		}
	}
	if (b)
	{
		for (std::vector<BattleItem*>::iterator it = b->getInventory()->begin(); it != b->getInventory()->end(); ++it)
		{
			if ((*it) == item)
			{
				b->getInventory()->erase(it);
				break;
			}
		}
	}

	deleteList(item);
}

/**
 * Add buildin items from list to unit.
 * @param unit Unit that should get weapon.
 * @param fixed List of buildin items.
 */
void SavedBattleGame::addFixedItems(BattleUnit *unit, const std::vector<std::string> &fixed)
{
	if (!fixed.empty())
	{
		std::vector<RuleItem*> ammo;
		for (std::vector<std::string>::const_iterator j = fixed.begin(); j != fixed.end(); ++j)
		{
			RuleItem *ruleItem = _rule->getItem(*j);
			if (ruleItem)
			{
				if (ruleItem->getBattleType() == BT_AMMO)
				{
					ammo.push_back(ruleItem);
					continue;
				}
				createItemForUnit(ruleItem, unit, true);
			}
		}
		for (std::vector<RuleItem*>::const_iterator j = ammo.begin(); j != ammo.end(); ++j)
		{
			createItemForUnit(*j, unit, true);
		}
	}
}

/**
 * Create all fixed items for new created unit.
 * @param unit Unit to equip.
 */
void SavedBattleGame::initUnit(BattleUnit *unit, size_t itemLevel)
{
	unit->setSpecialWeapon(this);
	Unit* rule = unit->getUnitRules();
	const Armor* armor = unit->getArmor();
	// Built in weapons: the unit has this weapon regardless of loadout or what have you.
	addFixedItems(unit, armor->getBuiltInWeapons());

	// For aliens and HWP
	if (rule)
	{
		auto &buildin = rule->getBuiltInWeapons();
		if (!buildin.empty())
		{
			if (itemLevel >= buildin.size())
			{
				itemLevel = buildin.size() -1;
			}
			// Built in weapons: the unit has this weapon regardless of loadout or what have you.
			addFixedItems(unit, buildin.at(itemLevel));
		}

		// terrorist alien's equipment is a special case - they are fitted with a weapon which is the alien's name with suffix _WEAPON
		if (rule->isLivingWeapon())
		{
			std::string terroristWeapon = rule->getRace().substr(4);
			terroristWeapon += "_WEAPON";
			RuleItem *ruleItem = _rule->getItem(terroristWeapon);
			if (ruleItem)
			{
				BattleItem *item = createItemForUnit(ruleItem, unit);
				if (item)
				{
					unit->setTurretType(item->getRules()->getTurretType());
				}
			}
		}
	}

	ModScript::CreateUnit::Output arg{};
	ModScript::CreateUnit::Worker work{ unit, this, this->getTurn(), };

	work.execute(armor->getScript<ModScript::CreateUnit>(), arg);
}

/**
 * Init new created item.
 * @param item
 */
void SavedBattleGame::initItem(BattleItem *item)
{
	ModScript::CreateItem::Output arg{};
	ModScript::CreateItem::Worker work{ item, this, this->getTurn(), };

	work.execute(item->getRules()->getScript<ModScript::CreateItem>(), arg);
}

/**
 * Create new item for unit.
 */
BattleItem *SavedBattleGame::createItemForUnit(const std::string& type, BattleUnit *unit, bool fixedWeapon)
{
	return createItemForUnit(_rule->getItem(type, true), unit, fixedWeapon);
}

/**
 * Create new item for unit.
 */
BattleItem *SavedBattleGame::createItemForUnit(RuleItem *rule, BattleUnit *unit, bool fixedWeapon)
{
	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	if (!unit->addItem(item, _rule, false, fixedWeapon, fixedWeapon))
	{
		delete item;
		item = nullptr;
	}
	else
	{
		_items.push_back(item);
		initItem(item);
	}
	return item;
}

/**
 * Create new buildin item for unit.
 */
BattleItem *SavedBattleGame::createItemForUnitBuildin(RuleItem *rule, BattleUnit *unit)
{
	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	item->setOwner(unit);
	deleteList(item);
	return item;
}
/**
 * Create new item for tile.
 */
BattleItem *SavedBattleGame::createItemForTile(const std::string& type, Tile *tile)
{
	return createItemForTile(_rule->getItem(type, true), tile);
}

/**
 * Create new item for tile;
 */
BattleItem *SavedBattleGame::createItemForTile(RuleItem *rule, Tile *tile)
{
	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	if (tile)
	{
		RuleInventory *ground = _rule->getInventory("STR_GROUND", true);
		tile->addItem(item, ground);
	}
	_items.push_back(item);
	initItem(item);
	return item;
}

/**
 * Sets whether the mission was aborted or successful.
 * @param flag True, if the mission was aborted, or false, if the mission was successful.
 */
void SavedBattleGame::setAborted(bool flag)
{
	_aborted = flag;
}

/**
 * Returns whether the mission was aborted or successful.
 * @return True, if the mission was aborted, or false, if the mission was successful.
 */
bool SavedBattleGame::isAborted() const
{
	return _aborted;
}

/**
 * increments the number of objectives to be destroyed.
 */
void SavedBattleGame::setObjectiveCount(int counter)
{
	_objectivesNeeded = counter;
	_objectivesDestroyed = 0;
}

/**
 * Sets whether the objective is destroyed.
 */
void SavedBattleGame::addDestroyedObjective()
{
	if (!allObjectivesDestroyed())
	{
		_objectivesDestroyed++;
		if (allObjectivesDestroyed())
		{
			if (getObjectiveType() == MUST_DESTROY)
			{
				_battleState->getBattleGame()->autoEndBattle();
			}
			else
			{
				_battleState->getBattleGame()->missionComplete();
			}
		}
	}
}

/**
 * Returns whether the objectives are destroyed.
 * @return True if the objectives are destroyed.
 */
bool SavedBattleGame::allObjectivesDestroyed() const
{
	return (_objectivesNeeded > 0 && _objectivesDestroyed == _objectivesNeeded);
}

/**
 * Gets the current item ID.
 * @return Current item ID pointer.
 */
int *SavedBattleGame::getCurrentItemId()
{
	return &_itemId;
}

/**
 * Finds a fitting node where a unit can spawn.
 * @param nodeRank Rank of the node (this is not the rank of the alien!).
 * @param unit Pointer to the unit (to get its position).
 * @return Pointer to the chosen node.
 */
Node *SavedBattleGame::getSpawnNode(int nodeRank, BattleUnit *unit)
{
	int highestPriority = -1;
	std::vector<Node*> compliantNodes;

	for (std::vector<Node*>::iterator i = getNodes()->begin(); i != getNodes()->end(); ++i)
	{
		if ((*i)->isDummy())
		{
			continue;
		}
		if ((*i)->getRank() == nodeRank								// ranks must match
			&& (!((*i)->getType() & Node::TYPE_SMALL)
				|| unit->getArmor()->getSize() == 1)				// the small unit bit is not set or the unit is small
			&& (!((*i)->getType() & Node::TYPE_FLYING)
				|| unit->getMovementType() == MT_FLY)				// the flying unit bit is not set or the unit can fly
			&& (*i)->getPriority() > 0								// priority 0 is no spawnplace
			&& setUnitPosition(unit, (*i)->getPosition(), true))	// check if not already occupied
		{
			if ((*i)->getPriority() > highestPriority)
			{
				highestPriority = (*i)->getPriority();
				compliantNodes.clear(); // drop the last nodes, as we found a higher priority now
			}
			if ((*i)->getPriority() == highestPriority)
			{
				compliantNodes.push_back((*i));
			}
		}
	}

	if (compliantNodes.empty()) return 0;

	int n = RNG::generate(0, compliantNodes.size() - 1);

	return compliantNodes[n];
}

/**
 * Finds a fitting node where a unit can patrol to.
 * @param scout Is the unit scouting?
 * @param unit Pointer to the unit (to get its position).
 * @param fromNode Pointer to the node the unit is at.
 * @return Pointer to the chosen node.
 */
Node *SavedBattleGame::getPatrolNode(bool scout, BattleUnit *unit, Node *fromNode)
{
	std::vector<Node *> compliantNodes;
	Node *preferred = 0;

	if (fromNode == 0)
	{
		if (Options::traceAI) { Log(LOG_INFO) << "This alien got lost. :("; }
		fromNode = getNodes()->at(RNG::generate(0, getNodes()->size() - 1));
		while (fromNode->isDummy())
		{
			fromNode = getNodes()->at(RNG::generate(0, getNodes()->size() - 1));
		}
	}

	// scouts roam all over while all others shuffle around to adjacent nodes at most:
	const int end = scout ? getNodes()->size() : fromNode->getNodeLinks()->size();

	for (int i = 0; i < end; ++i)
	{
		if (!scout && fromNode->getNodeLinks()->at(i) < 1) continue;

		Node *n = getNodes()->at(scout ? i : fromNode->getNodeLinks()->at(i));
		if ( !n->isDummy()																				// don't consider dummy nodes.
			&& (n->getFlags() > 0 || n->getRank() > 0 || scout)											// for non-scouts we find a node with a desirability above 0
			&& (!(n->getType() & Node::TYPE_SMALL) || unit->getArmor()->getSize() == 1)					// the small unit bit is not set or the unit is small
			&& (!(n->getType() & Node::TYPE_FLYING) || unit->getMovementType() == MT_FLY)	// the flying unit bit is not set or the unit can fly
			&& !n->isAllocated()																		// check if not allocated
			&& !(n->getType() & Node::TYPE_DANGEROUS)													// don't go there if an alien got shot there; stupid behavior like that
			&& setUnitPosition(unit, n->getPosition(), true)											// check if not already occupied
			&& getTile(n->getPosition()) && !getTile(n->getPosition())->getFire()						// you are not a firefighter; do not patrol into fire
			&& (unit->getFaction() != FACTION_HOSTILE || !getTile(n->getPosition())->getDangerous())	// aliens don't run into a grenade blast
			&& (!scout || n != fromNode)																// scouts push forward
			&& n->getPosition().x > 0 && n->getPosition().y > 0)
		{
			if (!preferred
				|| (unit->getRankInt() >=0 &&
					preferred->getRank() == Node::nodeRank[unit->getRankInt()][0] &&
					preferred->getFlags() < n->getFlags())
				|| preferred->getFlags() < n->getFlags())
			{
				preferred = n;
			}
			compliantNodes.push_back(n);
		}
	}

	if (compliantNodes.empty())
	{
		if (Options::traceAI) { Log(LOG_INFO) << (scout ? "Scout " : "Guard") << " found on patrol node! XXX XXX XXX"; }
		if (unit->getArmor()->getSize() > 1 && !scout)
		{
			return getPatrolNode(true, unit, fromNode); // move dammit
		}
		else
			return 0;
	}

	if (scout)
	{
		// scout picks a random destination:
		return compliantNodes[RNG::generate(0, compliantNodes.size() - 1)];
	}
	else
	{
		if (!preferred) return 0;

		// non-scout patrols to highest value unoccupied node that's not fromNode
		if (Options::traceAI) { Log(LOG_INFO) << "Choosing node flagged " << preferred->getFlags(); }
		return preferred;
	}
}

/**
 * Carries out new turn preparations such as fire and smoke spreading.
 */
void SavedBattleGame::prepareNewTurn()
{
	std::vector<Tile*> tilesOnFire;
	std::vector<Tile*> tilesOnSmoke;

	// prepare a list of tiles on fire
	for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
	{
		if (getTile(i)->getFire() > 0)
		{
			tilesOnFire.push_back(getTile(i));
		}
	}

	// first: fires spread
	for (std::vector<Tile*>::iterator i = tilesOnFire.begin(); i != tilesOnFire.end(); ++i)
	{
		// if we haven't added fire here this turn
		if ((*i)->getOverlaps() == 0)
		{
			// reduce the fire timer
			(*i)->setFire((*i)->getFire() -1);

			// if we're still burning
			if ((*i)->getFire())
			{
				// propagate in four cardinal directions (0, 2, 4, 6)
				for (int dir = 0; dir <= 6; dir += 2)
				{
					Position pos;
					Pathfinding::directionToVector(dir, &pos);
					Tile *t = getTile((*i)->getPosition() + pos);
					// if there's no wall blocking the path of the flames...
					if (t && getTileEngine()->horizontalBlockage((*i), t, DT_IN) == 0)
					{
						// attempt to set this tile on fire
						t->ignite((*i)->getSmoke());
					}
				}
			}
			// fire has burnt out
			else
			{
				(*i)->setSmoke(0);
				// burn this tile, and any object in it, if it's not fireproof/indestructible.
				if ((*i)->getMapData(O_OBJECT))
				{
					if ((*i)->getMapData(O_OBJECT)->getFlammable() != 255 && (*i)->getMapData(O_OBJECT)->getArmor() != 255)
					{
						if ((*i)->destroy(O_OBJECT, getObjectiveType()))
						{
							addDestroyedObjective();
						}
						if ((*i)->destroy(O_FLOOR, getObjectiveType()))
						{
							addDestroyedObjective();
						}
					}
				}
				else if ((*i)->getMapData(O_FLOOR))
				{
					if ((*i)->getMapData(O_FLOOR)->getFlammable() != 255 && (*i)->getMapData(O_FLOOR)->getArmor() != 255)
					{
						if ((*i)->destroy(O_FLOOR, getObjectiveType()))
						{
							addDestroyedObjective();
						}
					}
				}
				getTileEngine()->applyGravity(*i);
			}
		}
	}

	// prepare a list of tiles on fire/with smoke in them (smoke acts as fire intensity)
	for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
	{
		if (getTile(i)->getSmoke() > 0)
		{
			tilesOnSmoke.push_back(getTile(i));
		}
	}

	// now make the smoke spread.
	for (std::vector<Tile*>::iterator i = tilesOnSmoke.begin(); i != tilesOnSmoke.end(); ++i)
	{
		// smoke and fire follow slightly different rules.
		if ((*i)->getFire() == 0)
		{
			// reduce the smoke counter
			(*i)->setSmoke((*i)->getSmoke() - 1);
			// if we're still smoking
			if ((*i)->getSmoke())
			{
				// spread in four cardinal directions
				for (int dir = 0; dir <= 6; dir += 2)
				{
					Position pos;
					Pathfinding::directionToVector(dir, &pos);
					Tile *t = getTile((*i)->getPosition() + pos);
					// as long as there are no walls blocking us
					if (t && getTileEngine()->horizontalBlockage((*i), t, DT_SMOKE) == 0)
					{
						// only add smoke to empty tiles, or tiles with no fire, and smoke that was added this turn
						if (t->getSmoke() == 0 || (t->getFire() == 0 && t->getOverlaps() != 0))
						{
							t->addSmoke((*i)->getSmoke());
						}
					}
				}
			}
		}
		else
		{
			// smoke from fire spreads upwards one level if there's no floor blocking it.
			Position pos = Position(0,0,1);
			Tile *t = getTile((*i)->getPosition() + pos);
			if (t && t->hasNoFloor(*i))
			{
				// only add smoke equal to half the intensity of the fire
				t->addSmoke((*i)->getSmoke()/2);
			}
			// then it spreads in the four cardinal directions.
			for (int dir = 0; dir <= 6; dir += 2)
			{
				Pathfinding::directionToVector(dir, &pos);
				t = getTile((*i)->getPosition() + pos);
				if (t && getTileEngine()->horizontalBlockage((*i), t, DT_SMOKE) == 0)
				{
					t->addSmoke((*i)->getSmoke()/2);
				}
			}
		}
	}

	if (!tilesOnFire.empty() || !tilesOnSmoke.empty())
	{
		// do damage to units, average out the smoke, etc.
		for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
		{
			if (getTile(i)->getSmoke() != 0)
				getTile(i)->prepareNewTurn();
		}
	}

	Mod *mod = getBattleState()->getGame()->getMod();
	for (std::vector<BattleUnit*>::iterator i = getUnits()->begin(); i != getUnits()->end(); ++i)
	{
		(*i)->calculateEnviDamage(mod, this);
	}

	reviveUnconsciousUnits();

	// fires could have been started, stopped or smoke could reveal/conceal units.
	getTileEngine()->calculateLighting(LL_FIRE);
	getTileEngine()->recalculateFOV();
}

/**
 * Checks for units that are unconscious and revives them if they shouldn't be.
 *
 * Revived units need a tile to stand on. If the unit's current position is occupied, then
 * all directions around the tile are searched for a free tile to place the unit in.
 * If no free tile is found the unit stays unconscious.
 */
void SavedBattleGame::reviveUnconsciousUnits(bool noTU)
{
	for (std::vector<BattleUnit*>::iterator i = getUnits()->begin(); i != getUnits()->end(); ++i)
	{
		if ((*i)->getArmor()->getSize() == 1)
		{
			Position originalPosition = (*i)->getPosition();
			if (originalPosition == Position(-1, -1, -1))
			{
				for (std::vector<BattleItem*>::iterator j = _items.begin(); j != _items.end(); ++j)
				{
					if ((*j)->getUnit() && (*j)->getUnit() == *i && (*j)->getOwner())
					{
						originalPosition = (*j)->getOwner()->getPosition();
					}
				}
			}
			if ((*i)->getStatus() == STATUS_UNCONSCIOUS && (*i)->getStunlevel() < (*i)->getHealth() && (*i)->getHealth() > 0)
			{
				Tile *targetTile = getTile(originalPosition);
				bool largeUnit =  targetTile && targetTile->getUnit() && targetTile->getUnit() != *i && targetTile->getUnit()->getArmor()->getSize() != 1;
				if (placeUnitNearPosition((*i), originalPosition, largeUnit))
				{
					// recover from unconscious
					(*i)->turn(false); // makes the unit stand up again
					(*i)->kneel(false);
					if (noTU) (*i)->setTimeUnits(0);
					removeUnconsciousBodyItem((*i));
				}
			}
		}
	}
}

/**
 * Removes the body item that corresponds to the unit.
 */
void SavedBattleGame::removeUnconsciousBodyItem(BattleUnit *bu)
{
	int size = bu->getArmor()->getSize();
	size *= size;
	// remove the unconscious body item corresponding to this unit
	for (std::vector<BattleItem*>::iterator it = getItems()->begin(); it != getItems()->end(); )
	{
		if ((*it)->getUnit() == bu)
		{
			removeItem((*it));
			if (--size == 0) break;
		}
		else
		{
			++it;
		}
	}
}

/**
 * Places units on the map. Handles large units that are placed on multiple tiles.
 * @param bu The unit to be placed.
 * @param position The position to place the unit.
 * @param testOnly If true then just checks if the unit can be placed at the position.
 * @return True if the unit could be successfully placed.
 */
bool SavedBattleGame::setUnitPosition(BattleUnit *bu, Position position, bool testOnly)
{
	int size = bu->getArmor()->getSize() - 1;
	Position zOffset (0,0,0);
	// first check if the tiles are occupied
	for (int x = size; x >= 0; x--)
	{
		for (int y = size; y >= 0; y--)
		{
			Tile *t = getTile(position + Position(x,y,0) + zOffset);
			Tile *tb = getTile(position + Position(x,y,-1) + zOffset);
			if (t == 0 ||
				(t->getUnit() != 0 && t->getUnit() != bu) ||
				t->getTUCost(O_OBJECT, bu->getMovementType()) == 255 ||
				(t->hasNoFloor(tb) && bu->getMovementType() != MT_FLY) ||
				(t->getMapData(O_OBJECT) && t->getMapData(O_OBJECT)->getBigWall() && t->getMapData(O_OBJECT)->getBigWall() <= 3))
			{
				return false;
			}
			// move the unit up to the next level (desert and seabed terrains)
			if (t && t->getTerrainLevel() == -24)
			{
				zOffset.z += 1;
				x = size;
				y = size + 1;
			}
		}
	}

	if (size > 0)
	{
		getPathfinding()->setUnit(bu);
		for (int dir = 2; dir <= 4; ++dir)
		{
			if (getPathfinding()->isBlocked(getTile(position + zOffset), 0, dir, 0))
				return false;
		}
	}

	if (testOnly) return true;

	for (int x = size; x >= 0; x--)
	{
		for (int y = size; y >= 0; y--)
		{
			if (x==0 && y==0)
			{
				bu->setPosition(position + zOffset);
			}
			getTile(position + Position(x,y,0) + zOffset)->setUnit(bu, getTile(position + Position(x,y,-1) + zOffset));
		}
	}

	return true;
}

/**
 * @brief Checks whether anyone on a particular faction is looking at the unit.
 *
 * Similar to getSpottingUnits() but returns a bool and stops searching if one positive hit is found.
 *
 * @param faction Faction to check through.
 * @param unit Whom to spot.
 * @return True when the unit can be seen
 */
bool SavedBattleGame::eyesOnTarget(UnitFaction faction, BattleUnit* unit)
{
	for (std::vector<BattleUnit*>::iterator i = getUnits()->begin(); i != getUnits()->end(); ++i)
	{
		if ((*i)->getFaction() != faction) continue;

		std::vector<BattleUnit*> *vis = (*i)->getVisibleUnits();
		if (std::find(vis->begin(), vis->end(), unit) != vis->end()) return true;
		// aliens know the location of all XCom agents sighted by all other aliens due to sharing locations over their space-walkie-talkies
	}

	return false;
}

/**
 * Adds this unit to the vector of falling units,
 * if it doesn't already exist.
 * @param unit The unit.
 * @return Was the unit added?
 */
bool SavedBattleGame::addFallingUnit(BattleUnit* unit)
{
	bool add = true;
	for (std::list<BattleUnit*>::iterator i = _fallingUnits.begin(); i != _fallingUnits.end(); ++i)
	{
		if (unit == *i)
		{
			add = false;
			break;
		}
	}
	if (add)
	{
		_fallingUnits.push_front(unit);
		_unitsFalling = true;
	}
	return add;
}

/**
 * Gets all units in the battlescape that are falling.
 * @return The falling units in the battlescape.
 */
std::list<BattleUnit*> *SavedBattleGame::getFallingUnits()
{
	return &_fallingUnits;
}

/**
 * Toggles the switch that says "there are units falling, start the fall state".
 * @param fall True if there are any units falling in the battlescape.
 */
void SavedBattleGame::setUnitsFalling(bool fall)
{
	_unitsFalling = fall;
}

/**
 * Returns whether there are any units falling in the battlescape.
 * @return True if there are any units falling in the battlescape.
 */
bool SavedBattleGame::getUnitsFalling() const
{
	return _unitsFalling;
}

/**
 * Gets the highest ranked, living XCom unit.
 * @return The highest ranked, living XCom unit.
 */
BattleUnit* SavedBattleGame::getHighestRankedXCom()
{
	BattleUnit* highest = 0;
	for (std::vector<BattleUnit*>::iterator j = _units.begin(); j != _units.end(); ++j)
	{
		if ((*j)->getOriginalFaction() == FACTION_PLAYER && !(*j)->isOut())
		{
			if (highest == 0 || (*j)->getRankInt() > highest->getRankInt())
			{
				highest = *j;
			}
		}
	}
	return highest;
}

/**
 * Gets morale modifier of unit.
 * @param unit
 * @return Morale modifier.
 */
int SavedBattleGame::getUnitMoraleModifier(BattleUnit* unit)
{
	int result = 100;

	if (unit->getOriginalFaction() == FACTION_PLAYER)
	{
		switch (unit->getRankInt())
		{
		case 5:
			result += 25;
		case 4:
			result += 20;
		case 3:
			result += 10;
		case 2:
			result += 20;
		default:
			break;
		}
	}

	return result;
}

/**
 * Gets the morale modifier for XCom based on the highest ranked, living XCom unit,
 * or Alien modifier based on they number.
 * @param hostile modifier for player or hostile?
 * @return The morale modifier.
 */
int SavedBattleGame::getFactionMoraleModifier(bool player)
{
	if (player)
	{
		BattleUnit *leader = getHighestRankedXCom();
		int result = 100;
		if (leader)
		{
			switch (leader->getRankInt())
			{
			case 5:
				result += 25;
			case 4:
				result += 10;
			case 3:
				result += 5;
			case 2:
				result += 10;
			default:
				break;
			}
		}
		return result;
	}
	else
	{
		int number = 0;
		for (std::vector<BattleUnit*>::iterator j = _units.begin(); j != _units.end(); ++j)
		{
			if ((*j)->getOriginalFaction() == FACTION_HOSTILE && !(*j)->isOut())
			{
				++number;
			}
		}
		return std::max(6 * number, 100);
	}
}

/**
 * Places a unit on or near a position.
 * @param unit The unit to place.
 * @param entryPoint The position around which to attempt to place the unit.
 * @return True if the unit was successfully placed.
 */
bool SavedBattleGame::placeUnitNearPosition(BattleUnit *unit, const Position& entryPoint, bool largeFriend)
{
	if (setUnitPosition(unit, entryPoint))
	{
		return true;
	}

	int me = 0 - unit->getArmor()->getSize();
	int you = largeFriend ? 2 : 1;
	int xArray[8] = {0, you, you, you, 0, me, me, me};
	int yArray[8] = {me, me, 0, you, you, you, 0, me};
	for (int dir = 0; dir <= 7; ++dir)
	{
		Position offset = Position (xArray[dir], yArray[dir], 0);
		Tile *t = getTile(entryPoint + offset);
		if (t && !getPathfinding()->isBlocked(getTile(entryPoint + (offset / 2)), t, dir, 0)
			&& setUnitPosition(unit, entryPoint + offset))
		{
			return true;
		}
	}

	if (unit->getMovementType() == MT_FLY)
	{
		Tile *t = getTile(entryPoint + Position(0, 0, 1));
		if (t && t->hasNoFloor(getTile(entryPoint)) && setUnitPosition(unit, entryPoint + Position(0, 0, 1)))
		{
			return true;
		}
	}
	return false;
}

/**
 * Resets the turn counter.
 */
void SavedBattleGame::resetTurnCounter()
{
	_turn = 1;
	_cheating = false;
	_side = FACTION_PLAYER;
	_beforeGame = true;
}

/**
 * Resets visibility of all the tiles on the map.
 */
void SavedBattleGame::resetTiles()
{
	for (int i = 0; i != getMapSizeXYZ(); ++i)
	{
		_tiles[i].setDiscovered(false, 0);
		_tiles[i].setDiscovered(false, 1);
		_tiles[i].setDiscovered(false, 2);
	}
}

/**
 * @return the tilesearch vector for use in AI functions.
 */
const std::vector<Position> &SavedBattleGame::getTileSearch() const
{
	return _tileSearch;
}

/**
 * is the AI allowed to cheat?
 * @return true if cheating.
 */
bool SavedBattleGame::isCheating() const
{
	return _cheating;
}

/**
 * Gets the TU reserved type.
 * @return A battleactiontype.
 */
BattleActionType SavedBattleGame::getTUReserved() const
{
	return _tuReserved;
}

/**
 * Sets the TU reserved type.
 * @param reserved A battleactiontype.
 */
void SavedBattleGame::setTUReserved(BattleActionType reserved)
{
	_tuReserved = reserved;
}

/**
 * Gets the kneel reservation setting.
 * @return Should we reserve an extra 4 TUs to kneel?
 */
bool SavedBattleGame::getKneelReserved() const
{
	return _kneelReserved;
}

/**
 * Sets the kneel reservation setting.
 * @param reserved Should we reserve an extra 4 TUs to kneel?
 */
void SavedBattleGame::setKneelReserved(bool reserved)
{
	_kneelReserved = reserved;
}

/**
 * Return a reference to the base module destruction map
 * this map contains information on how many destructible base modules
 * remain at any given grid reference in the basescape, using [x][y] format.
 * -1 for "no items" 0 for "destroyed" and any actual number represents how many left.
 * @return the base module damage map.
 */
std::vector< std::vector<std::pair<int, int> > > &SavedBattleGame::getModuleMap()
{
	return _baseModules;
}

/**
 * calculate the number of map modules remaining by counting the map objects
 * on the top floor who have the baseModule flag set. we store this data in the grid
 * as outlined in the comments above, in pairs representing initial and current values.
 */
void SavedBattleGame::calculateModuleMap()
{
	_baseModules.resize((_mapsize_x / 10), std::vector<std::pair<int, int> >((_mapsize_y / 10), std::make_pair(-1, -1)));

	for (int x = 0; x != _mapsize_x; ++x)
	{
		for (int y = 0; y != _mapsize_y; ++y)
		{
			for (int z = 0; z != _mapsize_z; ++z)
			{
				Tile *tile = getTile(Position(x,y,z));
				if (tile && tile->getMapData(O_OBJECT) && tile->getMapData(O_OBJECT)->isBaseModule())
				{
					_baseModules[x/10][y/10].first += _baseModules[x/10][y/10].first > 0 ? 1 : 2;
					_baseModules[x/10][y/10].second = _baseModules[x/10][y/10].first;
				}
			}
		}
	}
}

/**
 * get a pointer to the geoscape save
 * @return a pointer to the geoscape save.
 */
SavedGame *SavedBattleGame::getGeoscapeSave()
{
	return _battleState->getGame()->getSavedGame();
}

/**
 * check the depth of the battlescape.
 * @return depth.
 */
int SavedBattleGame::getDepth() const
{
	return _depth;
}

/**
 * set the depth of the battlescape game.
 * @param depth the intended depth 0-3.
 */
void SavedBattleGame::setDepth(int depth)
{
	_depth = depth;
}

/**
 * uses the depth variable to choose a palette.
 * @param state the state to set the palette for.
 */
void SavedBattleGame::setPaletteByDepth(State *state)
{
	if (_depth == 0)
	{
		state->setPalette("PAL_BATTLESCAPE");
	}
	else
	{
		std::ostringstream ss;
		ss << "PAL_BATTLESCAPE_" << _depth;
		state->setPalette(ss.str());
	}
}

/**
 * set the ambient battlescape sound effect.
 * @param sound the intended sound.
 */
void SavedBattleGame::setAmbientSound(int sound)
{
	_ambience = sound;
}

/**
 * get the ambient battlescape sound effect.
 * @return the intended sound.
 */
int SavedBattleGame::getAmbientSound() const
{
	return _ambience;
}

/**
 * get ruleset.
 * @return the ruleset of game.
 */
const Mod *SavedBattleGame::getMod() const
{
	return _rule;
}

/**
 * get the list of items we're guaranteed to take with us (ie: items that were in the skyranger)
 * @return the list of items we're garaunteed.
 */
std::vector<BattleItem*> *SavedBattleGame::getGuaranteedRecoveredItems()
{
	return &_recoverGuaranteed;
}

/**
 * get the list of items we're not guaranteed to take with us (ie: items that were NOT in the skyranger)
 * @return the list of items we might get.
 */
std::vector<BattleItem*> *SavedBattleGame::getConditionalRecoveredItems()
{
	return &_recoverConditional;
}

/**
 * Get the music track for the current battle.
 * @return the name of the music track.
 */
const std::string &SavedBattleGame::getMusic() const
{
	return _music;
}

/**
 * Set the music track for this battle.
 * @param track the track name.
 */
void SavedBattleGame::setMusic(const std::string &track)
{
	_music = track;
}

/**
 * Set the objective type for the current battle.
 * @param the objective type.
 */
void SavedBattleGame::setObjectiveType(int type)
{
	_objectiveType = type;
}

/**
 * Get the objective type for the current battle.
 * @return the objective type.
 */
SpecialTileType SavedBattleGame::getObjectiveType() const
{
	return (SpecialTileType)(_objectiveType);
}



/**
 * Sets the ambient sound effect volume.
 * @param volume the ambient volume.
 */
void SavedBattleGame::setAmbientVolume(double volume)
{
	_ambientVolume = volume;
}

/**
 * Gets the ambient sound effect volume.
 * @return the ambient sound volume.
 */
double SavedBattleGame::getAmbientVolume() const
{
	return _ambientVolume;
}

/**
 * Gets the maximum number of turns we have before this mission ends.
 * @return the turn limit.
 */
int SavedBattleGame::getTurnLimit() const
{
	return _turnLimit;
}

/**
 * Gets the action type to perform when the timer expires.
 * @return the action type to perform.
 */
ChronoTrigger SavedBattleGame::getChronoTrigger() const
{
	return _chronoTrigger;
}

/**
 * Sets the turn limit for this mission.
 * @param limit the turn limit.
 */
void SavedBattleGame::setTurnLimit(int limit)
{
	_turnLimit = limit;
}

/**
 * Sets the action type to occur when the timer runs out.
 * @param trigger the action type to perform.
 */
void SavedBattleGame::setChronoTrigger(ChronoTrigger trigger)
{
	_chronoTrigger = trigger;
}

/**
 * Sets the turn at which the players become exposed to the AI.
 * @param turn the turn to start cheating.
 */
void SavedBattleGame::setCheatTurn(int turn)
{
	_cheatTurn = turn;
}

bool SavedBattleGame::isBeforeGame() const
{
	return _beforeGame;
}

namespace
{

void randomChanceScript(SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = RNG::percent(val);
	}
	else
	{
		val = 0;
	}
}

void randomRangeScript(SavedBattleGame* sbg, int& val, int min, int max)
{
	if (sbg && max >= min)
	{
		val = RNG::generate(min, max);
	}
	else
	{
		val = 0;
	}
}

} // namespace

/**
 * Randomly chooses hidden movement background.
 */
void SavedBattleGame::setRandomHiddenMovementBackground(const Mod *mod)
{
	if (mod && !mod->getHiddenMovementBackgrounds().empty())
	{
		int rng = RNG::generate(0, mod->getHiddenMovementBackgrounds().size() - 1);
		_hiddenMovementBackground = mod->getHiddenMovementBackgrounds().at(rng);
	}
	else
	{
		_hiddenMovementBackground = "TAC00.SCR";
	}
}

/**
 * Gets the hidden movement background ID.
 * @return hidden movement background ID
 */
std::string SavedBattleGame::getHiddenMovementBackground() const
{
	return _hiddenMovementBackground;
}

/**
 * Register Armor in script parser.
 * @param parser Script parser.
 */
void SavedBattleGame::ScriptRegister(ScriptParserBase* parser)
{
	Bind<SavedBattleGame> sbg = { parser };

	sbg.add<&SavedBattleGame::getTurn>("getTurn");
	sbg.add<&SavedBattleGame::getAnimFrame>("getAnimFrame");

	sbg.add<&randomChanceScript>("randomChance");
	sbg.add<&randomRangeScript>("randomRange");

	sbg.addScriptValue<&SavedBattleGame::_scriptValues>(true);
}

}
