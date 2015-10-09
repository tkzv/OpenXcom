/*
 * Copyright 2010-2015 OpenXcom Developers.
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
#define _USE_MATH_DEFINES
#include <cmath>
#include "UnitSprite.h"
#include "../Engine/SurfaceSet.h"
#include "../Mod/RuleItem.h"
#include "../Mod/Armor.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/Mod.h"
#include "../Engine/ShaderDraw.h"
#include "../Engine/ShaderMove.h"
#include "../Engine/Options.h"

namespace OpenXcom
{

/**
 * Sets up a UnitSprite with the specified size and position.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
UnitSprite::UnitSprite(Surface* dest, Mod* mod, int frame, bool helmet) :
	_unit(0), _itemA(0), _itemB(0),
	_unitSurface(0),
	_itemSurfaceA(mod->getSurfaceSet("HANDOB.PCK")), _itemSurfaceB(mod->getSurfaceSet("HANDOB2.PCK")),
	_fireSurface(mod->getSurfaceSet("SMOKE.PCK")),
	_dest(dest), _mod(mod),
	_part(0), _animationFrame(frame), _drawingRoutine(0),
	_helmet(helmet), _half(false), _color(0), _colorSize(0),
	_x(0), _y(0), _shade(0),
	_scriptWorkRef()
{

}

/**
 * Deletes the UnitSprite.
 */
UnitSprite::~UnitSprite()
{

}

namespace
{

struct ColorReplace
{
	static inline void loop(Uint8& dest, const Uint8& src, const Uint8& override, int burn, int shade)
	{
		const Uint8 temp = (src & helper::ColorShade) + override;
		if (burn)
		{
			const Uint8 tempBurn = (temp & helper::ColorShade) + burn;
			if (tempBurn > 26)
			{
				//nothing
			}
			else if (tempBurn > 15)
			{
				helper::StandardShade::func(dest, helper::ColorShade, shade);
			}
			else
			{
				helper::StandardShade::func(dest, (temp & helper::ColorGroup) + tempBurn, shade);
			}
		}
		else
		{
			helper::StandardShade::func(dest, temp, shade);
		}
	}

	static inline void func(Uint8& dest, const Uint8& src, const std::pair<Uint8, Uint8> *color, int size, int burn, int shade)
	{
		if (src)
		{
			const int group = (src & helper::ColorGroup);
			for (int i = 0; i < size; ++i)
			{
				if (group == color[i].first)
				{
					loop(dest, src, color[i].second, burn, shade);
					return;
				}
			}
			loop(dest, 0, src, burn, shade);
		}
	}
};

BattleItem *getIfVisible(BattleItem *item)
{
	if (item && (!item->getRules()->isFixed() || item->getRules()->getFixedShow()))
	{
		return item;
	}
	return 0;
}

} //namespace

/**
 * blit item sprite onto surface
 * @param item item sprite, can be null
 */
void UnitSprite::blitItem(Surface* item)
{
	if (item)
	{
		item->blitNShade(_dest, _x + item->getX(), _y + item->getY(), _shade, _half);
	}
}

/**
 * blit body sprite onto surface with optional recoloring
 * @param body
 */
void UnitSprite::blitBody(Surface* body, int part)
{
	if(_scriptWorkRef.proc)
	{
		BattleUnit::ScriptFillCustom(&_scriptWorkRef, part, _animationFrame, _shade);
		_scriptWorkRef.executeBlit(body, _dest,  _x + body->getX(), _y + body->getY(), _half);
	}
	else
	{
		int burn = 0;
		int overkill = _unit->getOverKillDamage();
		int maxHp = _unit->getBaseStats()->health;
		if (overkill)
		{
			if (overkill > maxHp)
			{
				burn = 16 * (_unit->getFallingPhase() + 1) / _unit->getArmor()->getDeathFrames();
			}
			else
			{
				burn = 16 * overkill * (_unit->getFallingPhase() + 1) / _unit->getArmor()->getDeathFrames() / maxHp;
			}
		}
		ShaderMove<Uint8> dest(_dest, 0, 0);
		if (_half)
		{
			GraphSubset g = dest.getDomain();
			g.beg_x = _x + body->getWidth() / 2;
			dest.setDomain(g);
		}
		_dest->lock();
		ShaderDraw<ColorReplace>(
			dest,
			ShaderSurface(body, _x + body->getX(), _y + body->getY()),
			ShaderScalar(_color),
			ShaderScalar(_colorSize),
			ShaderScalar(burn),
			ShaderScalar(_shade)
		);
		_dest->unlock();
	}
}

/**
 * Draws a unit, using the drawing rules of the unit.
 * This function is called by Map, for each unit on the screen.
 */
void UnitSprite::draw(BattleUnit* unit, int part, int x, int y, int shade, bool half)
{
	_x = x;
	_y = y;

	_unit = unit;
	_part = part;
	_shade = shade;
	_half = half;

	_itemA = getIfVisible(_unit->getItem("STR_RIGHT_HAND"));
	_itemB = getIfVisible(_unit->getItem("STR_LEFT_HAND"));

	_unitSurface = _mod->getSurfaceSet(_unit->getArmor()->getSpriteSheet());

	BattleUnit::ScriptFill(&_scriptWorkRef, _unit);

	_drawingRoutine = _unit->getArmor()->getDrawingRoutine();
	if (Options::battleHairBleach)
	{
		_colorSize = _unit->getRecolor().size();
		if (_colorSize)
		{
			_color = &(_unit->getRecolor()[0]);
		}
		else
		{
			_color = 0;
		}
	}

	// Array of drawing routines
	void (UnitSprite::*routines[])() =
	{
		&UnitSprite::drawRoutine0,
		&UnitSprite::drawRoutine1,
		&UnitSprite::drawRoutine2,
		&UnitSprite::drawRoutine3,
		&UnitSprite::drawRoutine4,
		&UnitSprite::drawRoutine5,
		&UnitSprite::drawRoutine6,
		&UnitSprite::drawRoutine7,
		&UnitSprite::drawRoutine8,
		&UnitSprite::drawRoutine9,
		&UnitSprite::drawRoutine0,
		&UnitSprite::drawRoutine11,
		&UnitSprite::drawRoutine12,
		&UnitSprite::drawRoutine0,
		&UnitSprite::drawRoutine0,
		&UnitSprite::drawRoutine0,
		&UnitSprite::drawRoutine12,
		&UnitSprite::drawRoutine4,
		&UnitSprite::drawRoutine4,
		&UnitSprite::drawRoutine19,
		&UnitSprite::drawRoutine20,
		&UnitSprite::drawRoutine21,
	};
	// Call the matching routine
	(this->*(routines[_drawingRoutine]))();
	// draw fire
	if (unit->getFire() > 0)
	{
		_fireSurface->getFrame(4 + (_animationFrame / 2) % 4)->blitNShade(_dest, _x, _y, 0);
	}
}

