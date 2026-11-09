// Headlights, level 1 (PS2 illusion) for GTA San Andreas.
//
// What it does: for the player's car and the nearest cars with their
// lights on, at night, it draws
//   1. a ground light pool, using the game's own shadow system
//      (CShadows::StoreShadowToBeRendered with the stock headlight
//      texture, exactly like the rear-light projection in the
//      IIIStreakLights mod), and
//   2. a light cone made of two crossed translucent quads per
//      headlight, drawn with RwIm3D in additive blending.
//
// What it does NOT do: no pipeline change, no new coronas (the
// game's own headlight coronas stay as they are; coronaBoost only
// scales cone + pool brightness), no per-pixel lighting, no
// deferred lighting. That keeps the cost near zero: at most 8 cars,
// 4 quads per car, 32 RwIm3D calls per frame, and one shadow per car.
//
// How vehicles are found (verified against plugin-sdk, whose headers
// are NOT included here to avoid clashing with SkyGfx's own CVector
// / CPool definitions; everything is read by byte offset):
//   - vehicle pool pointer:  CPools::ms_pVehiclePool at 0xB74494.
//     The pool's slots are CHeli-sized: 0xA18 bytes per slot.
//     That stride is load-bearing: CVehicle alone is only 0x5A0,
//     using it would land on the wrong vehicle for every slot
//     after the first one.
//   - matrix pointer: CPlaceable::m_matrix at offset 0x14.
//     RwMatrix: right at 0x0, forward ("up") at 0x10, world-up
//     ("at") at 0x20, position at 0x30.
//   - model index: CEntity::m_nModelIndex, short at offset 0x22.
//   - lights on: CVehicle bitfield byte at offset 0x428, bit 6
//     (0x40). Derived, not guessed: CVehicle::m_autoPilot is at
//     0x390 and CAutoPilot is 0x98 bytes, so the flag bitfields
//     start at 0x428; the first byte is lawEnforcer, ambulance,
//     fireTruck, locked, engineOn, handbrakeOn, lightsOn, freebies.
//     This offset is derived from validated plugin-sdk offsets, but
//     it has NOT been verified at runtime yet. If the cones show up
//     on cars with their lights off (or never show up at all),
//     this byte is the first thing to check.
//   - headlight position: fallback only, pos + forward*2.0 +
//     right*(+/-0.8) + up*0.5, matching IIIStreakLights' fallback.
//     TODO (phase 2): read the real dummy from the model,
//     CVehicleModelInfo::m_pVehicleStruct at 0x5C,
//     m_avDummyPos[0] (DUMMY_HEADLIGHTS).
//   - player vehicle: FindPlayerVehicle at 0x56E0D0.
//   - camera: TheCamera.GetPosition() (already in gta.h).
//   - night: CClock hours byte at 0xB70153 (already in gta.h).
//   - rain: CWeather__Rain at 0xC81324 (already in gta.h).
//   - ground pool texture: gpShadowHeadLightsTex at 0xC403F8.
//   - CShadows::StoreShadowToBeRendered (long form) at 0x707390.
//   - cone rendering: RwIm3DTransform / RwIm3DRenderIndexedPrimitive
//     / RwIm3DEnd, as wrapped in main.cpp (0x7EF450/0x7EF550/0x7EF520).

#include "skygfx.h"
#include "headlights.h"
#include "ini_parser.hpp"

HeadlightsConfig gHeadlightsConfig;

// ------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------

static float
HL_Clamp(float v, float lo, float hi)
{
	if(v < lo) return lo;
	if(v > hi) return hi;
	return v;
}

static int
HL_ClampInt(int v, int lo, int hi)
{
	if(v < lo) return lo;
	if(v > hi) return hi;
	return v;
}

static int
HL_ReadInt(linb::ini &cfg, const char *key, int def)
{
	try{
		return std::stoi(cfg.get("Headlights", key, ""));
	}catch(...){
		return def;
	}
}

static float
HL_ReadFloat(linb::ini &cfg, const char *key, float def)
{
	try{
		return std::stof(cfg.get("Headlights", key, ""));
	}catch(...){
		return def;
	}
}

