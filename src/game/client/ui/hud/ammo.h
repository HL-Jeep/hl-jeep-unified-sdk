/***
 *
 *	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
 *
 *	This product contains software technology licensed from Id
 *	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
 *	All Rights Reserved.
 *
 *   Use, distribution, and modification of this source code and/or resulting
 *   object code is restricted to non-commercial enhancements to products from
 *   Valve LLC.  All other use, distribution, or modification is prohibited
 *   without written permission from Valve LLC.
 *
 ****/

#pragma once

#include <array>

#include "cdll_dll.h"

struct AmmoType;
struct WeaponInfo;

struct WEAPON
{
	const WeaponInfo* Info{};
	std::array<const AmmoType*, MAX_WEAPON_ATTACK_MODES> AmmoTypes{};

	int AmmoInMagazine{0};

	int iCount{0}; // # of itesm in plist

	HLSPRITE hActive{0};
	Rect rcActive;
	HLSPRITE hInactive{0};
	Rect rcInactive;
	HLSPRITE hAmmo{0};
	Rect rcAmmo;
	HLSPRITE hAmmo2{0};
	Rect rcAmmo2;
	HLSPRITE hCrosshair{0};
	Rect rcCrosshair;
	HLSPRITE hAutoaim{0};
	Rect rcAutoaim;
	HLSPRITE hZoomedCrosshair{0};
	Rect rcZoomedCrosshair;
	HLSPRITE hZoomedAutoaim{0};
	Rect rcZoomedAutoaim;
};
