#pragma once

// Headlights, level 1 (PS2 illusion): light cone + ground pool.
// No new coronas are registered; the game's own headlight coronas stay
// as they are and coronaBoost only scales cone + pool brightness.
// No bundled option set here: every value is read individually
// from skygfx_headlights.ini and clamped to a hard limit in code.

#include "skygfx.h"

struct HeadlightsConfig
{
	int enabled;		// master switch, 0 = off, 1 = on
	float intensity;	// light power, 0.0 - 2.0
	float range;		// beam distance in metres, 5 - 40
	float coneWidth;	// cone width, 0.5 - 2.0
	float coneOpacity;	// cone density/opacity, 0.0 - 1.0
	float poolIntensity;	// ground pool intensity, 0.0 - 1.5
	float poolLength;	// ground pool length, relative to range, 0.5 - 1.5
	float coronaBoost;	// corona size/brightness multiplier, 0.5 - 2.0
	int maxCars;		// simultaneous cars, 1 - 8 (player first)
	int lightColorR;	// 0 - 255
	int lightColorG;	// 0 - 255
	int lightColorB;	// 0 - 255
	int onlyAtNight;	// 0 = also in daylight, 1 = night only
	int rainFade;		// 0 = ignore rain, 1 = fade with rain
	float fadeStart;	// where the cone starts fading, 0.0 - 0.9
};

extern HeadlightsConfig gHeadlightsConfig;

// (Re)read skygfx_headlights.ini next to skygfx.asi.
// If the file does not exist, the effect stays off.
void Headlights_ReadIni(void);

// Store one ground light pool per car. Call before the scene is
// rendered (RenderScene_before), because shadows are queued first
// and rendered later in the same frame.
void Headlights_StoreGroundPools(void);

// Draw the light cones. Call after the world is rendered
// (RenderScene_after), with additive blending.
void Headlights_RenderCones(void);