// ------------------------------------------------------------------
// .ini
// ------------------------------------------------------------------

void
Headlights_ReadIni(void)
{
	// Defaults are the values shipped in skygfx_headlights.ini.
	// If the file is missing the effect stays off (enabled = 0).
	gHeadlightsConfig.enabled = 1;
	gHeadlightsConfig.intensity = 1.0f;
	gHeadlightsConfig.range = 28.0f;
	gHeadlightsConfig.coneWidth = 1.0f;
	gHeadlightsConfig.coneOpacity = 0.35f;
	gHeadlightsConfig.poolIntensity = 0.8f;
	gHeadlightsConfig.poolLength = 1.0f;
	gHeadlightsConfig.coronaBoost = 1.25f;
	gHeadlightsConfig.maxCars = 4;
	gHeadlightsConfig.lightColorR = 255;
	gHeadlightsConfig.lightColorG = 240;
	gHeadlightsConfig.lightColorB = 200;
	gHeadlightsConfig.onlyAtNight = 1;
	gHeadlightsConfig.rainFade = 1;
	gHeadlightsConfig.fadeStart = 0.6f;

	char *path = getpath("skygfx_headlights.ini");
	if(!path){
		gHeadlightsConfig.enabled = 0;
		return;
	}

	linb::ini cfg;
	if(!cfg.load_file(path)){
		gHeadlightsConfig.enabled = 0;
		return;
	}

	HeadlightsConfig &c = gHeadlightsConfig;
	c.enabled       = HL_ClampInt(HL_ReadInt(cfg, "enabled", 1), 0, 1);
	c.intensity     = HL_Clamp(HL_ReadFloat(cfg, "intensity", 1.0f), 0.0f, 2.0f);
	c.range         = HL_Clamp(HL_ReadFloat(cfg, "range", 28.0f), 5.0f, 40.0f);
	c.coneWidth     = HL_Clamp(HL_ReadFloat(cfg, "coneWidth", 1.0f), 0.5f, 2.0f);
	c.coneOpacity   = HL_Clamp(HL_ReadFloat(cfg, "coneOpacity", 0.35f), 0.0f, 1.0f);
	c.poolIntensity = HL_Clamp(HL_ReadFloat(cfg, "poolIntensity", 0.8f), 0.0f, 1.5f);
	c.poolLength    = HL_Clamp(HL_ReadFloat(cfg, "poolLength", 1.0f), 0.5f, 1.5f);
	c.coronaBoost   = HL_Clamp(HL_ReadFloat(cfg, "coronaBoost", 1.25f), 0.5f, 2.0f);
	c.maxCars       = HL_ClampInt(HL_ReadInt(cfg, "maxCars", 4), 1, 8);
	c.lightColorR   = HL_ClampInt(HL_ReadInt(cfg, "lightColorR", 255), 0, 255);
	c.lightColorG   = HL_ClampInt(HL_ReadInt(cfg, "lightColorG", 240), 0, 255);
	c.lightColorB   = HL_ClampInt(HL_ReadInt(cfg, "lightColorB", 200), 0, 255);
	c.onlyAtNight   = HL_ClampInt(HL_ReadInt(cfg, "onlyAtNight", 1), 0, 1);
	c.rainFade      = HL_ClampInt(HL_ReadInt(cfg, "rainFade", 1), 0, 1);
	c.fadeStart     = HL_Clamp(HL_ReadFloat(cfg, "fadeStart", 0.6f), 0.0f, 0.9f);
}

// ------------------------------------------------------------------
// Vehicle access by byte offset (see the header comment at the top)
// ------------------------------------------------------------------

// Pool slots are CHeli-sized: 0xA18 bytes. See top of file.
struct HL_VehicleSlot { char data[0xA18]; };

static uint32_t HL_FindPlayerVehicle_A = 0x56E0D0;
WRAPPER void *HL_FindPlayerVehicle(int32 playerId, uint8 bIncludeRemote) { VARJMP(HL_FindPlayerVehicle_A); }