/**
 * Drawing routine for XCom soldiers in overalls, sectoids (routine 0),
 * mutons (routine 10),
 * aquanauts (routine 13),
 * calcinites, deep ones, gill men, lobster men, tasoths (routine 14),
 * aquatoids (routine 15) (this one is no different, it just precludes breathing animations.
 */
void UnitSprite::drawRoutine0()
{
	Surface *torso = 0, *legs = 0, *leftArm = 0, *rightArm = 0, *itemA = 0, *itemB = 0;
	// magic numbers
	const int legsStand = 16, legsKneel = 24;
	int maleTorso, femaleTorso, die, rarm1H, larm2H, rarm2H, rarmShoot, legsFloat, torsoHandsWeaponY = 0;
	if (_drawingRoutine <= 10)
	{
		die = 264; // ufo:eu death frame
		maleTorso = 32;
		femaleTorso = 267;
		rarm1H = 232;
		larm2H = 240;
		rarm2H = 248;
		rarmShoot = 256;
		legsFloat = 275;
	}
	else if (_drawingRoutine == 13)
	{
		if (_helmet)
		{
			die = 259; // aquanaut underwater death frame
			maleTorso = 32; // aquanaut underwater ion armour torso

            if (_unit->getArmor()->getForcedTorso() == TORSO_USE_GENDER)
			{
				femaleTorso = 32; // aquanaut underwater plastic aqua armour torso
			}
			else
			{
				femaleTorso = 286; // aquanaut underwater magnetic ion armour torso
			}
			rarm1H = 248;
			larm2H = 232;
			rarm2H = rarmShoot = 240;
			legsFloat = 294;
		}
		else
		{
			die = 256; // aquanaut land death frame
			// aquanaut land torso
			maleTorso = 270;
			femaleTorso = 262;
			rarm1H = 248;
			larm2H = 232;
			rarm2H = rarmShoot = 240;
			legsFloat = 294;
		}
	}
	else
	{
		die = 256; // tftd unit death frame
		// tftd unit torso
		maleTorso = 32;
		femaleTorso = 262;
		rarm1H = 248;
		larm2H = 232;
		rarm2H = rarmShoot = 240;
		legsFloat = 294;
	}
	const int larmStand = 0, rarmStand = 8;
	const int legsWalk[8] = { 56, 56+24, 56+24*2, 56+24*3, 56+24*4, 56+24*5, 56+24*6, 56+24*7 };
	const int larmWalk[8] = { 40, 40+24, 40+24*2, 40+24*3, 40+24*4, 40+24*5, 40+24*6, 40+24*7 };
	const int rarmWalk[8] = { 48, 48+24, 48+24*2, 48+24*3, 48+24*4, 48+24*5, 48+24*6, 48+24*7 };
	const int YoffWalk[8] = {1, 0, -1, 0, 1, 0, -1, 0}; // bobbing up and down
	const int mutonYoffWalk[8] = {1, 1, 0, 0, 1, 1, 0, 0}; // bobbing up and down (muton)
	const int aquatoidYoffWalk[8] = {1, 0, 0, 1, 2, 1, 0, 0}; // bobbing up and down (aquatoid)
	const int offX[8] = { 8, 10, 7, 4, -9, -11, -7, -3 }; // for the weapons
	const int offY[8] = { -6, -3, 0, 2, 0, -4, -7, -9 }; // for the weapons
	const int offX2[8] = { -8, 3, 5, 12, 6, -1, -5, -13 }; // for the left handed weapons
	const int offY2[8] = { 1, -4, -2, 0, 3, 3, 5, 0 }; // for the left handed weapons
	const int offX3[8] = { 0, 0, 2, 2, 0, 0, 0, 0 }; // for the weapons (muton)
	const int offY3[8] = { -3, -3, -1, -1, -1, -3, -3, -2 }; // for the weapons (muton)
	const int offX4[8] = { -8, 2, 7, 14, 7, -2, -4, -8 }; // for the left handed weapons
	const int offY4[8] = { -3, -3, -1, 0, 3, 3, 0, 1 }; // for the left handed weapons
	const int offX5[8] = { -1, 1, 1, 2, 0, -1, 0, 0 }; // for the weapons (muton)
	const int offY5[8] = { 1, -1, -1, -1, -1, -1, -3, 0 }; // for the weapons (muton)
	const int offX6[8] = { 0, 6, 6, 12, -4, -5, -5, -13 }; // for the left handed rifles
	const int offY6[8] = { -4, -4, -1, 0, 5, 0, 1, 0 }; // for the left handed rifles
	const int offX7[8] = { 0, 6, 8, 12, 2, -5, -5, -13 }; // for the left handed rifles (muton)
	const int offY7[8] = { -4, -6, -1, 0, 3, 0, 1, 0 }; // for the left handed rifles (muton)
	const int offYKneel = 4;
	const int offXAiming = 16;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	const int unitDir = _unit->getDirection();
	const int walkPhase = _unit->getWalkingPhase();

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		torso = _unitSurface->getFrame(die + _unit->getFallingPhase());

		blitBody(torso, BODYPART_COLLAPSING);
		return;
	}
	if (_drawingRoutine == 0 || _helmet)
	{
		if ((_unit->getGender() == GENDER_FEMALE && _unit->getArmor()->getForcedTorso() != TORSO_ALWAYS_MALE)
			|| _unit->getArmor()->getForcedTorso() == TORSO_ALWAYS_FEMALE)
		{
			torso = _unitSurface->getFrame(femaleTorso + unitDir);
		}
		else
		{
			torso = _unitSurface->getFrame(maleTorso + unitDir);
		}
	}
	else
	{
		if (_unit->getGender() == GENDER_FEMALE)
		{
			torso = _unitSurface->getFrame(femaleTorso + unitDir);
		}
		else
		{
			torso = _unitSurface->getFrame(maleTorso + unitDir);
		}
	}


	// when walking, torso(fixed sprite) has to be animated up/down
	if (_unit->getStatus() == STATUS_WALKING)
	{

		if (_drawingRoutine == 10)
			torsoHandsWeaponY = mutonYoffWalk[walkPhase];
		else if (_drawingRoutine == 13 || _drawingRoutine == 14)
			torsoHandsWeaponY = YoffWalk[walkPhase]+1;
		else if (_drawingRoutine == 15)
			torsoHandsWeaponY = aquatoidYoffWalk[walkPhase];
		else
			torsoHandsWeaponY = YoffWalk[walkPhase];
		torso->setY(torsoHandsWeaponY);
		legs = _unitSurface->getFrame(legsWalk[unitDir] + walkPhase);
		leftArm = _unitSurface->getFrame(larmWalk[unitDir] + walkPhase);
		rightArm = _unitSurface->getFrame(rarmWalk[unitDir] + walkPhase);
		if (_drawingRoutine == 10 && unitDir == 3)
		{
			leftArm->setY(-1);
		}
	}
	else
	{
		if (_unit->isKneeled())
		{
			legs = _unitSurface->getFrame(legsKneel + unitDir);
		}
		else if (_unit->isFloating() && _unit->getMovementType() == MT_FLY)
		{
			legs = _unitSurface->getFrame(legsFloat + unitDir);
		}
		else
		{
			legs = _unitSurface->getFrame(legsStand + unitDir);
		}
		leftArm = _unitSurface->getFrame(larmStand + unitDir);
		rightArm = _unitSurface->getFrame(rarmStand + unitDir);
	}

	sortRifles();

	// holding an item
	if (_itemA)
	{
		// draw handob item
		if (_unit->getStatus() == STATUS_AIMING && _itemA->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + dir);
			itemA->setX(offX[unitDir]);
			itemA->setY(offY[unitDir]);
		}
		else
		{
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + unitDir);
			if (_drawingRoutine == 10)
			{
				if (_itemA->getRules()->isTwoHanded())
				{
					itemA->setX(offX3[unitDir]);
					itemA->setY(offY3[unitDir]);
				}
				else
				{
					itemA->setX(offX5[unitDir]);
					itemA->setY(offY5[unitDir]);
				}
			}
			else
			{
				itemA->setX(0);
				itemA->setY(0);
			}
		}

		// draw arms holding the item
		if (_itemA->getRules()->isTwoHanded())
		{
			leftArm = _unitSurface->getFrame(larm2H + unitDir);
			if (_unit->getStatus() == STATUS_AIMING)
			{
				rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
			}
			else
			{
				rightArm = _unitSurface->getFrame(rarm2H + unitDir);
			}
		}
		else
		{
			if (_drawingRoutine == 10)
				rightArm = _unitSurface->getFrame(rarm2H + unitDir);
			else
				rightArm = _unitSurface->getFrame(rarm1H + unitDir);
		}


		// the fixed arm(s) have to be animated up/down when walking
		if (_unit->getStatus() == STATUS_WALKING)
		{
			itemA->setY(itemA->getY() + torsoHandsWeaponY);
            rightArm->setY(torsoHandsWeaponY);
			if (_itemA->getRules()->isTwoHanded())
				leftArm->setY(torsoHandsWeaponY);
		}
	}
	//if we are left handed or dual wielding...
	if (_itemB)
	{
		leftArm = _unitSurface->getFrame(larm2H + unitDir);
		itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + unitDir);
		if (!_itemB->getRules()->isTwoHanded())
		{
			if (_drawingRoutine == 10)
			{
				itemB->setX(offX4[unitDir]);
				itemB->setY(offY4[unitDir]);
			}
			else
			{
				itemB->setX(offX2[unitDir]);
				itemB->setY(offY2[unitDir]);
			}
		}
		else
		{
			itemB->setX(0);
			itemB->setY(0);
			rightArm = _unitSurface->getFrame(rarm2H + unitDir);
		}

		if (_unit->getStatus() == STATUS_AIMING && _itemB->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + dir);
			if (_drawingRoutine == 10)
			{
				itemB->setX(offX7[unitDir]);
				itemB->setY(offY7[unitDir]);
			}
			else
			{
				itemB->setX(offX6[unitDir]);
				itemB->setY(offY6[unitDir]);
			}
			rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
		}

		if (_unit->getStatus() == STATUS_WALKING)
		{
			itemB->setY(itemB->getY() + torsoHandsWeaponY);
            leftArm->setY(torsoHandsWeaponY);
			if (_itemB->getRules()->isTwoHanded())
				rightArm->setY(torsoHandsWeaponY);
		}
	}
	// offset everything but legs when kneeled
	if (_unit->isKneeled())
	{
		leftArm->setY(offYKneel);
		rightArm->setY(offYKneel);
		torso->setY(offYKneel);
		itemA?itemA->setY(itemA->getY() + offYKneel):void();
		itemB?itemB->setY(itemB->getY() + offYKneel):void();
	}
	else if (_unit->getStatus() != STATUS_WALKING)
	{
		leftArm->setY(0);
		rightArm->setY(0);
		torso->setY(0);
	}

	// items are calculated for soldier height (22) - some aliens are smaller, so item is drawn lower.
	if (itemA)
	{
		itemA->setY(itemA->getY() + (22 - _unit->getStandHeight()));
	}
	if (itemB)
	{
		itemB->setY(itemB->getY() + (22 - _unit->getStandHeight()));
	}

	if (_unit->getStatus() == STATUS_AIMING)
	{
		torso->setX(offXAiming);
		legs->setX(offXAiming);
		leftArm->setX(offXAiming);
		rightArm->setX(offXAiming);
		if (itemA)
			itemA->setX(itemA->getX() + offXAiming);
		if (itemB)
			itemB->setX(itemB->getX() + offXAiming);
	}
	else if (!itemA && _drawingRoutine == 10 && _unit->getStatus() == STATUS_WALKING && unitDir == 2)
	{
		rightArm->setX(-6);
	}

	// blit order depends on unit direction, and whether we are holding a 2 handed weapon.
	switch (unitDir)
	{
	case 0: blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 1: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitItem(itemB); blitBody(torso, BODYPART_TORSO); blitItem(itemA); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 2: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitItem(itemB); blitItem(itemA); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 3:
		if (_unit->getStatus() != STATUS_AIMING  && ((_itemA && _itemA->getRules()->isTwoHanded()) || (_itemB && _itemB->getRules()->isTwoHanded())))
		{
			blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB); blitBody(rightArm, BODYPART_RIGHTARM);
		}
		else
		{
			blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB);
		}
		break;
	case 4:	blitBody(legs, BODYPART_LEGS); blitBody(rightArm, BODYPART_RIGHTARM); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB);	break;
	case 5:
		if (_unit->getStatus() != STATUS_AIMING  && ((_itemA && _itemA->getRules()->isTwoHanded()) || (_itemB && _itemB->getRules()->isTwoHanded())))
		{
			blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB);
		}
		else
		{
			blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitItem(itemA); blitItem(itemB); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM);
		}
		break;
	case 6: blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 7:
		if (_unit->getStatus() != STATUS_AIMING  && ((_itemA && _itemA->getRules()->isTwoHanded()) || (_itemB && _itemB->getRules()->isTwoHanded())))
		{
			blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO);
		}
		else
		{
			blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO);
		}
		break;
	}
	torso->setX(0);
	legs->setX(0);
	leftArm->setX(0);
	rightArm->setX(0);
	if (itemA)
		itemA->setX(0);
	if (itemB)
		itemB->setX(0);
}


