#include "StdAfx.h"
#include "AimAssist.h"
#include "Game.h"
#include "XPlayer.h"
#include "WeaponClass.h"
#include "WeaponSystemEx.h"
#include "XEntityProcessingCmd.h"
#include <IEntitySystem.h>
#include <IAgent.h>
#include <stdarg.h>
#ifdef LINUX
#include <SDL3/SDL.h>
#endif

namespace
{
	// Per level: friction (swipe scale near a target), pull rate idle and while firing (1/s).
	struct SLevel { float fFriction, fPullIdle, fPullFiring; };
	const SLevel s_Levels[] = { { 1.0f, 0, 0 }, { 0.6f, 2, 5 }, { 0.45f, 4, 12 }, { 0.45f, 4, 12 } };

	const float SNAP_PULL = 30.0f;        // Snap level: rate for SNAP_TIME after fire is pressed
	const float SNAP_TIME = 0.15f;
	const float SWIPE_BREAK = 90.0f;      // deg/s of swipe that pauses the pull
	const float SWIPE_PAUSE = 0.25f;
	const float SWIPE_MIN = 2.0f;         // deg/s below which the view counts as still
	const float STD_FOV = 1.5707963f;     // the unzoomed FOV the cone and range are tuned for

	// cl_aim_assist_debug output; SDL's log reaches logcat on Android, where log.txt is written late.
	void DebugLog(const char* szFormat, ...)
	{
		char szText[256];
		va_list args;
		va_start(args, szFormat);
		vsnprintf(szText, sizeof(szText), szFormat, args);
		va_end(args);
#ifdef LINUX
		SDL_Log("%s", szText);
#else
		GetISystem()->GetILog()->Log("%s", szText);
#endif
	}

	bool s_bPrevValid;
	Vec3 s_vPrevAngles;
	EntityId s_nTarget;
	bool s_bWasFiring;
	float s_fSnapTime, s_fPauseTime;

	// AI species, -1 for entities without a puppet; the player and Val are species 0.
	int Species(IEntity* pEnt)
	{
		IAIObject* pAI = pEnt->GetAI();
		IPuppet* pPuppet;
		if (!pAI || !pAI->CanBeConvertedTo(AIOBJECT_PUPPET, (void**)&pPuppet))
			return -1;
		return pPuppet->GetPuppetParameters().m_nSpecies;
	}

	// Chest bone in world space, else the entity's origin.
	Vec3 AimPoint(IEntity* pEnt)
	{
		IEntityCharacter* pChar = pEnt->GetCharInterface();
		ICryCharInstance* pModel = pChar ? pChar->GetCharacter(0) : NULL;
		ICryBone* pBone = pModel ? pModel->GetBoneByName("Bip01 Spine1") : NULL;
		if (!pBone)
			return pEnt->GetPos();
		Matrix44 m = Matrix44::CreateRotationZYX(-pEnt->GetAngles() * gf_DEGTORAD) * Matrix44::GetTranslationMat(pEnt->GetPos());
		return m.TransformPointOLD(pBone->GetBonePosition());
	}

	// Yaw/pitch still to turn from angles to aim at dir, and the angle between them.
	float AngleTo(const Vec3& angles, const Vec3& dir, float& fYaw, float& fPitch)
	{
		Vec3 target = ConvertVectorToCameraAngles(dir);
		fYaw = Snap_s180(target.z - angles.z);
		fPitch = Snap_s180(target.x - angles.x);
		float fYawAtPitch = fYaw * cry_cosf(angles.x * gf_DEGTORAD);
		return cry_sqrtf(fYawAtPitch * fYawAtPitch + fPitch * fPitch);
	}

	bool Usable(CXGame* pGame, CPlayer* pPlayer)
	{
		if (pGame->IsMultiplayer() || !pPlayer->IsAlive() || pPlayer->GetVehicle() || pPlayer->m_pMountedWeapon ||
			pPlayer->m_stats.onLadder || !pPlayer->m_bFirstPerson || pGame->m_bHideLocalPlayer)
			return false;
		// Hitscan weapons only: no rockets, grenades or melee.
		CWeaponClass* pWeapon = pPlayer->GetSelectedWeapon();
		WeaponParams wp;
		return pWeapon && pWeapon->GetModeParams(pPlayer->m_stats.firemode, wp) && wp.iFireModeType == FireMode_Instant;
	}
}

void AimAssist::Reset()
{
	s_bPrevValid = false;
	s_nTarget = 0;
	s_bWasFiring = false;
	s_fSnapTime = s_fPauseTime = 0;
}