// Game function, long form: CShadows::StoreShadowToBeRendered at 0x707390.
typedef void (__cdecl *HL_StoreShadowFn)(uint8 type, RwTexture *texture,
	const CVector *posn, float topX, float topY, float rightX, float rightY,
	int16 intensity, uint8 red, uint8 green, uint8 blue,
	float zDistance, bool drawOnWater, float scale,
	void *realTimeShadow, bool drawOnBuildings);
static HL_StoreShadowFn HL_StoreShadow = (HL_StoreShadowFn)0x707390;

RwTexture *&gpShadowHeadLightsTex = *(RwTexture**)0xC403F8;

struct HL_Car
{
	CVector pos;	// vehicle origin
	CVector fwd;	// matrix up = forward
	CVector right;	// matrix right
	CVector up;	// matrix at = world up
	CVector lampL;	// left headlight world position
	CVector lampR;	// right headlight world position
	float distSq;	// distance squared to the camera
	bool isPlayer;
};

static bool
HL_IsNight(void)
{
	uint8 h = CClock__ms_nGameClockHours;
	return h >= 19 || h < 7;
}

// Combined brightness multiplier: intensity * coronaBoost, faded by
// rain when rainFade is on. coronaBoost is NOT a separate corona
// here (no new coronas are registered in v1); it scales the cone
// and the pool, which is what makes the headlights look stronger.
static float
HL_EffectiveIntensity(void)
{
	float f = gHeadlightsConfig.intensity * gHeadlightsConfig.coronaBoost;
	f = HL_Clamp(f, 0.0f, 3.0f);
	if(gHeadlightsConfig.rainFade){
		float rain = HL_Clamp(CWeather__Rain, 0.0f, 1.0f);
		f *= 1.0f - rain;
	}
	return f;
}

// Collect the cars that get the effect this frame, player first,
// then nearest to the camera, up to maxCars. Returns the count.
static int
HL_CollectCars(HL_Car *out, int maxOut)
{
	if(!gHeadlightsConfig.enabled)
		return 0;
	if(gHeadlightsConfig.onlyAtNight && !HL_IsNight())
		return 0;

	CPool<HL_VehicleSlot> *pool = *(CPool<HL_VehicleSlot>**)0xB74494;
	if(!pool)
		return 0;

	float range = gHeadlightsConfig.range;
	float rangeSq = range * range;
	CVector camPos = TheCamera.GetPosition();
	void *playerVeh = HL_FindPlayerVehicle(-1, 0);

	// Gather every candidate first (the pool is small: 110 slots),
	// then sort player-first / nearest-first. Static buffer, no
	// allocation per frame.
	static HL_Car candidates[128];
	int numCandidates = 0;
	int poolSize = pool->GetSize();
	if(poolSize > 128)
		poolSize = 128;

	for(int i = 0; i < poolSize; i++){
		HL_VehicleSlot *slot = pool->GetAtIndex(i);
		if(!slot)
			continue;
		char *base = (char*)slot;

		int16 model = *(int16*)(base + 0x22);
		if(model < 400 || model > 611)
			continue;

		// bLightsOn: byte 0x428, bit 6. See the header comment.
		if((*(uint8*)(base + 0x428) & 0x40) == 0)
			continue;

		CMatrix *m = *(CMatrix**)(base + 0x14);
		if(!m)
			continue;

		HL_Car &car = candidates[numCandidates];
		car.pos = *(CVector*)&m->matrix.pos;
		car.fwd = *(CVector*)&m->matrix.up;
		car.right = *(CVector*)&m->matrix.right;
		car.up = *(CVector*)&m->matrix.at;

		// Headlight fallback positions (phase 2: real model dummy).
		// lamp = pos + fwd*2.0 + right*(+/-0.8) + up*0.5
		car.lampL.x = car.pos.x + car.fwd.x*2.0f - car.right.x*0.8f + car.up.x*0.5f;
		car.lampL.y = car.pos.y + car.fwd.y*2.0f - car.right.y*0.8f + car.up.y*0.5f;
		car.lampL.z = car.pos.z + car.fwd.z*2.0f - car.right.z*0.8f + car.up.z*0.5f;
		car.lampR.x = car.pos.x + car.fwd.x*2.0f + car.right.x*0.8f + car.up.x*0.5f;
		car.lampR.y = car.pos.y + car.fwd.y*2.0f + car.right.y*0.8f + car.up.y*0.5f;
		car.lampR.z = car.pos.z + car.fwd.z*2.0f + car.right.z*0.8f + car.up.z*0.5f;

		float dx = car.pos.x - camPos.x;
		float dy = car.pos.y - camPos.y;
		float dz = car.pos.z - camPos.z;
		car.distSq = dx*dx + dy*dy + dz*dz;
		// range is also the performance cutoff: nothing beyond it.
		if(car.distSq > rangeSq)
			continue;

		car.isPlayer = (playerVeh && slot == playerVeh);
		numCandidates++;
	}

	// Player first, then nearest. Simple selection sort, at most
	// 128 entries, and only the first maxCars are used.
	int want = gHeadlightsConfig.maxCars;
	if(want > maxOut)
		want = maxOut;
	int n = numCandidates;
	if(n > want)
		n = want;
	for(int i = 0; i < n; i++){
		int best = i;
		for(int j = i + 1; j < numCandidates; j++){
			bool jFirst = candidates[j].isPlayer && !candidates[best].isPlayer;
			bool sameKind = candidates[j].isPlayer == candidates[best].isPlayer;
			if(jFirst || (sameKind && candidates[j].distSq < candidates[best].distSq))
				best = j;
		}
		if(best != i){
			HL_Car tmp = candidates[i];
			candidates[i] = candidates[best];
			candidates[best] = tmp;
		}
		out[i] = candidates[i];
	}
	return n;
}