/**
 * Drawing routine for floaters.
 */
void UnitSprite::drawRoutine1()
{

	Surface *torso = 0, *leftArm = 0, *rightArm = 0, *itemA = 0, *itemB = 0;
	// magic numbers
	const int stand = 16, walk = 24, die = 64;
	const int larm = 8, rarm = 0, larm2H = 67, rarm2H = 75, rarmShoot = 83, rarm1H= 91; // note that arms are switched vs "normal" sheets
	const int yoffWalk[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // bobbing up and down
	const int offX[8] = { 8, 10, 7, 4, -9, -11, -7, -3 }; // for the weapons
	const int offY[8] = { -6, -3, 0, 2, 0, -4, -7, -9 }; // for the weapons
	const int offX2[8] = { -8, 3, 7, 13, 6, -3, -5, -13 }; // for the weapons
	const int offY2[8] = { 1, -4, -1, 0, 3, 3, 5, 0 }; // for the weapons
	const int offX3[8] = { 0, 6, 6, 12, -4, -5, -5, -13 }; // for the left handed rifles
	const int offY3[8] = { -4, -4, -1, 0, 5, 0, 1, 0 }; // for the left handed rifles
	const int offXAiming = 16;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		torso = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(torso, BODYPART_COLLAPSING);
		return;
	}

	const int unitDir = _unit->getDirection();
	const int walkPhase = _unit->getWalkingPhase();

	leftArm = _unitSurface->getFrame(larm + unitDir);
	rightArm = _unitSurface->getFrame(rarm + unitDir);
	// when walking, torso(fixed sprite) has to be animated up/down
	if (_unit->getStatus() == STATUS_WALKING)
	{
		torso = _unitSurface->getFrame(walk + (5 * unitDir) + (walkPhase / 1.6)); // floater only has 5 walk animations instead of 8
		torso->setY(yoffWalk[walkPhase]);
	}
	else
	{
		torso = _unitSurface->getFrame(stand + unitDir);
	}

	sortRifles();

	// holding an item
	if (_itemA)
	{
		// draw handob item
		if (_unit->getStatus() == STATUS_AIMING && _itemA->getRules()->isTwoHanded())
		{
			int dir = (_unit->getDirection() + 2)%8;
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + dir);
			itemA->setX(offX[unitDir]);
			itemA->setY(offY[unitDir]);
		}
		else
		{
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + unitDir);
			itemA->setX(0);
			itemA->setY(0);
		}
		// draw arms holding the item
		if (_itemA->getRules()->isTwoHanded())
		{
			leftArm = _unitSurface->getFrame(larm2H + unitDir);
			if (_unit->getStatus() == STATUS_AIMING)
			{
				rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
			}
			else
			{
				rightArm = _unitSurface->getFrame(rarm2H + unitDir);
			}
		}
		else
		{
			rightArm = _unitSurface->getFrame(rarm1H + unitDir);
		}
	}

	//if we are left handed or dual wielding...
	if (_itemB)
	{
		leftArm = _unitSurface->getFrame(larm2H + unitDir);
		itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + unitDir);
		if (!_itemB->getRules()->isTwoHanded())
		{
			itemB->setX(offX2[unitDir]);
			itemB->setY(offY2[unitDir]);
		}
		else
		{
			itemB->setX(0);
			itemB->setY(0);
			rightArm = _unitSurface->getFrame(rarm2H + unitDir);
		}

		if (_unit->getStatus() == STATUS_AIMING && _itemB->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + dir);
			itemB->setX(offX3[unitDir]);
			itemB->setY(offY3[unitDir]);
			rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
		}

		if (_unit->getStatus() == STATUS_WALKING)
		{
			leftArm->setY(yoffWalk[walkPhase]);
			itemB->setY(itemB->getY() + yoffWalk[walkPhase]);
			if (_itemB->getRules()->isTwoHanded())
				rightArm->setY(yoffWalk[walkPhase]);
		}
	}

	if (_unit->getStatus() != STATUS_WALKING)
	{
		leftArm->setY(0);
		rightArm->setY(0);
		torso->setY(0);
	}
	if (_unit->getStatus() == STATUS_AIMING)
	{
		torso->setX(offXAiming);
		leftArm->setX(offXAiming);
		rightArm->setX(offXAiming);
		if (itemA)
			itemA->setX(itemA->getX() + offXAiming);
		if (itemB)
			itemB->setX(itemB->getX() + offXAiming);
	}
	// blit order depends on unit direction.
	switch (unitDir)
	{
	case 0: blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 1: blitBody(leftArm, BODYPART_LEFTARM); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 2: blitBody(leftArm, BODYPART_LEFTARM); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 3:	blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 4:	blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 5:	blitBody(rightArm, BODYPART_RIGHTARM); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB); break;
	case 6: blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 7:	blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(torso, BODYPART_TORSO); break;
	}
	torso->setX(0);
	leftArm->setX(0);
	rightArm->setX(0);
	if (itemA)
		itemA->setX(0);
	if (itemB)
		itemB->setX(0);
}

