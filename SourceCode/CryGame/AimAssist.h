#pragma once

class CXGame;
class CPlayer;
class CXEntityProcessingCmd;

// Aim assist for touch and pad play: steers the local player's view toward the hostile
// nearest the crosshair. Single player only; cl_aim_assist picks the strength.
namespace AimAssist
{
	// Before CPlayer::ProcessAngles: adjusts this frame's view angles in cmd.
	void Update(CXGame* pGame, CPlayer* pPlayer, CXEntityProcessingCmd& cmd, bool bFiring);
	// After ProcessAngles (clamps, recoil): the angles the next frame's swipe is measured from.
	void Commit(CXEntityProcessingCmd& cmd);
	void Reset();
}