// ------------------------------------------------------------------
// Ground pool (before the scene is rendered)
// ------------------------------------------------------------------

void
Headlights_StoreGroundPools(void)
{
	// One shadow per car max (maxCars <= 8, hard-clamped at load).
	HL_Car cars[8];
	int n = HL_CollectCars(cars, 8);
	if(n <= 0)
		return;

	RwTexture *tex = gpShadowHeadLightsTex;
	if(!tex)
		return;

	float eff = HL_EffectiveIntensity();
	if(eff <= 0.001f)
		return;

	// Stock headlight shadows use intensities around 40-128.
	int16 intensity = (int16)HL_ClampInt(
		(int)(gHeadlightsConfig.poolIntensity * eff * 100.0f), 0, 255);
	uint8 r = (uint8)gHeadlightsConfig.lightColorR;
	uint8 g = (uint8)gHeadlightsConfig.lightColorG;
	uint8 b = (uint8)gHeadlightsConfig.lightColorB;

	float halfLen = gHeadlightsConfig.range * 0.5f * gHeadlightsConfig.poolLength;
	float halfWidth = gHeadlightsConfig.coneWidth * 2.4f;

	for(int i = 0; i < n; i++){
		HL_Car &car = cars[i];

		// Centre of the pool: midway between the lamps, then
		// half the pool length ahead of the car.
		CVector centre;
		centre.x = (car.lampL.x + car.lampR.x)*0.5f + car.fwd.x*halfLen;
		centre.y = (car.lampL.y + car.lampR.y)*0.5f + car.fwd.y*halfLen;
		centre.z = (car.lampL.z + car.lampR.z)*0.5f + car.fwd.z*halfLen;

		HL_StoreShadow(2 /* SHADOW_ADDITIVE */, tex, &centre,
			car.fwd.x*halfLen, car.fwd.y*halfLen,
			car.right.x*halfWidth, car.right.y*halfWidth,
			intensity, r, g, b,
			6.0f, false, 1.0f, nil, true);
	}
}

// ------------------------------------------------------------------
// Light cone (after the world is rendered)
// ------------------------------------------------------------------