/**
 * Drawing routine for XCom tanks.
 */
void UnitSprite::drawRoutine2()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	const int offX[8] = { -2, -7, -5, 0, 5, 7, 2, 0 }; // hovertank offsets
	const int offy[8] = { -1, -3, -4, -5, -4, -3, -1, -1 }; // hovertank offsets

	Surface *s = 0;

	const int hoverTank = _unit->getMovementType() == MT_FLY ? 32 : 0;
	const int turret = _unit->getTurretType();

	// draw the animated propulsion below the hwp
	if (_part > 0 && hoverTank != 0)
	{
		s = _unitSurface->getFrame(104 + ((_part-1) * 8) + _animationFrame % 8);
		blitBody(s, 0);
	}

	// draw the tank itself
	s = _unitSurface->getFrame(hoverTank + (_part * 8) + _unit->getDirection());
	blitBody(s, 0);

	// draw the turret, together with the last part
	if (_part == 3 && turret != -1)
	{
		s = _unitSurface->getFrame(64 + (turret * 8) + _unit->getTurretDirection());
		int turretOffsetX = 0;
		int turretOffsetY = -4;
		if (hoverTank)
		{
			turretOffsetX += offX[_unit->getDirection()];
			turretOffsetY += offy[_unit->getDirection()];
		}
		s->setX(turretOffsetX);
		s->setY(turretOffsetY);
		blitBody(s, 0);
	}

}