void AimAssist::Update(CXGame* pGame, CPlayer* pPlayer, CXEntityProcessingCmd& cmd, bool bFiring)
{
	int nLevel = pGame->cl_aim_assist->GetIVal();
	if (nLevel <= 0 || nLevel > 3 || !Usable(pGame, pPlayer))
	{
		Reset();
		return;
	}
	const SLevel& lvl = s_Levels[nLevel];
	ISystem* pSystem = pGame->GetSystem();
	float fDt = pSystem->GetITimer()->GetFrameTime();
	if (fDt <= 0)
		return;

	Vec3& angles = cmd.GetDeltaAngles();
	Vec3 swipe(0, 0, 0);
	if (s_bPrevValid)
		swipe = Vec3(Snap_s180(angles.x - s_vPrevAngles.x), 0, Snap_s180(angles.z - s_vPrevAngles.z));
	if (swipe.len() > SWIPE_BREAK * fDt)
		s_fPauseTime = SWIPE_PAUSE;

	const CCamera& cam = pSystem->GetViewCamera();
	Vec3 eye = cam.GetPos();
	float fZoom = STD_FOV / crymax(cam.GetFov(), 0.01f);
	float fCone = pGame->cl_aim_assist_angle->GetFVal() / fZoom;
	float fRange = pGame->cl_aim_assist_range->GetFVal() * crymax(fZoom, 1.0f);
	IEntity* pSelf = pPlayer->GetEntity();

	// Nearest hostile to the crosshair in view; the current target wins close calls.
	IEntity* pBest = NULL;
	float fBestScore = 1e9f, fBestErr = 0, fBestYaw = 0, fBestPitch = 0;
	IEntityItPtr It = pSystem->GetIEntitySystem()->GetEntityInFrustrumIterator(true);
	while (IEntity* pEnt = It->Next())
	{
		// Hostiles are any other species, bar the harmless pigs the levels place as species 1.
		CPlayer* pOther;
		if (pEnt == pSelf || !pEnt->GetContainer() ||
			!pEnt->GetContainer()->QueryContainerInterface(CIT_IPLAYER, (void**)&pOther) ||
			!pOther->IsAlive() || Species(pEnt) <= 0 || !strcmp(pEnt->GetEntityClassName(), "Pig"))
			continue;
		Vec3 dir = AimPoint(pEnt) - eye;
		float fDist = dir.len();
		if (fDist < 1.0f || fDist > fRange)
			continue;
		float fYaw, fPitch;
		float fErr = AngleTo(angles, dir, fYaw, fPitch);
		bool bCurrent = pEnt->GetId() == s_nTarget;
		float fScore = bCurrent ? fErr * 0.6f : fErr;
		if (fErr > (bCurrent ? fCone * 1.3f : fCone) || fScore >= fBestScore)
			continue;
		// Line of sight, starting a little ahead to skip the player's own geometry.
		ray_hit hit;
		Vec3 start = eye + dir * (0.5f / fDist);
		if (pSystem->GetIPhysicalWorld()->RayWorldIntersection(vectorf(start), dir * (1.0f - 0.5f / fDist),
			ent_terrain | ent_static | ent_sleeping_rigid | ent_rigid, 0, &hit, 1, pEnt->GetPhysics(), pSelf->GetPhysics()))
			continue;
		pBest = pEnt;
		fBestScore = fScore;
		fBestErr = fErr;
		fBestYaw = fYaw;
		fBestPitch = fPitch;
	}

	bool bFirePressed = bFiring && !s_bWasFiring;
	s_bWasFiring = bFiring;
	EntityId nBest = pBest ? pBest->GetId() : 0;
	if (pGame->cl_aim_assist_debug->GetIVal())
	{
		static float fNextLog;
		float fNow = pSystem->GetITimer()->GetCurrTime();
		if (nBest != s_nTarget || fNow >= fNextLog || pGame->cl_aim_assist_debug->GetIVal() > 1)
		{
			fNextLog = fNow + 1.0f;
			if (pBest)
				DebugLog("AimAssist: target %s species %d dist %.1f err %.2f cone %.1f yaw %.1f pitch %.1f%s", pBest->GetName(),
					Species(pBest), (pBest->GetPos() - eye).len(), fBestErr, fCone, angles.z, angles.x, bFiring ? " firing" : "");
			else
				DebugLog("AimAssist: no target, yaw %.1f pitch %.1f", angles.z, angles.x);
		}
	}
	s_nTarget = nBest;

	if (pBest)
	{
		if (nLevel == 3 && bFirePressed)
			s_fSnapTime = SNAP_TIME;
		// Never pull against a swipe, or a slow one could not leave the target.
		bool bAway = swipe.len() > SWIPE_MIN * fDt && fBestYaw * swipe.z + fBestPitch * swipe.x < 0;
		// Slow the swipe while it is over the target, but not a flick past it.
		if (s_bPrevValid && s_fPauseTime <= 0 && fBestErr < fCone * 0.5f)
		{
			angles.x = s_vPrevAngles.x + swipe.x * lvl.fFriction;
			angles.z = s_vPrevAngles.z + swipe.z * lvl.fFriction;
			fBestYaw += swipe.z * (1.0f - lvl.fFriction);
			fBestPitch += swipe.x * (1.0f - lvl.fFriction);
		}
		float fRate = s_fSnapTime > 0 ? SNAP_PULL : (s_fPauseTime > 0 || bAway ? 0 : (bFiring ? lvl.fPullFiring : lvl.fPullIdle));
		float fFrac = 1.0f - cry_expf(-fRate * fDt);
		angles.z += fBestYaw * fFrac;
		angles.x += fBestPitch * fFrac;
	}
	s_fSnapTime = crymax(s_fSnapTime - fDt, 0.0f);
	s_fPauseTime = crymax(s_fPauseTime - fDt, 0.0f);
}

void AimAssist::Commit(CXEntityProcessingCmd& cmd)
{
	s_vPrevAngles = cmd.GetDeltaAngles();
	s_bPrevValid = true;
}