// One quad of the cone: three rings (near, fadeStart, far), two
// vertices per ring. Ring alphas: full, full, zero, so the beam
// holds its brightness up to fadeStart and fades out over the
// last (1 - fadeStart) of its range.
static void
HL_DrawConeQuad(const CVector &origin, const CVector &fwd,
	const CVector &axis, float range, float fadeStart,
	float spread, float widthScale,
	int r, int g, int b, int alpha)
{
	RwIm3DVertex verts[6];
	RwImVertexIndex indices[12] = { 0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5 };

	float ts[3] = { 0.0f, fadeStart, 1.0f };
	int alphas[3] = { alpha, alpha, 0 };

	for(int ring = 0; ring < 3; ring++){
		float t = ts[ring];
		float dist = range * t;
		// The beam widens as it goes: 0.30 m half-size at the lamp,
		// plus the spread over the distance.
		float half = (0.30f + dist * spread) * widthScale;
		CVector c;
		c.x = origin.x + fwd.x*dist;
		c.y = origin.y + fwd.y*dist;
		c.z = origin.z + fwd.z*dist;
		for(int side = 0; side < 2; side++){
			float s = side == 0 ? -half : half;
			RwIm3DVertex *v = &verts[ring*2 + side];
			RwIm3DVertexSetPos(v, c.x + axis.x*s, c.y + axis.y*s, c.z + axis.z*s);
			RwIm3DVertexSetNormal(v, 0.0f, 0.0f, 0.0f);
			RwIm3DVertexSetRGBA(v, r, g, b, alphas[ring]);
			RwIm3DVertexSetU(v, 0.0f);
			RwIm3DVertexSetV(v, 0.0f);
		}
	}

	if(RwIm3DTransform(verts, 6, nil, rwIM3D_VERTEXUV | rwIM3D_VERTEXRGBA)){
		RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, indices, 12);
		RwIm3DEnd();
	}
}

void
Headlights_RenderCones(void)
{
	HL_Car cars[8];
	int n = HL_CollectCars(cars, 8);
	if(n <= 0)
		return;

	float eff = HL_EffectiveIntensity();
	if(eff <= 0.001f)
		return;

	int alpha = (int)(gHeadlightsConfig.coneOpacity * 255.0f * eff);
	alpha = HL_ClampInt(alpha, 0, 255);
	if(alpha <= 0)
		return;

	int r = gHeadlightsConfig.lightColorR;
	int g = gHeadlightsConfig.lightColorG;
	int b = gHeadlightsConfig.lightColorB;
	float range = gHeadlightsConfig.range;
	float fadeStart = gHeadlightsConfig.fadeStart;
	float widthScale = gHeadlightsConfig.coneWidth;

	// Save the render states we touch.
	void *oldRaster, *oldSrcBlend, *oldDstBlend, *oldVertAlpha;
	void *oldZWrite, *oldZTest, *oldFog, *oldCull;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &oldRaster);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &oldSrcBlend);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &oldDstBlend);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &oldVertAlpha);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &oldZWrite);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &oldZTest);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &oldFog);
	RwRenderStateGet(rwRENDERSTATECULLMODE, &oldCull);

	// Same End/Begin pattern as RenderScene_before and the postfx
	// passes: re-begin the main camera so RwIm3D has a current
	// camera, and leave it begun afterwards.
	RwCameraEndUpdate(Scene.camera);
	RwCameraBeginUpdate(Scene.camera);

	RwRaster *whiteRaster = gpWhiteTexture ? RwTextureGetRaster(gpWhiteTexture) : nil;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, whiteRaster);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

	for(int i = 0; i < n; i++){
		HL_Car &car = cars[i];
		const CVector *lamps[2] = { &car.lampL, &car.lampR };
		for(int lamp = 0; lamp < 2; lamp++){
			const CVector &origin = *lamps[lamp];
			// Vertical quad: beam in the forward/up plane.
			HL_DrawConeQuad(origin, car.fwd, car.up,
				range, fadeStart, 0.05f, widthScale, r, g, b, alpha);
			// Horizontal quad: beam in the forward/right plane.
			HL_DrawConeQuad(origin, car.fwd, car.right,
				range, fadeStart, 0.09f, widthScale, r, g, b, alpha);
		}
	}

	// Restore the render states.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, oldRaster);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, oldSrcBlend);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, oldDstBlend);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, oldVertAlpha);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, oldZWrite);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, oldZTest);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, oldFog);
	RwRenderStateSet(rwRENDERSTATECULLMODE, oldCull);
}