/**
 * Drawing routine for cyberdiscs.
 */
void UnitSprite::drawRoutine3()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0;

	// draw the animated propulsion below the hwp
	if (_part > 0)
	{
		s = _unitSurface->getFrame(32 + ((_part-1) * 8) + _animationFrame % 8);
		blitBody(s, 0);
	}

	s = _unitSurface->getFrame((_part * 8) + _unit->getDirection());

	blitBody(s, 0);
}

/**
 * Drawing routine for civilians, ethereals, zombies (routine 4),
 * tftd civilians, tftd zombies (routine 17), more tftd civilians (routine 18).
 * Very easy: first 8 is standing positions, then 8 walking sequences of 8, finally death sequence of 3
 */
void UnitSprite::drawRoutine4()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0, *itemA = 0, *itemB = 0;
	int stand = 0, walk = 8, die = 72;
	const int offX[8] = { 8, 10, 7, 4, -9, -11, -7, -3 }; // for the weapons
	const int offY[8] = { -6, -3, 0, 2, 0, -4, -7, -9 }; // for the weapons
	const int offX2[8] = { -8, 3, 5, 12, 6, -1, -5, -13 }; // for the weapons
	const int offY2[8] = { 1, -4, -2, 0, 3, 3, 5, 0 }; // for the weapons
	const int offX3[8] = { 0, 6, 6, 12, -4, -5, -5, -13 }; // for the left handed rifles
	const int offY3[8] = { -4, -4, -1, 0, 5, 0, 1, 0 }; // for the left handed rifles
	const int standConvert[8] = { 3, 2, 1, 0, 7, 6, 5, 4 }; // array for converting stand frames for some tftd civilians
	const int offXAiming = 16;

	if (_drawingRoutine == 17) // tftd civilian - first set
	{
		stand = 64;
		walk = 0;
	}
	else if (_drawingRoutine == 18) // tftd civilian - second set
	{
		stand = 140;
		walk = 76;
		die = 148;
	}

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	const int unitDir = _unit->getDirection();

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		s = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(s, BODYPART_COLLAPSING);
		return;
	}
	else if (_unit->getStatus() == STATUS_WALKING)
	{
		s = _unitSurface->getFrame(walk + (8 * unitDir) + _unit->getWalkingPhase());
	}
	else if (_drawingRoutine != 17)
	{
		s = _unitSurface->getFrame(stand + unitDir);
	}
	else
	{
		s = _unitSurface->getFrame(stand + standConvert[unitDir]);
	}

	sortRifles();

	if (_itemA && !_itemA->getRules()->isFixed())
	{
		// draw handob item
		if (_unit->getStatus() == STATUS_AIMING && _itemA->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + dir);
			itemA->setX(offX[unitDir]);
			itemA->setY(offY[unitDir]);
		}
		else
		{
			if (_itemA->getSlot()->getId() == "STR_RIGHT_HAND")
			{
				itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + unitDir);
				itemA->setX(0);
				itemA->setY(0);
			}
			else
			{
				itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + unitDir);
				itemA->setX(offX2[unitDir]);
				itemA->setY(offY2[unitDir]);
			}
		}
	}

	//if we are dual wielding...
	if (_itemB && !_itemB->getRules()->isFixed())
	{
		itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + unitDir);
		if (!_itemB->getRules()->isTwoHanded())
		{
			itemB->setX(offX2[unitDir]);
			itemB->setY(offY2[unitDir]);
		}
		else
		{
			itemB->setX(0);
			itemB->setY(0);
		}

		if (_unit->getStatus() == STATUS_AIMING && _itemB->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + dir);
			itemB->setX(offX3[unitDir]);
			itemB->setY(offY3[unitDir]);
		}
	}

	if (_unit->getStatus() == STATUS_AIMING)
	{
		s->setX(offXAiming);
		if (itemA)
			itemA->setX(itemA->getX() + offXAiming);
		if (itemB)
			itemB->setX(itemB->getX() + offXAiming);
	}
	switch (unitDir)
	{
	case 0: blitItem(itemB); blitItem(itemA); blitBody(s, BODYPART_TORSO); break;
	case 1: blitItem(itemB); blitBody(s, BODYPART_TORSO); blitItem(itemA); break;
	case 2: blitBody(s, BODYPART_TORSO); blitItem(itemB); blitItem(itemA); break;
	case 3: blitBody(s, BODYPART_TORSO); blitItem(itemA); blitItem(itemB); break;
	case 4: blitBody(s, BODYPART_TORSO); blitItem(itemA); blitItem(itemB); break;
	case 5: blitItem(itemA); blitBody(s, BODYPART_TORSO); blitItem(itemB); break;
	case 6: blitItem(itemA); blitBody(s, BODYPART_TORSO); blitItem(itemB); break;
	case 7: blitItem(itemA); blitItem(itemB); blitBody(s, BODYPART_TORSO); break;
	}
	s->setX(0);
	if (itemA)
		itemA->setX(0);
	if (itemB)
		itemB->setX(0);
}

/**
 * Drawing routine for sectopods and reapers.
 */
void UnitSprite::drawRoutine5()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0;

	if (_unit->getStatus() == STATUS_WALKING)
	{
		s = _unitSurface->getFrame( 32 + (_unit->getDirection() * 16) + (_part * 4) + ((_unit->getWalkingPhase() / 2) % 4));
	}
	else
	{
		s = _unitSurface->getFrame((_part * 8) + _unit->getDirection());
	}

	blitBody(s, 0);
}

/**
 * Drawing routine for snakemen.
 */
void UnitSprite::drawRoutine6()
{
	Surface *torso = 0, *legs = 0, *leftArm = 0, *rightArm = 0, *itemA = 0, *itemB = 0;
	// magic numbers
	const int Torso = 24, legsStand = 16, die = 96;
	const int larmStand = 0, rarmStand = 8, rarm1H = 99, larm2H = 107, rarm2H = 115, rarmShoot = 123;
	const int legsWalk[8] = { 32, 40, 48, 56, 64, 72, 80, 88 };
	const int yoffWalk[8] = {3, 3, 2, 1, 0, 0, 1, 2}; // bobbing up and down
	const int xoffWalka[8] = {0, 0, 1, 2, 3, 3, 2, 1};
	const int xoffWalkb[8] = {0, 0, -1, -2, -3, -3, -2, -1};
	const int yoffStand[8] = {2, 1, 1, 0, 0, 0, 0, 0};
	const int offX[8] = { 8, 10, 5, 2, -8, -10, -5, -2 }; // for the weapons
	const int offY[8] = { -6, -3, 0, 0, 2, -3, -7, -9 }; // for the weapons
	const int offX2[8] = { -8, 2, 7, 13, 7, 0, -3, -15 }; // for the weapons
	const int offY2[8] = { 1, -4, -2, 0, 3, 3, 5, 0 }; // for the weapons
	const int offX3[8] = { 0, 6, 6, 12, -4, -5, -5, -13 }; // for the left handed rifles
	const int offY3[8] = { -4, -4, -1, 0, 5, 0, 1, 0 }; // for the left handed rifles
	const int offXAiming = 16;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		torso = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(torso, BODYPART_COLLAPSING);
		return;
	}

	const int unitDir = _unit->getDirection();
	const int walkPhase = _unit->getWalkingPhase();

	torso = _unitSurface->getFrame(Torso + unitDir);
	leftArm = _unitSurface->getFrame(larmStand + unitDir);
	rightArm = _unitSurface->getFrame(rarmStand + unitDir);


	// when walking, torso(fixed sprite) has to be animated up/down
	if (_unit->getStatus() == STATUS_WALKING)
	{
		int xoffWalk = 0;
		if (unitDir < 3)
			xoffWalk = xoffWalka[walkPhase];
		if (unitDir < 7 && unitDir > 3)
			xoffWalk = xoffWalkb[walkPhase];
		torso->setY(yoffWalk[walkPhase]);
		torso->setX(xoffWalk);
		legs = _unitSurface->getFrame(legsWalk[unitDir] + walkPhase);
		rightArm->setY(yoffWalk[walkPhase]);
		leftArm->setY(yoffWalk[walkPhase]);
		rightArm->setX(xoffWalk);
		leftArm->setX(xoffWalk);
	}
	else
	{
		legs = _unitSurface->getFrame(legsStand + unitDir);
	}

	sortRifles();

	// holding an item
	if (_itemA)
	{
		// draw handob item
		if (_unit->getStatus() == STATUS_AIMING && _itemA->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + dir);
			itemA->setX(offX[unitDir]);
			itemA->setY(offY[unitDir]);
		}
		else
		{
			itemA = _itemSurfaceA->getFrame(_itemA->getRules()->getHandSprite() + unitDir);
			itemA->setX(0);
			itemA->setY(0);
			if (!_itemA->getRules()->isTwoHanded())
			{
				itemA->setY(yoffStand[unitDir]);
			}
		}


		// draw arms holding the item
		if (_itemA->getRules()->isTwoHanded())
		{
			leftArm = _unitSurface->getFrame(larm2H + unitDir);
			if (_unit->getStatus() == STATUS_AIMING)
			{
				rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
			}
			else
			{
				rightArm = _unitSurface->getFrame(rarm2H + unitDir);
			}
		}
		else
		{
			rightArm = _unitSurface->getFrame(rarm1H + unitDir);
		}


		// the fixed arm(s) have to be animated up/down when walking
		if (_unit->getStatus() == STATUS_WALKING)
		{
			itemA->setY(yoffWalk[walkPhase]);
			rightArm->setY(yoffWalk[walkPhase]);
			if (_itemA->getRules()->isTwoHanded())
				leftArm->setY(yoffWalk[walkPhase]);
		}
	}
	//if we are left handed or dual wielding...
	if (_itemB)
	{
		leftArm = _unitSurface->getFrame(larm2H + unitDir);
		itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + unitDir);
		if (!_itemB->getRules()->isTwoHanded())
		{
			itemB->setX(offX2[unitDir]);
			itemB->setY(offY2[unitDir]);
		}
		else
		{
			itemB->setX(0);
			itemB->setY(0);
			if (!_itemB->getRules()->isTwoHanded())
			{
				itemB->setY(yoffStand[unitDir]);
			}
			rightArm = _unitSurface->getFrame(rarm2H + unitDir);
		}

		if (_unit->getStatus() == STATUS_AIMING && _itemB->getRules()->isTwoHanded())
		{
			int dir = (unitDir + 2)%8;
			itemB = _itemSurfaceB->getFrame(_itemB->getRules()->getHandSprite() + dir);
			itemB->setX(offX3[unitDir]);
			itemB->setY(offY3[unitDir]);
			rightArm = _unitSurface->getFrame(rarmShoot + unitDir);
		}

		if (_unit->getStatus() == STATUS_WALKING)
		{
			leftArm->setY(yoffWalk[walkPhase]);
			itemB->setY(offY2[unitDir] + yoffWalk[walkPhase]);
			if (_itemB->getRules()->isTwoHanded())
				rightArm->setY(yoffWalk[walkPhase]);
		}
	}
	// offset everything but legs when kneeled
	if (_unit->getStatus() != STATUS_WALKING)
	{
		leftArm->setY(0);
		rightArm->setY(0);
		torso->setY(0);
	}
	if (_unit->getStatus() == STATUS_AIMING)
	{
		torso->setX(offXAiming);
		legs->setX(offXAiming);
		leftArm->setX(offXAiming);
		rightArm->setX(offXAiming);
		if (itemA)
			itemA->setX(itemA->getX() + offXAiming);
		if (itemB)
			itemB->setX(itemB->getX() + offXAiming);
	}

	// blit order depends on unit direction.
	switch (unitDir)
	{
	case 0: blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 1: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitItem(itemB); blitBody(torso, BODYPART_TORSO); blitItem(itemA); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 2: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 3: blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitItem(itemA); blitItem(itemB); break;
	case 4:	blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB); break;
	case 5:	blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitItem(itemA); blitItem(itemB); break;
	case 6: blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitItem(itemA); blitItem(itemB); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 7:	blitItem(itemA); blitItem(itemB); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); break;
	}
	torso->setX(0);
	legs->setX(0);
	leftArm->setX(0);
	rightArm->setX(0);
	if (itemA)
		itemA->setX(itemA->getX() + 0);
	if (itemB)
		itemB->setX(itemB->getX() + 0);
}

/**
 * Drawing routine for chryssalid.
 */
void UnitSprite::drawRoutine7()
{

	Surface *torso = 0, *legs = 0, *leftArm = 0, *rightArm = 0;
	// magic numbers
	const int Torso = 24, legsStand = 16, die = 224;
	const int larmStand = 0, rarmStand = 8;
	const int legsWalk[8] = { 48, 48+24, 48+24*2, 48+24*3, 48+24*4, 48+24*5, 48+24*6, 48+24*7 };
	const int larmWalk[8] = { 32, 32+24, 32+24*2, 32+24*3, 32+24*4, 32+24*5, 32+24*6, 32+24*7 };
	const int rarmWalk[8] = { 40, 40+24, 40+24*2, 40+24*3, 40+24*4, 40+24*5, 40+24*6, 40+24*7 };
	const int yoffWalk[8] = {1, 0, -1, 0, 1, 0, -1, 0}; // bobbing up and down

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		torso = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(torso, BODYPART_COLLAPSING);
		return;
	}

	const int unitDir = _unit->getDirection();
	const int walkPhase = _unit->getWalkingPhase();

	torso = _unitSurface->getFrame(Torso + unitDir);


	// when walking, torso(fixed sprite) has to be animated up/down
	if (_unit->getStatus() == STATUS_WALKING)
	{
		torso->setY(yoffWalk[walkPhase]);
		legs = _unitSurface->getFrame(legsWalk[unitDir] + walkPhase);
		leftArm = _unitSurface->getFrame(larmWalk[unitDir] + walkPhase);
		rightArm = _unitSurface->getFrame(rarmWalk[unitDir] + walkPhase);
	}
	else
	{

		legs = _unitSurface->getFrame(legsStand + unitDir);
		leftArm = _unitSurface->getFrame(larmStand + unitDir);
		rightArm = _unitSurface->getFrame(rarmStand + unitDir);
		leftArm->setY(0);
		rightArm->setY(0);
		torso->setY(0);
	}

	// blit order depends on unit direction
	switch (unitDir)
	{
	case 0: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 1: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 2: blitBody(leftArm, BODYPART_LEFTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 3: blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); break;
	case 4: blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 5: blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 6: blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); blitBody(leftArm, BODYPART_LEFTARM); break;
	case 7: blitBody(leftArm, BODYPART_LEFTARM); blitBody(rightArm, BODYPART_RIGHTARM); blitBody(legs, BODYPART_LEGS); blitBody(torso, BODYPART_TORSO); break;
	}
}

/**
 * Drawing routine for silacoids.
 */
void UnitSprite::drawRoutine8()
{
	Surface *legs = 0;
	// magic numbers
	const int Body = 0, aim = 5, die = 6;
	const int Pulsate[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	legs = _unitSurface->getFrame(Body + Pulsate[_animationFrame % 8]);

	if (_unit->getStatus() == STATUS_COLLAPSING)
		legs = _unitSurface->getFrame(die + _unit->getFallingPhase());

	if (_unit->getStatus() == STATUS_AIMING)
		legs = _unitSurface->getFrame(aim);

	blitBody(legs, 0);
}

/**
 * Drawing routine for celatids.
 */
void UnitSprite::drawRoutine9()
{
	Surface *torso = 0;
	// magic numbers
	const int Body = 0, die = 25;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	torso = _unitSurface->getFrame(Body + _animationFrame % 8);

	if (_unit->getStatus() == STATUS_COLLAPSING)
		torso = _unitSurface->getFrame(die + _unit->getFallingPhase());

	blitBody(torso, 0);
}

/**
* Drawing routine for tftd tanks.
*/
void UnitSprite::drawRoutine11()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	const int offTurretX[8] = { -2, -6, -5, 0, 5, 6, 2, 0 }; // turret offsets
	const int offTurretYAbove[8] = { 5, 3, 0, 0, 0, 3, 5, 4 }; // turret offsets
	const int offTurretYBelow[8] = { -12, -13, -16, -16, -16, -13, -12, -12 }; // turret offsets

	int body = 0;
	int animFrame = _unit->getWalkingPhase() % 4;
	if (_unit->getMovementType() == MT_FLY)
	{
		body = 128;
		animFrame = _animationFrame % 4;
	}

	Surface *s = _unitSurface->getFrame(body + (_part * 4) + 16 * _unit->getDirection() + animFrame);
	s->setY(4);
	blitBody(s, 0);

	int turret = _unit->getTurretType();
	// draw the turret, overlapping all 4 parts
	if ((_part == 3 || _part == 0) && turret != -1 && !_unit->getFloorAbove())
	{
		s = _unitSurface->getFrame(256 + (turret * 8) + _unit->getTurretDirection());
		s->setX(offTurretX[_unit->getDirection()]);
		if (_part == 3)
			s->setY(offTurretYBelow[_unit->getDirection()]);
		else
			s->setY(offTurretYAbove[_unit->getDirection()]);
		blitBody(s, 0);
	}

}

/**
* Drawing routine for hallucinoids (routine 12) and biodrones (routine 16).
*/
void UnitSprite::drawRoutine12()
{
	const int die = 8;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0;
	s = _unitSurface->getFrame((_part * 8) + _animationFrame);

	if ( (_unit->getStatus() == STATUS_COLLAPSING) && (_drawingRoutine == 16) )
	{
		// biodrone death frames
		s = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(s, 0);
		return;
	}

	blitBody(s, 0);
}

/**
 * Drawing routine for tentaculats.
 */
void UnitSprite::drawRoutine19()
{
	Surface *s = 0;
	// magic numbers
	const int stand = 0, move = 8, die = 16;

	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		s = _unitSurface->getFrame(die + _unit->getFallingPhase());
		blitBody(s, 0);
		return;
	}

	if (_unit->getStatus() == STATUS_WALKING)
	{
		s = _unitSurface->getFrame(move + _unit->getDirection());
	}
	else
	{
		s = _unitSurface->getFrame(stand + _unit->getDirection());
	}

	blitBody(s, 0);
}

/**
 * Drawing routine for triscenes.
 */
void UnitSprite::drawRoutine20()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0;

	if (_unit->getStatus() == STATUS_WALKING)
	{
		s = _unitSurface->getFrame((_unit->getWalkingPhase()/2%4) + 5 * (_part + 4 * _unit->getDirection()));
	}
	else
	{
		s = _unitSurface->getFrame(5 * (_part + 4 * _unit->getDirection()));
	}

	blitBody(s, 0);
}

/**
 * Drawing routine for xarquids.
 */
void UnitSprite::drawRoutine21()
{
	if (_unit->isOut())
	{
		// unit is drawn as an item
		return;
	}

	Surface *s = 0;

	s = _unitSurface->getFrame((_part * 4) + (_unit->getDirection() * 16) + (_animationFrame % 4));

	blitBody(s, 0);
}

/**
 * Determines which weapons to display in the case of two-handed weapons.
 */
void UnitSprite::sortRifles()
{
	if (_itemA && _itemA->getRules()->isTwoHanded())
	{
		if (_itemB && _itemB->getRules()->isTwoHanded())
		{
			if (_unit->getActiveHand() == "STR_LEFT_HAND")
			{
				_itemA = _itemB;
			}
			_itemB = 0;
		}
		else if (_unit->getStatus() != STATUS_AIMING)
		{
			_itemB = 0;
		}
	}
	else if (_itemB && _itemB->getRules()->isTwoHanded())
	{
		if (_unit->getStatus() != STATUS_AIMING)
		{
			_itemA = 0;
		}
	}
}

} //namespace OpenXcom
