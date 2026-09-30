//////////////////////////////////////////////////////////////////////
//
//	Game source code
//	
//	File: Game.cpp
// 
//	History:
//	-August 02,2001: Create by Alberto Demichelis
//	-Sep 24,2001 : Modified by Petar Kotevski
//
//////////////////////////////////////////////////////////////////////
 
#include "StdAfx.h"
#include <IStreamEngine.h>
#include <ICryPak.h>
#include "Game.h"
#include "XNetwork.h"
#include "XServer.h"
#include "XClient.h"

#include "UIHud.h"

#include "WeaponSystemEx.h"
#include "WeaponClass.h"

#include "XVehicleSystem.h"
#include "PlayerSystem.h"
#include "XServer.h"

#include "XSystemBase.h"

#include "UISystem.h"
#include "Flock.h" 

#include "EntityClassRegistry.h"
#include "ScriptObjectGame.h"
#include "ScriptObjectInput.h"

#include "ScriptObjectLanguage.h"
#include "ScriptObjectPlayer.h"
#include "ScriptObjectSpectator.h"					// CScriptObjectSpectator
#include "ScriptObjectAdvCamSystem.h"				// CScriptObjectAdvCamSystem
#include "ScriptObjectSynched2DTable.h"			// CScriptObjectSynched2DTable
#include "ScriptObjectVehicle.h"
#include "ScriptObjectRenderer.h"
#include "ScriptObjectStream.h"
#include "ScriptObjectWeaponClass.h"
#include "ScriptObjectAI.h"

#include "ScriptObjectUI.h"

#include "ScriptObjectBoids.h"
#include "ScriptTimerMgr.h"

#include "IngameDialog.h"
#include <IEntitySystem.h>
#include <ISound.h>

#include "TagPoint.h"
//#include "XPath.h"

#include "XPlayer.h"
#include "XVehicle.h"

#include <IMovieSystem.h>
#include "CMovieUser.h"

#include "TimeDemoRecorder.h"
#include "GameMods.h"

#ifdef WIN32
#include <winioctl.h>
#include <tchar.h>

typedef std::basic_string< TCHAR > tstring;
typedef std::vector< TCHAR > tvector;
#endif
 
//////////////////////////////////////////////////////////////////////////
// Pointer to Global ISystem.
static ISystem* gISystem = 0;
ISystem* GetISystem()
{
	return gISystem;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// DLL Interface

//////////////////////////////////////////////////////////////////////
// interface of the DLL
IGame* CreateGameInstance()
{
	CXGame *pGame = new CXGame();
	return pGame;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CTagPointManager

class CTagPointManager : public ITagPointManager
{
public:
	CTagPointManager( IGame *pGame )  { m_pGame = (CXGame*) pGame; };
	virtual ~CTagPointManager() {};

	// This function creates a tag point in the game world
	virtual ITagPoint *CreateTagPoint(const string &name, const Vec3 &pos, const Vec3 &angles) 
	{
		return m_pGame->CreateTagPoint( name, pos, angles );
	};

	// Retrieves a tag point by name
	virtual ITagPoint *GetTagPoint(const string &name)
	{
		return m_pGame->GetTagPoint( name );
	}

	// Deletes a tag point from the game
	virtual void RemoveTagPoint(ITagPoint *pPoint)
	{
		m_pGame->RemoveTagPoint( pPoint );
	}

	virtual void AddRespawnPoint(ITagPoint *pPoint)
	{
		m_pGame->AddRespawnPoint( pPoint );
	}

	virtual void RemoveRespawnPoint(ITagPoint *pPoint)
	{
		m_pGame->RemoveRespawnPoint( pPoint );
	}

private:
	CXGame *m_pGame;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// CXGame
//!constructor
CXGame::CXGame()
{
	m_pTimeDemoRecorder = NULL;
	m_pScriptObjectGame = NULL;
	m_pScriptTimerMgr = NULL;
	m_pScriptSystem = NULL;
	m_pServer = NULL;
	m_pClient = NULL;
	m_pLog = NULL;
	m_pServerSnooper = NULL;
	m_pNETServerSnooper = 0;
	m_pRConSystem = 0;
	m_pWeaponSystemEx = NULL;
	m_mapTagPoints.clear();
	m_bMenuInitialized = false;
	m_pCurrentUI = 0;	
	m_pIActionMapManager=NULL;
	m_pIngameDialogMgr = new CIngameDialogMgr();
	m_pUISystem = 0;
	mp_model = 0;
	// to avoid all references to movie user in this file
	m_pMovieUser = new CMovieUser(this);
	m_nPlayerIconTexId = -1;
	m_nVehicleIconTexId = -1;
	m_nBuildingIconTexId = -1;
	m_nUnknownIconTexId = -1;
	m_bMenuOverlay = false;
	m_bUIOverlay = false;
	m_bUIExclusiveInput = false;
	m_bHideLocalPlayer = false;
	m_pCVarCheatMode=NULL;

	m_fTimeGran=m_fFixedStep=m_fTimeGran2FixedStep=m_frFixedStep = 0;
	m_iFixedStep2TimeGran = 0;
	g_language=0;
	g_playerprofile=0;
	g_serverprofile=0;

	m_tPlayerPersistentData.m_bDataSaved=false;
	m_fFadingStartTime=-1.0f;
	cv_game_physics_quality=NULL;
	m_bMapLoadedFromCheckpoint=false;
	m_bSynchronizing = false;

	//m_fTimeToSaveThumbnail = 0;
	m_pGameMods = NULL;
	m_bLastDoLateSwitch = 0;
	m_bLastCDAuthentication = 0;
	m_bAllowQuicksave = true;

	m_sGameName = "FarCry";
	m_pTagPointManager = new CTagPointManager( this );
	m_nDEBUG_TIMING = 0;
	m_fDEBUG_STARTTIMER = 0;
}

//////////////////////////////////////////////////////////////////////
//!destructor
CXGame::~CXGame()
{
	m_pScriptSystem->BeginCall("Shutdown");
	m_pScriptSystem->PushFuncParam(0);
	m_pScriptSystem->EndCall();

	if (m_pIngameDialogMgr)
		delete m_pIngameDialogMgr;
	m_pIngameDialogMgr=NULL;

	if (m_pMovieUser)
	{
		if (m_pSystem)
		{
			IMovieSystem *pMovieSystem=m_pSystem->GetIMovieSystem();
			if (pMovieSystem)
			{
				if (pMovieSystem->GetUser()==m_pMovieUser)
					pMovieSystem->SetUser(NULL);
			}
		}
		delete m_pMovieUser;
	}

	m_pMovieUser=NULL;

	// shutdown the client if there is one
	ShutdownClient();

	// shutdown the server if there is one
	ShutdownServer();

	if (m_pUISystem)
	{
		m_pUISystem->Release();
	}
	SAFE_DELETE(m_pUISystem);


	CScriptObjectUI::ReleaseTemplate();
	CScriptObjectPlayer::ReleaseTemplate();
	CScriptObjectFireParam::ReleaseTemplate();
	CScriptObjectWeaponClass::ReleaseTemplate();
	CScriptObjectVehicle::ReleaseTemplate();
	CScriptObjectSpectator::ReleaseTemplate();
	CScriptObjectAdvCamSystem::ReleaseTemplate();
	CScriptObjectSynched2DTable::ReleaseTemplate();
	CScriptObjectBoids::ReleaseTemplate();
	CScriptObjectRenderer::ReleaseTemplate();
	CScriptObjectGame::ReleaseTemplate();
	CScriptObjectInput::ReleaseTemplate();
	CScriptObjectLanguage::ReleaseTemplate();

	CScriptObjectAI::ReleaseTemplate();
	CScriptObjectServer::ReleaseTemplate();
	CScriptObjectServerSlot::ReleaseTemplate();
	CScriptObjectClient::ReleaseTemplate();
	CScriptObjectStream::ReleaseTemplate();

	cl_scope_flare->Release();
	cl_ThirdPersonRange->Release();

	cl_ThirdPersonOffs->Release();
	cl_ThirdPersonOffsAngHor->Release();
	cl_ThirdPersonOffsAngVert->Release();

	cl_display_hud->Release();
  cl_motiontracker->Release();
  cl_hud_pickup_icons->Release();
  cl_msg_notification->Release();
  cl_hud_name->Release();
	ai_num_of_bots->Release();
	p_name->Release();
	p_model->Release();
	mp_model->Release();
	p_color->Release();
	p_always_run->Release();
	g_language->Release();
	g_playerprofile->Release();
	g_serverprofile->Release();
	g_GC_Frequence->Release();
	p_speed_run->Release();
	p_sprint_scale->Release();

	p_sprint_decoy->Release();
	p_sprint_restore_run->Release();
	p_sprint_restore_idle->Release();

	p_speed_walk->Release();
	p_speed_crouch->Release();
	p_speed_prone->Release();
	p_jump_force->Release();
	p_jump_run_time->Release();
	p_jump_walk_time->Release();
//	p_lean->Release();
	p_lean_offset->Release();
	p_bob_pitch->Release();
	p_bob_roll->Release();
	p_bob_length->Release();
	p_bob_weapon->Release();
	p_bob_fcoeff->Release();
	p_weapon_switch->Release();

	cv_game_physics_quality->Release();

	m_jump_vel->Release();
	m_jump_arc->Release();

	b_camera->Release();

//	f_update->Release();
//	f_draw->Release();
//	f_drawDbg->Release();

	sv_timeout->Release();
	cl_timeout->Release();
	cl_loadtimeout->Release();

	g_LevelName->Release();
	g_GameType->Release();
	g_LeftHanded->Release();

	p_DeadBody->Release();
	p_HitImpulse->Release();
	p_RotateHead->Release();
	a_DrawArea->Release();
	a_LogArea->Release();

	m_pCVarCheatMode->Release();

	pl_JumpNegativeImpulse->Release();

	if (m_pRenderer && (m_nPlayerIconTexId>=0))
		m_pRenderer->RemoveTexture(m_nPlayerIconTexId);
	if (m_pRenderer && (m_nVehicleIconTexId>=0))
		m_pRenderer->RemoveTexture(m_nVehicleIconTexId);
	if (m_pRenderer && (m_nBuildingIconTexId>=0))
		m_pRenderer->RemoveTexture(m_nBuildingIconTexId);
	if (m_pRenderer && (m_nUnknownIconTexId>=0))
		m_pRenderer->RemoveTexture(m_nUnknownIconTexId);
	
	SAFE_DELETE(m_pUIHud);
	SAFE_DELETE(m_pWeaponSystemEx);
	SAFE_DELETE(m_pVehicleSystem);
	SAFE_DELETE(m_pPlayerSystem);

	SAFE_DELETE(m_pFlockManager);

	CScriptObjectPlayer::ReleaseTemplate();
	
	//shutdown script stuff
	SAFE_DELETE(m_pScriptObjectGame);	
	SAFE_DELETE(m_pScriptObjectInput);

	SAFE_DELETE(m_pScriptObjectBoids);
	SAFE_DELETE(m_pScriptObjectLanguage);
	SAFE_DELETE(m_pScriptObjectAI);


	// Release the action map
	SAFE_RELEASE(m_pIActionMapManager);
	SAFE_DELETE(m_pScriptTimerMgr);
	// release the tags
	if (!m_mapTagPoints.empty())
	{
		TagPointMap::iterator ti;
		for (ti=m_mapTagPoints.begin();ti!=m_mapTagPoints.end();ti++)
			delete ti->second;
	}

	SAFE_RELEASE(m_pServerSnooper);
	SAFE_RELEASE(m_pNETServerSnooper);
	SAFE_RELEASE(m_pRConSystem);
	SAFE_DELETE(m_pTimeDemoRecorder);
	SAFE_DELETE(m_pGameMods);

	::operator delete(m_pTagPointManager);
}

//////////////////////////////////////////////////////////////////////////
bool CXGame::InitClassRegistry()
{
	m_EntityClassRegistry.Init( m_pSystem );
	CPlayerSystem *pPlayerSystem = GetPlayerSystem();
	CVehicleSystem *pVehicleSystem = GetVehicleSystem();
	CWeaponSystemEx *pWeaponSystemEx = GetWeaponSystemEx();	// m10

	assert( pPlayerSystem );
	assert( pVehicleSystem );
	assert( pWeaponSystemEx );

	// Enumerate entity classes.
	EntityClass *entCls = NULL;
	m_EntityClassRegistry.MoveFirst();
	do {
		entCls = m_EntityClassRegistry.Next();
		if (entCls)
		{
			const char* entity_type = entCls->strGameType.c_str();
			EntityClassId ClassId = entCls->ClassId;
			if(strcmp("Player",entity_type)==0)
				pPlayerSystem->AddPlayerClass(ClassId);

			if(strcmp("Vehicle",entity_type)==0)
				pVehicleSystem->AddVehicleClass(ClassId);

			if(strcmp("Projectile",entity_type)==0)
			{
				// cannot be loaded at that point - other scripts must be loaded before
				pWeaponSystemEx->AddProjectileClass(ClassId);
			}
		}
	} while (entCls);
	return true;
}

//////////////////////////////////////////////////////////////////////////
void CXGame::SoftReset()
{
	m_pLog->Log("Soft Reset Begin");
	//allow to reload scripts with(LoadScript)
	m_pScriptSystem->SetGlobalToNull("_localplayer");
	if(m_pScriptSystem)
		m_pScriptSystem->UnloadScripts();

	std::vector<string> vLoadedWeapons;

	for (int i = 0; i != m_pWeaponSystemEx->GetNumWeaponClasses(); ++i)
		vLoadedWeapons.push_back(m_pWeaponSystemEx->GetWeaponClass(i)->GetName());

	m_pWeaponSystemEx->Reset();

	if (m_pSystem->GetIMovieSystem())
		m_pSystem->GetIMovieSystem()->StopAllSequences();

	m_pScriptObjectGame->Reset();

	m_pScriptSystem->ForceGarbageCollection();
	//m_p3DEngine->ClearRenderResources();

	m_iLastCmdIdx = 0;

	m_pWeaponSystemEx->Init(this);

	for (std::vector<string>::iterator i = vLoadedWeapons.begin(); i != vLoadedWeapons.end(); ++i)
		AddWeapon((*i).c_str());

	//if (GetMyPlayer())
	//	GetMyPlayer()->SetNeedUpdate(true);

	if (m_pCurrentUI) 
	{
		m_pCurrentUI->Reset();
		m_pCurrentUI->Init(m_pScriptSystem);
	}

	m_pLog->Log("Soft Reset End");
}

//////////////////////////////////////////////////////////////////////
//!reset the game before a level reloading
//!this function allow the reloading of all scripts
//!and wipe out all textures from the 3dengine
void CXGame::Reset()
{
	m_pEntitySystem->Reset();

	// Unload all music.
	if (m_pSystem->GetIMusicSystem())
		m_pSystem->GetIMusicSystem()->Unload();

	//allow to reload scripts with(LoadScript)
	m_pScriptSystem->SetGlobalToNull("_localplayer");
	if(m_pScriptSystem)
		m_pScriptSystem->UnloadScripts();

	m_pWeaponSystemEx->Reset();
	m_XSurfaceMgr.Reset();
	m_XAreaMgr.Clear();
	ClearTagPoints();

	if (m_pSystem->GetIMovieSystem())
		m_pSystem->GetIMovieSystem()->Reset(false);

	m_pScriptObjectGame->Reset();

	m_pScriptSystem->ForceGarbageCollection();
	m_p3DEngine->ClearRenderResources();

	// Must reset all timers.
	m_pScriptTimerMgr->Reset();

	//clen up the input buffer
	if (m_pSystem->GetIInput())
	{
		m_pSystem->GetIInput()->Update(true);
		m_pSystem->GetIInput()->Update(true);
	}
	if (m_pIActionMapManager)
		m_pIActionMapManager->Reset();

	m_iLastCmdIdx = 0;

	//////////////////////////////////////////////////////////////////////////
	// Reset UI.
	//////////////////////////////////////////////////////////////////////////
	if (m_pUISystem)
	{
		m_pUISystem->UnloadAllModels();
		m_pUISystem->StopAllVideo();
		m_p3DEngine->Enable(1);
		//m_pSystem->GetILog()->Log("UISystem: Enabled 3D Engine!");
	}
	if (m_pCurrentUI) 
		m_pCurrentUI->Reset();
	if (m_pUIHud)
		m_pUIHud->Reset();

	if (GetMyPlayer())
		GetMyPlayer()->SetNeedUpdate(true);
}

IXSystem *CXGame::GetXSystem(){return m_pServer?m_pServer->m_pISystem:m_pClient?m_pClient->m_pISystem:NULL;}
//////////////////////////////////////////////////////////////////////
//! Initialize the game. This must be called before calling other functions of this class.
bool CXGame::Init(struct ISystem *pSystem,bool bDedicatedSrv,bool bInEditor,const char *szGameMod)
{	
	// Setup the system and 3D Engine pointers
	m_pSystem	= pSystem;

	m_pGameMods = new CGameMods(this);

	gISystem = pSystem;
	m_bDedicatedServer=bDedicatedSrv;
	m_XAreaMgr.Init( pSystem );
	m_bEditor=bInEditor;


	m_bRelaunch=false;
	m_bMovieSystemPaused = false;
	m_bIsLoadingLevelFromFile=false;
	
	m_bOK = false;
	m_bUpdateRet = true;
	m_pClient	= NULL;
	m_pServer	= NULL;

	m_pSystem->GetILog()->Log("Game Initialization");

	IMovieSystem *pMovieSystem=m_pSystem->GetIMovieSystem();
	if (pMovieSystem)
		pMovieSystem->SetUser(m_pMovieUser);

	if (!m_pTimeDemoRecorder)
		m_pTimeDemoRecorder = new CTimeDemoRecorder(pSystem);
  
	m_pUIHud = NULL;
	m_pNetwork= m_pSystem->GetINetwork();
	m_pLog= m_pSystem->GetILog();
	m_p3DEngine	= m_pSystem->GetI3DEngine();
	m_pRenderer = m_pSystem->GetIRenderer();
	m_pScriptSystem=pSystem->GetIScriptSystem();
	m_pEntitySystem=m_pSystem->GetIEntitySystem();

	// Register game rendering callback.
	//[Timur] m_p3DEngine->SetRenderCallback( OnRenderCallback,this );
	
	// init subsystems
#ifndef _XBOX
	m_pServerSnooper=m_pNetwork->CreateServerSnooper(this);
	m_pNETServerSnooper=m_pNetwork->CreateNETServerSnooper(this);
	m_pRConSystem=m_pNetwork->CreateRConSystem();
#endif
	m_pWeaponSystemEx = new CWeaponSystemEx();
	m_pVehicleSystem = new CVehicleSystem();
	m_pPlayerSystem = new CPlayerSystem();
	m_pFlockManager = new CFlockManager(m_pSystem);

	CScriptObjectUI::InitializeTemplate(m_pScriptSystem);

	// init is not necessary for now, but add here if it later is
	m_pScriptObjectGame=new CScriptObjectGame;
	CScriptObjectGame::InitializeTemplate(m_pScriptSystem);

	m_pScriptObjectInput=new CScriptObjectInput;
	CScriptObjectInput::InitializeTemplate(m_pScriptSystem);
	m_pScriptObjectLanguage=new CScriptObjectLanguage;
	CScriptObjectLanguage::InitializeTemplate(m_pScriptSystem);
	m_pScriptObjectBoids = new CScriptObjectBoids;
	CScriptObjectBoids::InitializeTemplate(m_pScriptSystem);
	m_pScriptObjectAI = new CScriptObjectAI;
	CScriptObjectAI::InitializeTemplate(m_pScriptSystem);
	CScriptObjectServer::InitializeTemplate(m_pScriptSystem);

	CScriptObjectPlayer::InitializeTemplate(m_pScriptSystem);
	CScriptObjectFireParam::InitializeTemplate(m_pScriptSystem);
	CScriptObjectWeaponClass::InitializeTemplate(m_pScriptSystem);
	CScriptObjectVehicle::InitializeTemplate(m_pScriptSystem);
	CScriptObjectSpectator::InitializeTemplate(m_pScriptSystem);
	CScriptObjectAdvCamSystem::InitializeTemplate(m_pScriptSystem);
	CScriptObjectSynched2DTable::InitializeTemplate(m_pScriptSystem);
	CScriptObjectRenderer::InitializeTemplate(m_pScriptSystem);


	m_pScriptObjectGame->Init(m_pScriptSystem, this);
	m_pScriptObjectInput->Init(m_pScriptSystem,this,m_pSystem);


	m_pScriptObjectBoids->Init(m_pScriptSystem,m_pSystem,m_pFlockManager);
	m_pScriptObjectLanguage->Init(m_pScriptSystem,&m_StringTableMgr);
	m_pScriptObjectAI->Init(m_pScriptSystem,m_pSystem,this);

	CScriptObjectServerSlot::InitializeTemplate(m_pScriptSystem);
	CScriptObjectClient::InitializeTemplate(m_pScriptSystem);
	CScriptObjectStream::InitializeTemplate(m_pScriptSystem);
	
	m_pScriptTimerMgr=new CScriptTimerMgr(m_pScriptSystem,m_pSystem->GetIEntitySystem(),this);

// making some constants accessable to the script
	m_pScriptSystem->SetGlobalValue("FireActivation_OnPress",ePressing);
	m_pScriptSystem->SetGlobalValue("FireActivation_OnRelease",eReleasing);
	m_pScriptSystem->SetGlobalValue("FireActivation_OnHold",eHolding);
	
	m_pScriptSystem->SetGlobalValue("ENTITYTYPE_PLAYER", ENTITYTYPE_PLAYER);
	m_pScriptSystem->SetGlobalValue("ENTITYTYPE_WAYPOINT", ENTITYTYPE_WAYPOINT);
	m_pScriptSystem->SetGlobalValue("ENTITYTYPE_OWNTEAM", ENTITYTYPE_OWNTEAM);

/*	m_pScriptSystem->SetGlobalValue("CMD_GO", CMD_GO);
	m_pScriptSystem->SetGlobalValue("CMD_ATTACK", CMD_ATTACK);
	m_pScriptSystem->SetGlobalValue("CMD_DEFEND", CMD_DEFEND);
	m_pScriptSystem->SetGlobalValue("CMD_COVER", CMD_COVER);
	m_pScriptSystem->SetGlobalValue("CMD_BARRAGEFIRE", CMD_BARRAGEFIRE);*/

	InitConsoleVars();

	if (szGameMod && szGameMod[0])
	{
		// apply the mod without restarting as the game just started!
		GetModsInterface()->SetCurrentMod(szGameMod,false);
	}
	
	InitClassRegistry();
		
	// execute the "main"-script (to pre-load other scripts, etc.)
	m_pScriptSystem->ExecuteFile("scripts/main.lua");
	m_pScriptSystem->BeginCall("Init");
	m_pScriptSystem->PushFuncParam(0);
	m_pScriptSystem->EndCall();

	// initialize the surface-manager
	m_XSurfaceMgr.Init(m_pScriptSystem,m_p3DEngine,GetSystem()->GetIPhysicalWorld());
	
	// init key-bindings
	if(!m_bDedicatedServer)
		InitInputMap();

	// create various console-commands/variables
	InitConsoleCommands();
	
	// loading the main language-string-table
	if (!m_StringTableMgr.Load(GetSystem(),*m_pScriptObjectLanguage,g_language->GetString()))
		m_pLog->Log("cannot load language file [%s]",g_language->GetString());
		
	// creating HUD interface
	m_pLog->Log("Initializing UI");
	m_pUIHud = new CUIHud(this,m_pSystem);

	LoadConfiguration("","game.cfg");

	//////////////////////////////////////////////////////////////////////////
	// Materials
	// load materials (once before all, this info stays till we quit the game - no need to load material later)
	// first load normal materials
	m_XSurfaceMgr.LoadMaterials("scripts/materials");
 
	if(!m_bDedicatedServer)
	{
		m_pSystem->GetIConsole()->ShowConsole(0);
		if (!bInEditor)
		{
			//------------------------------------------------------------------------------------------------- 
			m_pUISystem = new CUISystem;

			if (m_pUISystem)
			{
				m_pUISystem->Create(this, m_pSystem, m_pScriptSystem, "Scripts/MenuScreens/UISystem.lua", 1);
			}
			else
			{
				m_pLog->Log("Failed to create UI System!");
			}
			//------------------------------------------------------------------------------------------------- 
		}

		//------------------------------------------------------------------------------------------------- 
		if (m_pUISystem)
		{
			m_bMenuOverlay = 1;
		}
		//------------------------------------------------------------------------------------------------- 
	}
	else
		m_pSystem->GetIConsole()->ShowConsole(true);

	// load textures used as icons by the mini-map
	if (m_pRenderer)
	{
		ITexPic *pPic;
		pPic=m_pRenderer->EF_LoadTexture("gui/map_player", FT_NOREMOVE, 0, eTT_Base);
		if (pPic && pPic->IsTextureLoaded())
			m_nPlayerIconTexId=pPic->GetTextureID();
		pPic=m_pRenderer->EF_LoadTexture("gui/map_vehicle", FT_NOREMOVE, 0, eTT_Base);
		if (pPic && pPic->IsTextureLoaded())
			m_nVehicleIconTexId=pPic->GetTextureID();
		pPic=m_pRenderer->EF_LoadTexture("gui/map_building", FT_NOREMOVE, 0, eTT_Base);
		if (pPic && pPic->IsTextureLoaded())
			m_nBuildingIconTexId=pPic->GetTextureID();
		pPic=m_pRenderer->EF_LoadTexture("gui/map_unknown", FT_NOREMOVE, 0, eTT_Base);
		if (pPic && pPic->IsTextureLoaded())
			m_nUnknownIconTexId=pPic->GetTextureID();
	}

	if(!bInEditor)
		m_pEntitySystem->SetDynamicEntityIdMode(true);	

#if !defined(_XBOX) && !defined(PS2) && !defined(LINUX)
	SetCursor(NULL);
#endif

	//////////////////////////////////////////////////////////////////////////
	
	DevModeInit();
	m_bOK = true;	
	e_deformable_terrain = NULL;

	return (true);
}

//////////////////////////////////////////////////////////////////////
//! game-mainloop
bool CXGame::Run(bool &bRelaunch)
{
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF);
	
	if(m_bDedicatedServer)
	{
		return Update();
	}
	else 
	{
		//static float fLastFrame=0;
		m_bRelaunch=false;
		while(1) 
		{		
			if (!Update()) 
				break;
		}

		bRelaunch=m_bRelaunch;
	}
	return true;
}

#if !defined(_XBOX) && !defined(PS2) && !defined(LINUX)
#include <Mmsystem.h>
#include ".\game.h"
#pragma comment (lib , "Winmm.lib")
#else
#define GetCurrentTime() ((unsigned int)(GetSystem()->GetITimer()->GetCurrTime() * 1000.f))
#endif

//////////////////////////////////////////////////////////////////////////
bool CXGame::IsInPause(IProcess *pProcess)
{
	bool bPause = (m_bMenuOverlay && (!IsMultiplayer()));
	//check if the game is in pause or in menu mode
	if ((pProcess->GetFlags() & PROC_MENU) && !IsMultiplayer())
		bPause=true;

	return (bPause);
}

#ifdef LINUX
#include <SDL3/SDL.h>
#include <deque>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include "farcry_bridge.h"

// Touch input for this frame, drained every frame so nothing stale fires later.
static FarCryTouchInput s_TouchInput;

// The scope button has no release, and hold-to-aim weapons (no ZoomDeadSwitch) only leave the scope on
// one; player.lua's ZoomToggle handler takes 2 as "leave" for every weapon.
static void TouchZoomToggle(CXClient* pClient, ISystem* pSystem)
{
	IScriptSystem* pSS = pSystem->GetIScriptSystem();
	IEntity* pPlayer = pSystem->GetIEntitySystem()->GetEntity(pClient->GetPlayerId());
	if (!pSS || !pPlayer)
		return;
	static const char szQuery[] = "_fcScopeUp = (ClientStuff and ClientStuff.vlayers and ClientStuff.vlayers:IsActive(\"WeaponScope\")) and 1 or 0";
	int nUp = 0;
	pSS->ExecuteBuffer(szQuery, sizeof(szQuery) - 1);
	pSS->GetGlobalValue("_fcScopeUp", nUp);
	if (nUp)
		pPlayer->SendScriptEvent(ScriptEvent_ZoomToggle, 2);
	else
		pClient->OnAction(ACTION_ZOOM_TOGGLE, 1.0f, etPressing);
}

static CPlayer* TouchPlayer(CXClient* pClient, ISystem* pSystem)
{
	IEntity* pEntity = pSystem->GetIEntitySystem()->GetEntity(pClient->GetPlayerId());
	IEntityContainer* pCnt = pEntity ? pEntity->GetContainer() : NULL;
	CPlayer* pPlayer = NULL;
	if (pCnt && pCnt->QueryContainerInterface(CIT_IPLAYER, (void**)&pPlayer))
		return pPlayer;
	return NULL;
}

// The crouch button's stance change, applied to the local player (see CPlayer::TouchStance).
static void TouchStance(CXClient* pClient, ISystem* pSystem, bool bHold)
{
	if (CPlayer* pPlayer = TouchPlayer(pClient, pSystem))
		pPlayer->TouchStance(bHold);
}

// Turn the drained touch state into client actions by name (docs/porting/phase2.md).
static void ApplyTouchInput(CXClient* pClient, ISystem* pSystem)
{
	const FarCryTouchInput& in = s_TouchInput;
	if (in.moveFwd != 0.0f)
		pClient->OnAction(ACTION_MOVEFB, -in.moveFwd, etHolding); // negative is forward
	if (in.moveSide != 0.0f)
		pClient->OnAction(ACTION_MOVELR, in.moveSide, etHolding);

	static const XACTIONID held[FC_HELD_COUNT] = {
		ACTION_FIRE0, ACTION_JUMP, ACTION_MOVEMODE, ACTION_RUNSPRINT, ACTION_WALK,
		ACTION_LEANLEFT, ACTION_LEANRIGHT, ACTION_FIRE_GRENADE };
	// Sprint changes seat each frame it is held in a vehicle, so there send only its press.
	static unsigned prevHeld;
	unsigned heldNow = in.held;
	if (heldNow & (1u << FC_HELD_SPRINT))
	{
		CPlayer* pPlayer = TouchPlayer(pClient, pSystem);
		if (pPlayer && pPlayer->GetVehicle() && (prevHeld & (1u << FC_HELD_SPRINT)))
			heldNow &= ~(1u << FC_HELD_SPRINT);
	}
	prevHeld = in.held;
	for (int i = 0; i < FC_HELD_COUNT; i++)
		if (heldNow & (1u << i))
			pClient->OnAction(held[i], 1.0f, etHolding);

	for (int i = 0; i < in.impulseCount; i++)
	{
		int imp = in.impulses[i];
		if (imp >= FC_IMP_WEAPON_0 && imp <= FC_IMP_WEAPON_0 + 8)
		{
			pClient->OnAction(ACTION_WEAPON_0 + (imp - FC_IMP_WEAPON_0), 1.0f, etPressing);
			continue;
		}
		switch (imp)
		{
			case FC_IMP_USE:           pClient->OnAction(ACTION_USE, 1.0f, etPressing); break;
			case FC_IMP_RELOAD:        pClient->OnAction(ACTION_RELOAD, 1.0f, etPressing); break;
			case FC_IMP_FLASHLIGHT:    pClient->OnAction(ACTION_FLASHLIGHT, 1.0f, etPressing); break;
			case FC_IMP_NEXT_WEAPON:   pClient->OnAction(ACTION_NEXT_WEAPON, 1.0f, etPressing); break;
			case FC_IMP_PREV_WEAPON:   pClient->OnAction(ACTION_PREV_WEAPON, 1.0f, etPressing); break;
			case FC_IMP_CROUCH_TOGGLE: pClient->OnAction(ACTION_MOVEMODE_TOGGLE, 1.0f, etPressing); break;
			case FC_IMP_PRONE:         pClient->OnAction(ACTION_MOVEMODE2, 1.0f, etPressing); break;
			case FC_IMP_ZOOM_TOGGLE:   TouchZoomToggle(pClient, pSystem); break;
			case FC_IMP_BINOCULARS:    pClient->OnAction(ACTION_ITEM_0, 1.0f, etPressing); break;
			case FC_IMP_FIREMODE:      pClient->OnAction(ACTION_FIREMODE, 1.0f, etPressing); break;
			case FC_IMP_CYCLE_GRENADE: pClient->OnAction(ACTION_CYCLE_GRENADE, 1.0f, etPressing); break;
			case FC_IMP_DROP_WEAPON:   pClient->OnAction(ACTION_DROPWEAPON, 1.0f, etPressing); break;
			case FC_IMP_CHANGE_VIEW:   pClient->OnAction(ACTION_CHANGE_VIEW, 1.0f, etPressing); break;
			// Not in the action map; the F5/F6 triggers the client keeps for them.
			case FC_IMP_QUICKSAVE:     pClient->TriggerQuickSave(1.0f, etPressing); break;
			case FC_IMP_QUICKLOAD:     pClient->TriggerQuickLoad(1.0f, etPressing); break;
			case FC_IMP_STANCE_TAP:    TouchStance(pClient, pSystem, false); break;
			case FC_IMP_STANCE_HOLD:   TouchStance(pClient, pSystem, true); break;
			case FC_IMP_CRYVISION:     pClient->OnAction(ACTION_ITEM_1, 1.0f, etPressing); break;
			default: break;
		}
	}
}
#endif

#ifdef LINUX

// Development aid: FARCRY_DEVCMD=<file> drives the UI unattended. The file is read and deleted
// whenever it appears, one line per frame: "mouse x y" (800x600 virtual screen), "click [x y]",
// "key <SDL key name, _ for spaces> [frames]", "keydown/_keyup <name>", "wait n". Presses are held a frame, as CryInput samples once per frame.
static SDL_Keycode DevKey(const char* szName)
{
	string sName(szName);
	std::replace(sName.begin(), sName.end(), '_', ' ');
	return SDL_GetKeyFromName(sName.c_str());
}

static void DevCmdUpdate(ISystem* pSystem, const char* szFile, CXGame* pGame)
{
	static std::deque<string> cmds;
	static int nHold = 0;
	if (FILE* f = fopen(szFile, "r"))
	{
		char line[256];
		int nRead = 0;
		while (fgets(line, sizeof(line), f))
		{
			size_t n = strlen(line);
			while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
				line[--n] = 0;
			if (n)
				cmds.push_back(line), nRead++;
		}
		fclose(f);
		// An empty file is one the writer has created but not filled yet.
		if (nRead)
			unlink(szFile);
	}
	if (nHold > 0)
	{
		nHold--;
		return;
	}
	if (cmds.empty())
		return;
	string cmd = cmds.front();
	cmds.pop_front();
	IMouse* pMouse = pSystem->GetIInput() ? pSystem->GetIInput()->GetIMouse() : NULL;
	char arg[64] = { 0 };
	float x = 0, y = 0;
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	if (sscanf(cmd.c_str(), "mouse %f %f", &x, &y) == 2 && pMouse)
	{
		pMouse->SetVScreenX(x);
		pMouse->SetVScreenY(y);
	}
	else if (strncmp(cmd.c_str(), "click", 5) == 0)
	{
		if (sscanf(cmd.c_str(), "click %f %f", &x, &y) == 2 && pMouse)
		{
			pMouse->SetVScreenX(x);
			pMouse->SetVScreenY(y);
		}
		ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		ev.button.button = SDL_BUTTON_LEFT;
		ev.button.down = true;
		SDL_PushEvent(&ev);
		cmds.push_front("_release");
		nHold = 1;
	}
	else if (cmd == "_release")
	{
		ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
		ev.button.button = SDL_BUTTON_LEFT;
		SDL_PushEvent(&ev);
	}
	else if (sscanf(cmd.c_str(), "keydown %63s", arg) == 1)
	{
		// Held until "_keyup <name>", so it can be combined with other keys.
		ev.type = SDL_EVENT_KEY_DOWN;
		ev.key.key = DevKey(arg);
		ev.key.down = true;
		SDL_PushEvent(&ev);
		nHold = 1;
	}
	else if (sscanf(cmd.c_str(), "key %63s", arg) == 1)
	{
		ev.type = SDL_EVENT_KEY_DOWN;
		ev.key.key = DevKey(arg);
		ev.key.down = true;
		SDL_PushEvent(&ev);
		cmds.push_front(string("_keyup ") + arg);
		nHold = sscanf(cmd.c_str(), "key %*s %f", &x) == 1 ? (int)x : 1;
	}
	else if (sscanf(cmd.c_str(), "_keyup %63s", arg) == 1)
	{
		ev.type = SDL_EVENT_KEY_UP;
		ev.key.key = DevKey(arg);
		SDL_PushEvent(&ev);
	}
	else if (sscanf(cmd.c_str(), "wait %f", &x) == 1)
		nHold = (int)x;
	else if (sscanf(cmd.c_str(), "hold %f", &x) == 1)
	{
		// Hold the left mouse button (fire in game) for n frames, without any touch traffic.
		ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		ev.button.button = SDL_BUTTON_LEFT;
		ev.button.down = true;
		SDL_PushEvent(&ev);
		cmds.push_front("_release");
		nHold = (int)x;
	}
	else if (sscanf(cmd.c_str(), "turn %f %f", &x, &y) == 2)
	{
		// Relative mouse motion in pixels (0.2 degrees each), the path the real mouse and the touch look use.
		ev.type = SDL_EVENT_MOUSE_MOTION;
		ev.motion.xrel = x;
		ev.motion.yrel = y;
		SDL_PushEvent(&ev);
	}
	else if (sscanf(cmd.c_str(), "look %f %f", &x, &y) == 2)
	{
		// Point the player's view (pitch, yaw), e.g. at the ground for decal tests.
		if (pGame->GetMyPlayer()) pGame->GetMyPlayer()->SetAngles(Vec3(x, 0, y));
	}
	else if (strncmp(cmd.c_str(), "cmd ", 4) == 0)
		pSystem->GetIConsole()->ExecuteString(cmd.c_str() + 4, false, true); // "#lua" and cheats regardless of devmode
	else if (sscanf(cmd.c_str(), "env %63s %n", arg, &nHold) == 1)
	{
		// Set (or clear) one of the FARCRY_* development variables; the renderer re-reads FARCRY_SCREENSHOT_EVERY each frame.
		const char* val = cmd.c_str() + nHold;
		nHold = 0;
		*val ? setenv(arg, val, 1) : unsetenv(arg);
	}
	pSystem->GetILog()->Log("DEVCMD: %s", cmd.c_str());
	printf("DEVCMD: %s -> mouse %.0f %.0f\n", cmd.c_str(), pMouse ? pMouse->GetVScreenX() : -1.0f, pMouse ? pMouse->GetVScreenY() : -1.0f);
}
#endif

//////////////////////////////////////////////////////////////////////
//! update all game and children
bool CXGame::Update()
{
	if (!m_nDEBUG_TIMING)
	{
		m_fDEBUG_STARTTIMER = m_pSystem->GetITimer()->GetAsyncCurTime();
		m_nDEBUG_TIMING = 1;
	}

#ifdef LINUX
	// Development aid: FARCRY_RESUME_FRAME=<n> drops the in-game menu once, at frame n, for
	// unattended renderer captures; the menu can be reopened afterwards.
	{
		static int nResumeFrame = getenv("FARCRY_RESUME_FRAME") ? atoi(getenv("FARCRY_RESUME_FRAME")) : -1;
		static int nFrame = 0;
		static bool bResumed = false;
		if (nResumeFrame > 0 && ++nFrame >= nResumeFrame && !bResumed && m_bMenuOverlay && !m_bEditor && m_pClient && m_pClient->IsConnected())
		{
			MenuOff();
			bResumed = true;
		}
		// FARCRY_LOOK="pitch yaw": point the player's view there once gameplay is on screen, for captures.
		static const char* szLook = getenv("FARCRY_LOOK");
		if (szLook && nResumeFrame > 0 && nFrame == nResumeFrame + 200 && GetMyPlayer())
		{
			float fPitch = 0, fYaw = 0;
			sscanf(szLook, "%f %f", &fPitch, &fYaw);
			GetMyPlayer()->SetAngles(Vec3(fPitch, 0, fYaw));
		}
		// FARCRY_SKIP_CUTSCENES=1: keep stopping cutscenes once the menu is off (Training opens on one).
		static bool bSkipCutScenes = getenv("FARCRY_SKIP_CUTSCENES") != NULL;
		if (bSkipCutScenes && nResumeFrame > 0 && nFrame > nResumeFrame && (nFrame % 30) == 0)
			m_pSystem->GetIMovieSystem()->StopAllCutScenes();
		// FARCRY_QUIT_FRAME=<n>: clean shutdown at frame n, so logs and reports are flushed.
		static int nQuitFrame = getenv("FARCRY_QUIT_FRAME") ? atoi(getenv("FARCRY_QUIT_FRAME")) : -1;
		if (nQuitFrame > 0 && nFrame >= nQuitFrame && !m_pSystem->IsQuitting())
			m_pSystem->Quit();
		static const char* szDevCmd = getenv("FARCRY_DEVCMD");
		if (szDevCmd)
			DevCmdUpdate(m_pSystem, szDevCmd, this);
	}
#endif

#ifdef __ANDROID__
	{
		bool bInGame = !m_bMenuOverlay && !m_bEditor && m_pClient && m_pClient->IsConnected();
		FarCry_ReportScreenMode(bInGame, m_pSystem->GetIConsole()->GetStatus());
		FarCry_DrainTouchInput(&s_TouchInput, m_pSystem->GetITimer()->GetFrameTime());
		if (!bInGame)
			s_TouchInput.impulseCount = 0;
	}
#endif

	if (!m_bEditor)
	{
		if (!m_bMenuOverlay || !m_pUISystem || m_pUISystem->GetScriptObjectUI()->CanRenderGame())
		{
			m_p3DEngine->Enable(1);
		}
		else
		{
			m_p3DEngine->Enable(0);
		}
	}

	bool bRenderFrame = (!m_pSystem->GetViewCamera().GetPos().IsZero() || m_bMenuOverlay || m_bUIOverlay) 
											&& g_Render->GetIVal() != 0;

	//////////////////////////////////////////////////////////////////////////
	// Start Profiling frame
	m_pSystem->GetIProfileSystem()->StartFrame();
	//////////////////////////////////////////////////////////////////////////
	FUNCTION_PROFILER( m_pSystem,PROFILE_GAME );

	ITimer *pTimer=m_pSystem->GetITimer();
	pTimer->MeasureTime("EnterGameUp");
	//Timur[10/2/2002]
	// Cannot Run Without System.
	assert( m_pSystem );

	float	maxFPS=g_maxfps->GetFVal();
	if(maxFPS>0)
	{
//		float	extraTime = pTimer->GetAsyncCurTime() + 1.0f/maxFPS - pTimer->GetFrameTime();
//		while(extraTime - pTimer->GetAsyncCurTime() > 0)
//			;
#if !defined(LINUX)
		DWORD	extraTime = (DWORD)((1.0f/maxFPS - pTimer->GetFrameTime())*1000.0f);
		if(extraTime>0&&extraTime<300)//thread sleep not process sleep
			Sleep(extraTime);
#else
		// Pace against a running deadline: the frame time already includes the previous sleep, so
		// sleeping "budget minus frame time" alternates between sleeping and not, landing near 2x the cap.
		static Uint64 nNextFrame = 0;
		Uint64 nNow = SDL_GetTicksNS(), nPeriod = (Uint64)(1.0e9 / maxFPS);
		if (nNextFrame == 0 || nNow > nNextFrame + nPeriod)
			nNextFrame = nNow;
		else if (nNow < nNextFrame)
			SDL_DelayNS(nNextFrame - nNow);
		nNextFrame += nPeriod;
#endif
	}

	PhysicsVars *pVars = m_pSystem->GetIPhysicalWorld()->GetPhysVars();
	float fTimeGran=pVars->timeGranularity,	fFixedStep=g_MP_fixed_timestep->GetFVal();
	if (fTimeGran!=m_fTimeGran || fFixedStep!=m_fFixedStep)
	{
		m_fTimeGran = fTimeGran; m_fFixedStep = fFixedStep;
		if (fFixedStep>0)
		{
			m_fTimeGran2FixedStep = fTimeGran/fFixedStep;
			m_iFixedStep2TimeGran = (int)(fFixedStep/fTimeGran+0.5f);
			m_frFixedStep = 1.0f/fFixedStep;
		}
		else
			m_iFixedStep2TimeGran = 0;
		m_frTimeGran = 1.0f/fTimeGran;
	}
	pVars->bMultiplayer = IsMultiplayer() ? 1:0;

	//check what is the current process 
	//bool bPause=false;
	IProcess *pProcess=m_pSystem->GetIProcess();
	if (!pProcess)
		return false;

	bool bPause=IsInPause(pProcess);
	if (m_bIsLoadingLevelFromFile)
		bPause=false;

	// Pauses or unpauses movie system.
	if (bPause != m_bMovieSystemPaused)
	{
		m_bMovieSystemPaused = bPause;
		IMovieSystem *pMovieSystem=m_pSystem->GetIMovieSystem();
		if (pMovieSystem)
		{
			if (bPause)
				pMovieSystem->Pause();
			else
				pMovieSystem->Resume();
		}
	}

	// [marco] check current sound and vis areas
	// for music etc.	
	CheckSoundVisAreas();

	//int nStartGC=m_pScriptSystem->GetCGCount();
	pTimer->MeasureTime("SomeStuff");
	// update system
	int nPauseMode=0;
	if (bPause)
		nPauseMode=1;
	// TODO: Add pause mode for a cutscene currently playing (pausemode 2)

	if (IsMultiplayer()) {
		pe_params_flags pf; pf.flagsOR = pef_update;
		for(ListOfPlayers::iterator	pl=m_DeadPlayers.begin(); pl!=m_DeadPlayers.end(); pl++)
			if ((*pl)->GetEntity() && (*pl)->GetEntity()->GetPhysics())
				(*pl)->GetEntity()->GetPhysics()->SetParams(&pf);
	}

	ICVar* pVarFOV = m_pSystem->GetIConsole()->GetCVar("game_fov");
	float fov = 90.0f;
	if (pVarFOV)
	{
		fov = pVarFOV->GetFVal();
	}
	
	// The multiplayer flag selects fixed-step (g_MP_fixed_timestep) physics with catch-up and no
	// interpolation: rigid bodies then move in 10 ms quanta, visible as vehicle jitter. Only the
	// networked game needs that determinism; single player steps physics by the frame time.
	if (!m_pSystem->Update(
		IsMultiplayer() ? ESYSUPDATE_MULTIPLAYER:0,
		nPauseMode)) //Update returns false when quitting
		return (false);

	if (pVarFOV)
	{
		if (fabs(pVarFOV->GetFVal() - fov) > 0.001f)
		{
			fov = (pVarFOV->GetFVal() * 3.14159f) / 180.0f;
			CXClient* pClient = GetClient();
			if (pClient)
			{
				IEntity* pEntity = pClient->m_pISystem->GetLocalPlayer();
				IEntityCamera* pCamera = pEntity->GetCamera();
				IRenderer* pRenderer = m_pUISystem->GetIRenderer();
				if (pCamera)
				{
					pCamera->SetFov(fov,m_pRenderer->GetWidth(),m_pRenderer->GetHeight());
				}
			}

			m_pScriptSystem->ExecuteFile("scripts/default/hud/zoomview.lua", true, true);
		}
	}

	if (IsMultiplayer()) {
		pe_params_flags pf; pf.flagsAND = ~pef_update;
		for(ListOfPlayers::iterator	pl=m_DeadPlayers.begin(); pl!=m_DeadPlayers.end(); pl++)
			if ((*pl)->GetEntity() && (*pl)->GetEntity()->GetPhysics())
				(*pl)->GetEntity()->GetPhysics()->SetParams(&pf);
	}

	pTimer->MeasureTime("SysUpdate");
	
	// [marco] after system update, retrigger areas if necessary
	if (!bPause)
		RetriggerAreas();

	if (!bPause || (m_pClient && !m_pClient->IsConnected()))
	{	// network start
		FRAME_PROFILER( "GameUpdate:Client",m_pSystem,PROFILE_GAME );

		// update client
		if (m_pClient)
		{
			m_pClient->UpdateClientNetwork();
			pTimer->MeasureTime("Net");

			assert(m_pClient);
#ifdef __ANDROID__
			if (!m_bMenuOverlay && !m_bEditor && m_pClient->IsConnected())
			{
				// Joypad mode runs mouse look through a stick dead zone that eats slow swipes; XPlayer's AnalogMove covers the sticks.
				if (cl_use_joypad->GetIVal())
					cl_use_joypad->Set(0);
				ApplyTouchInput(m_pClient, m_pSystem);
			}
#endif
			m_pClient->Update();
			
			if(m_pClient->DestructIfMarked())			//  to make sure the client is only released in one place - here
				m_pClient=0;
		}

		pTimer->MeasureTime("ClServ Up");

		////////UPDATE THE NETWORK 
		// [Anton] moved from after the rendering, so that the server has access to the most recent physics data
		// update server
		if (m_pServer)
		{
			FRAME_PROFILER( "GameUpdate:Server",m_pSystem,PROFILE_GAME );

			m_pServer->Update();
		}

		pTimer->MeasureTime("EndServUp");
	}

	m_pNetwork->UpdateNetwork();	// used to update things like the UBI.com services

	DWORD dwCurrentTimeInMS=GetCurrentTime();

	if (!m_pSystem->IsDedicated())
	{
		if(m_pServerSnooper)
			m_pServerSnooper->Update(dwCurrentTimeInMS);

		if(m_pNETServerSnooper)
			m_pNETServerSnooper->Update(dwCurrentTimeInMS);
	}

	if(m_pRConSystem)
	{
		if (m_pClient)
			m_pRConSystem->Update( dwCurrentTimeInMS,m_pClient->GetNetworkClient() );
		else
			m_pRConSystem->Update(dwCurrentTimeInMS);
	}

	// network end

	// system rendering
	if (bRenderFrame)
	{	
		// render begin must be always called anyway to clear buffer, draw buttons etc.
		// even in menu mode
		m_pSystem->RenderBegin();
		
		m_pSystem->Render();
		pTimer->MeasureTime("3SysRend");
	}
		
	// update the HUD	
	if (m_pCurrentUI && !bPause && m_pClient && m_pClient->m_bDisplayHud)		
	{
		FRAME_PROFILER( "GameUpdate:HUD",m_pSystem,PROFILE_GAME );

		// update hud itself
		if(!m_pCurrentUI->Update())
			m_bUpdateRet = false;

    // update ingame-dialog-manager
		if (m_pIngameDialogMgr)
			m_pIngameDialogMgr->Update();
	
    pTimer->MeasureTime("HUD Up");
	}

	if (m_pUISystem && m_pUISystem->IsEnabled())
	{
		FRAME_PROFILER("GameUpdate:UI", m_pSystem, PROFILE_GAME);

		if (m_bMenuOverlay || m_bUIOverlay)
		{
			m_pUISystem->Update();			
			m_pUISystem->Draw();
		}
	}

	if(a_DrawArea->GetIVal())
  {
		m_XAreaMgr.DrawAreas( m_pSystem );
    pTimer->MeasureTime("XAreaDraw");
  }

	 // print time profiling results
#ifndef PS2
	pTimer->MeasureTime((const char*)-1);
#else
	pTimer->MeasureTime("Time");
#endif

	pTimer->MeasureTime("3TimeProf");

	//NETWORK DEBUGGING
	if(m_pClient && m_pClient->cl_netstats->GetIVal()!=0)
	{
		m_pClient->DrawNetStats();
	}
	if(m_pServer && m_pServer->sv_netstats->GetIVal()!=0)
	{
		m_pServer->DrawNetStats(m_pRenderer);
	}

	pTimer->MeasureTime("NetStats");

	// end of frame
	if (bRenderFrame)
  {	
		// same thing as for render begin		
		if (m_pTimeDemoRecorder)
			m_pTimeDemoRecorder->RenderInfo();

		m_pSystem->RenderEnd();
	}
  pTimer->MeasureTime("3Rend Up");
	
	// get messages from process
	//char *szProcessMessage = m_pSystem->GetIProcess()->GetPMessage();
	// process messages from process
	{
		if (m_fFadingStartTime>0)
		{
			if (((m_pSystem->GetITimer()->GetCurrTime())-m_fFadingStartTime)>1.5f)
			{
				m_fFadingStartTime=-1;
				SendMessage(m_szLoadMsg);
			}
		}

		while(!m_qMessages.empty())
		{
			string smsg=m_qMessages.front();
			m_qMessages.pop();
			ProcessPMessages(smsg.c_str());		
		}

		// the messages can switch the game to menu or viceversa
		bPause=IsInPause(pProcess);
	}
	
	//update script timers
	if(m_pScriptTimerMgr)
		m_pScriptTimerMgr->Update( (unsigned long)(pTimer->GetCurrTime()*1000) );
	
	pTimer->MeasureTime("ScrTimerUp");

	if(g_GC_Frequence->GetFVal()>0)
	{
		// Change Script Garbage collection frequency.
		m_pSystem->SetGCFrequency(g_GC_Frequence->GetFVal());
	}

	//////////////////////////////////////////////////////////////////////////
	// Special update function for developers mode.
	//////////////////////////////////////////////////////////////////////////
	if (IsDevModeEnable())		
	{
		if (!DevModeUpdate())
		{
			return false;
		}
	}
		
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////

	pTimer->MeasureTime("EndGameUp");

	//////////////////////////////////////////////////////////////////////////
	// End Profiling Frame
	m_pSystem->GetIProfileSystem()->EndFrame();
	//////////////////////////////////////////////////////////////////////////

	return (m_bUpdateRet);
}

void CXGame::UpdateDuringLoading()
{
/*	// to find areas where the function should be called more often
	{
		static DWORD dwTime=GetTickCount();

		DWORD dwNewTime=GetTickCount();

		DWORD dwRelative = dwNewTime-dwTime;

		if(dwRelative>1000)
			m_pSystem->GetILog()->Log(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> UpdateDuringLoading %d",dwRelative);
		else 
			m_pSystem->GetILog()->Log("UpdateDuringLoading %d",dwRelative);

		dwTime=dwNewTime;
	}
*/

	if(m_pServer)
		m_pServer->UpdateXServerNetwork();
}



//////////////////////////////////////////////////////////////////////////
bool CXGame::ParseLevelName(const char *szMsg,char *szLevelName,char *szMissionName)
{	
	const char *szPtr=szMsg;
	char *szDest;

	// get level & mission-name
	szPtr=szMsg+11; //skip "StartLevel "
	//find the level name
	memset(szLevelName,0,32);
	szDest=szLevelName;
	while ((*szPtr) && (*szPtr!=' '))		
		*szDest++=*szPtr++;		

	//find the mission name
	memset(szMissionName,0,32);

	if (*szPtr) //if not eos
	{
		szPtr++; //skip space
		szDest=szMissionName;			
		while (*szPtr)		
			*szDest++=*szPtr++;					
	}

	return (true);
}

//////////////////////////////////////////////////////////////////////
//Process the process messages
//it is needed for example to perform actions that must be done
//the next frame
void CXGame::ProcessPMessages(const char *szMsg)
{
	if (!szMsg) 
		return;

	if ((stricmp(szMsg,"EndDemo") == 0) || (stricmp(szMsg,"EndDemoQuit") == 0))	// used for demos (e3, magazine demos)
	{ 
		ITexPic * pPic = m_pRenderer->EF_LoadTexture("Textures/end_screen.dds", FT_NORESIZE, 0, eTT_Base);
		int img = -1;

		if (pPic)
		{
			img = pPic->GetTextureID();
		}

		GetSystem()->GetIRenderer()->BeginFrame();
		//GetSystem()->GetIRenderer()->Draw2dImage(0,0,(float)(GetSystem()->GetIRenderer()->GetWidth()),(float)(GetSystem()->GetIRenderer()->GetHeight()),img,0,1.0f,1.0f,0);
		// hack! hardcoded to fix the incorrect d3d9 renderer matrix
		GetSystem()->GetIRenderer()->SetState(GS_NODEPTHTEST);
		GetSystem()->GetIRenderer()->Draw2dImage(0,0,(float)(800),(float)(600),img,0,1.0f,1.0f,0);
		GetSystem()->GetIRenderer()->Update();
		float	time = GetSystem()->GetITimer()->GetCurrTime();
		// wait a bit and then quit the game
		while((GetSystem()->GetITimer()->GetCurrTime()-time) < 7)
		{
			GetSystem()->GetITimer()->Update();
		}

		if (stricmp(szMsg,"EndDemoQuit") == 0)
		{
			m_bUpdateRet = false;
		}
		else
		{
			this->SendMessage("Switch");
		}
		return;
	}
	else
	if (stricmp(szMsg,"Quit-Yes") == 0)	// quit message
	{
		m_bUpdateRet = false;
		return;
	}
	else
	if (stricmp(szMsg,"Relaunch") == 0)	// relaunch message
	{
		m_bRelaunch = true;
		m_bUpdateRet = false;
		return;
	}
	else
	if (stricmp(szMsg,"Switch")==0)	// switch process (menu <> game)
	{
		//clear the current message
//		::OutputDebugString("###############switch#############\n");
		//m_pSystem->GetIProcess()->SetPMessage("");				

    if(m_bEditor) return; // we are probably in the editor?

		if (m_bMenuOverlay)				// we're in menu-mode; switch to game
		{
			//check if a game is currently running before switching to
			//the 3d-engine

			CPlayer *pPlayer = 0;
			if (GetMyPlayer())
			{
				GetMyPlayer()->GetContainer()->QueryContainerInterface(CIT_IPLAYER,(void **)&pPlayer);
			}
			// there must be a client for us to be able to go out of the menu
			// if this is a singleplayer game, and the player is alive, we can go back to game
			// otherwise, if this is not multiplayer game and the player is dead, it is locked in the menu
			// also, the client must not be loading, or waiting to connect
			if (m_pClient && m_pClient->IsConnected() && m_pUISystem->GetScriptObjectUI()->CanSwitch(0))
			{
				if (IsMultiplayer() || (!pPlayer || pPlayer->m_stats.health > 0))
				{
					MenuOff();
				}
			}
		}	
		else if (m_pUISystem->GetScriptObjectUI()->CanSwitch(1))
		{
//			::OutputDebugString("->menu\n");
      // quit if menu disabled (usefull during development)
			ICVar * pCvarNoMenu = m_pSystem->GetIConsole()->CreateVariable("g_NoMenu","0",0);

      if(pCvarNoMenu && pCvarNoMenu->GetIVal())
			{
				m_pSystem->Quit();

				return;
			}

			MenuOn();
		}			
	}
	else if (stricmp(szMsg, "EndCutScene")==0)
	{
		if (m_bMenuOverlay)				// we're in menu-mode; switch to game
		{
			if (m_pClient)
			{
				MenuOff();
			}
			else if(!IsMultiplayer())
			{
				// there's no game, so lets quit...
				// exit from game-must be prompted with "Are you Sure?"
				m_pSystem->Quit();
			}
		}	
	}
	else if (stricmp(szMsg,"LoadLastCheckPoint")==0)
	{		
		// the loading funciotn calls game->update 20 times(??)
		// and the script menu system allows to click the reload
		// button again during game->update, so be sure that the game isn't
		// loading already
		if (!m_bIsLoadingLevelFromFile) 
		{	
			LoadLatest();
		}
	}
	else if (strnicmp(szMsg,"StartLevelFade",14)==0)		 // start a level with fade out
	{
		if (!m_bEditor)
		{
			ICVar *g_LevelStated = GetISystem()->GetIConsole()->GetCVar("g_LevelStated");

			if (g_LevelStated && g_LevelStated->GetIVal() && m_pClient && m_pClient->IsConnected())
			{
				HSCRIPTFUNCTION pfnDisplayLevelStats = m_pScriptSystem->GetFunctionPtr("Game", "DisplayTimeZone");

				if (pfnDisplayLevelStats)
				{
					m_pScriptSystem->BeginCall(pfnDisplayLevelStats);
					m_pScriptSystem->PushFuncParam(GetScriptObject());
					m_pScriptSystem->EndCall();

					m_pScriptSystem->ReleaseFunc(pfnDisplayLevelStats);
				}
			}
			else
			{
				m_p3DEngine->ResetScreenFx();
				m_p3DEngine->SetScreenFx("ScreenFade",1);
				float fFadeTime=1.5f;
				m_p3DEngine->SetScreenFxParam("ScreenFade","ScreenFadeTime", &fFadeTime);
				m_fFadingStartTime=m_pSystem->GetITimer()->GetCurrTime();
				AllowQuicksave(false);
				sprintf(m_szLoadMsg,"StartLevel%s",szMsg+14); // skip levelfade
				// disable input handling during fading to load
				m_pSystem->GetIInput()->EnableEventPosting(0);
			}
		}
		else
			m_pSystem->GetILog()->Log("Skipping Load Level in editor (%s)",szMsg);		
	}
	else if (strnicmp(szMsg,"StartLevel",10)==0)		 // start a level
	{
		if (!m_bEditor)
		{
			ICVar *g_LevelStated = GetISystem()->GetIConsole()->GetCVar("g_LevelStated");

			if (g_LevelStated && g_LevelStated->GetIVal() && m_pClient && m_pClient->IsConnected())
			{
				HSCRIPTFUNCTION pfnDisplayLevelStats = m_pScriptSystem->GetFunctionPtr("Game", "DisplayTimeZone");

				if (pfnDisplayLevelStats)
				{
					m_pScriptSystem->BeginCall(pfnDisplayLevelStats);
					m_pScriptSystem->PushFuncParam(GetScriptObject());
					m_pScriptSystem->EndCall();

					m_pScriptSystem->ReleaseFunc(pfnDisplayLevelStats);
				}
			}
			else
			{
				char szLevelName[32];
				char szMissionName[32];

				m_p3DEngine->ResetScreenFx();
				// always disable cryvision at start
				m_p3DEngine->SetScreenFx("NightVision",0);

				// reset fading in case someone will load a level
				// while the current one is fading out
				m_fFadingStartTime=-1; 
				m_p3DEngine->SetScreenFx("ScreenFade",0);

				ParseLevelName(szMsg,szLevelName,szMissionName);

				bool listen = false;
				if (strcmp(szMissionName, "listen")==0)
				{
					szMissionName[0] = 0;

					//strcpy( szMissionName,"" );
					listen = true;
				};

				// disable input handling during load
				m_pSystem->GetIInput()->EnableEventPosting(0);
				m_pSystem->GetIInput()->GetIKeyboard()->ClearKeyState();
				LoadLevelCS(false, szLevelName, szMissionName, listen);
				// finished loading, reenable input handling			
				m_pSystem->GetIInput()->EnableEventPosting(1);
				m_pSystem->GetIInput()->GetIKeyboard()->ClearKeyState();
			}
		}
		else		
			m_pSystem->GetILog()->Log("Skipping Load Level in editor (%s)",szMsg);		
	}			
	else
	if (strnicmp(szMsg,"SaveGame", 8)==0)		// save current game
	{
		if(!m_bEditor)
		{
			//clear the current message
//			m_pSystem->GetIProcess()->SetPMessage("");				
			const char *sname="quicksave";
			if(strlen(szMsg)>8) { 
				sname=szMsg+9;
			}
			Save(sname, NULL, NULL);			
		}
	}
	else
	if (strnicmp(szMsg,"LoadGame", 8)==0)		// load game
	{
		if(!m_bEditor)
		{
      m_p3DEngine->ResetScreenFx();
			//clear the current message
//			m_pSystem->GetIProcess()->SetPMessage("");				
			const char *sname="quicksave";
			if(strlen(szMsg)>8)
			{
				sname=szMsg+9;
			}

			// disable input handling during load
			m_pSystem->GetIInput()->EnableEventPosting(0);
			//m_pSystem->GetIInput()->GetIKeyboard()->ClearKeyState();
			// get out of all the areas localplayer is in - on load it will be retrigererd
			IEntity *pIMyPlayer = GetMyPlayer();
			if( pIMyPlayer )
			{
				IEntityContainer *pIContainer = pIMyPlayer->GetContainer();
				if (pIContainer)
				{
					CPlayer *pPlayer = NULL;
					if (pIContainer->QueryContainerInterface(CIT_IPLAYER, (void**)&pPlayer))
						m_XAreaMgr.ExitAllAreas( pPlayer->m_AreaUser );
				}
			}

			if (Load(sname))
			{
				//if loaded successfull, switch to 3d engine 			
				if(m_pClient)
				{
					m_pSystem->SetIProcess(m_p3DEngine);
					m_pSystem->GetIProcess()->SetFlags(PROC_3DENGINE);								
				}
			}

			// finished loading, reenable input
			m_pSystem->GetIInput()->EnableEventPosting(1);
			//m_pSystem->GetIInput()->GetIKeyboard()->ClearKeyState();
		}
	}
// 	else
//	if (stricmp(szMsg,"ConnectTo")==0)
//	{
//	}
}

#ifdef LINUX
string CXGame::GetCorrectedLevelPath(string in)
{
	string ret;
	ICryPak *pIPak = m_pSystem->GetIPak();
	dirent c_file;
	intptr_t hFile;
	bool found_mission = false;
	string level; 

	ret = in;
	
	if (ret.find('/') == string::npos)
	{
		return in;
	}
	
	level = ret.substr(ret.find('/') + 1, string::npos);
	ret.erase(ret.find('/'), string::npos);
	ret += "/*";
	hFile = pIPak->FindFirst(ret.c_str(), &c_file);
	if (hFile == -1L)
	{
		return in;
	}
	do
	{
		if (IS_DIR(c_file))
		{
			if (!stricmp(FNAME(c_file), level.c_str()))
			{
				found_mission = true;
				level = string(FNAME(c_file));
				break;
			}
		}
	} while(pIPak->FindNext( hFile, &c_file ) == 0);

	pIPak->FindClose( hFile );

	if (found_mission)
	{
		ret.erase(ret.find('*'), string::npos);
		ret += level;
	}
	else
	{
		return in;
	}

	return ret;
}
#endif

//////////////////////////////////////////////////////////////////////////
/*! Load a level on the local machine (connecting with a local client)
	@param dedicated if true the local client will not be created
	@param keepclient if true the current client server connection will be kept
	@param szMapName name of the level that has to be loaded
	@param szMissionName name of the mission that has to be loaded
	@param listen allow external clients to connect
*/
//////////////////////////////////////////////////////////////////////////
void CXGame::LoadLevelCS(bool keepclient, const char *szMapName, const char *szMissionName, bool listen)
{
	// need to reset timers as well
	m_pScriptTimerMgr->Reset();

	if (m_pUISystem)
	{
		m_pUISystem->GetScriptObjectUI()->OnSwitch(0);
		m_pUISystem->StopAllVideo();
		m_p3DEngine->Enable(1);

		m_pSystem->GetILog()->Log("UISystem: Enabled 3D Engine!");
	}


	if (m_pSystem->GetIMovieSystem())
		m_pSystem->GetIMovieSystem()->StopAllCutScenes();
	//m_lstPlayedCutScenes.clear();

	bool bDedicated=GetSystem()->IsDedicated();

	string strGameType = g_GameType->GetString();

	AutoSuspendTimeQuota AutoSuspender(GetSystem()->GetStreamEngine());

	assert( szMissionName != 0 );
	
	string sLevelFolder = szMapName;
	if (sLevelFolder.find('\\') == string::npos && sLevelFolder.find('/') == string::npos)
	{
		// This is just a map name, not a folder.
		sLevelFolder = GetLevelsFolder() + "/" + sLevelFolder;
#ifdef LINUX
		DIR *fdir;

#ifdef __ANDROID__
		fdir = opendir(CryGameRoot() ? (string(CryGameRoot()) + "/" + sLevelFolder).c_str() : sLevelFolder.c_str());
#else
		fdir = opendir(sLevelFolder.c_str());
#endif
		if (!fdir)
		{
			sLevelFolder = GetCorrectedLevelPath(sLevelFolder);
		}
		else
		{
			closedir(fdir);
		}
#endif
	}

	IConsole *pConsole=GetSystem()->GetIConsole();
	IInput *pInput=GetSystem()->GetIInput();					// might be 0 (e.g. dedicated server)

	if(pInput)
		pInput->SetMouseExclusive(false);
	
	//if(!*szMissionName) szMissionName = "Multiplayer";

	if (!IsMultiplayer())
	{
		m_pSystem->GetIConsole()->Clear();
		m_pSystem->GetIConsole()->SetScrollMax(600);
		m_pSystem->GetIConsole()->ShowConsole(true);

		string sLoadingScreenTexture = string("levels/") + string(szMapName) + string("/loadscreen_") + string(szMapName) + ".dds";

		m_pSystem->GetIConsole()->SetLoadingImage(sLoadingScreenTexture.c_str());
		m_pSystem->GetIConsole()->ResetProgressBar(0x7fffffff);
		m_pSystem->GetILog()->UpdateLoadingScreen("");	// just to draw the console
	}

	if (m_pClient && !keepclient)
	{
		ShutdownClient();
	}

	//ShutdownClient() can sometimes add "StartLevel" messages to the queue,
	//causing levels to be loaded multiple times.
	DeleteStartLevelMessages();

	// start server
	if((!m_pServer || !keepclient) && !StartupServer(listen))
	{
		m_pLog->LogToConsole("Unable to load the level %s,%s [startup server failed]", sLevelFolder.c_str(),szMissionName);
		if(pInput)
			pInput->SetMouseExclusive(true);
		LoadingError("@LoadLevelError");
		return;
	}

	bool bNeedClient = !bDedicated && ((keepclient && !m_pClient) || !keepclient);

	//create local client(must be before the level is loaded)
	if(bNeedClient)
	{
		if(!StartupLocalClient())
		{
			m_pLog->LogToConsole("Unable to load the level %s,mission %s [startup client failed]", sLevelFolder.c_str(),szMissionName);
			if(pInput)
				pInput->SetMouseExclusive(true);
			LoadingError("@LoadLevelError");
			return;
		}
	}

	const char *szMission = szMissionName;
	if (!*szMissionName)
		szMission=strGameType.c_str();

	// KIRILL lets reset - they will be spawned anyway
	m_pSystem->GetAISystem()->Reset();
	
	// refresh the current server info for incoming queries during loading
	m_pServer->GetServerInfo();

	// load the level
	if(!m_pServer->m_pISystem->LoadLevel( sLevelFolder.c_str(),szMission,false))
	{
		m_pLog->LogToConsole("Unable to load the level %s,mission %s \n", sLevelFolder.c_str(),szMissionName);
		if (pInput)
			pInput->SetMouseExclusive(true);
		LoadingError("@LoadLevelError");
		return;
	}

// start and connect a local client
	if(bNeedClient)
	{
		if(m_pClient)
		{
			if (IsMultiplayer() && m_pServer->m_pIServer->GetServerType()!=eMPST_LAN)
     		m_pClient->XConnect("127.0.0.1",false,true);
			else
				m_pClient->XConnect("127.0.0.1");
		}
	}
	
	if(m_pClient)
    m_pClient->OnMapChanged();
	if(m_pServer)
    m_pServer->OnMapChanged(); 
	if(pInput)
		pInput->SetMouseExclusive(true);
	AllowQuicksave(true);
};

//////////////////////////////////////////////////////////////////////////
bool CXGame::GetLevelMissions( const char *szLevelDir,std::vector<string> &missions )
{
	string sLevelPath = szLevelDir;
	
	if (!szLevelDir || sLevelPath.empty())	
		return false;

	string sEPath = sLevelPath+string("/levelinfo.xml");
	string sPaks = sLevelPath + "/*.pak";
	//GetSystem()->GetIPak()->OpenPacks(sPaks.c_str());	
	OpenPacks(sPaks.c_str());

	XmlNodeRef root = GetSystem()->LoadXmlFile( sEPath.c_str() );
	bool bResult = false;
	if (root)
	{
		XmlNodeRef missionsNode = root->findChild( "Missions" );
		if (missionsNode)
		{
			// we found a mission node - the level is valid
			bResult = true;
			for (int i = 0; i < missionsNode->getChildCount(); i++)
			{
				XmlNodeRef missionNode = missionsNode->getChild(i);
				if (missionNode->isTag( "Mission" ))
				{
					const char *sMissionName = missionNode->getAttr( "Name" );
					if (sMissionName)
						missions.push_back( sMissionName );
				}
			}
		}
	}
	//GetSystem()->GetIPak()->
	ClosePacks(sPaks.c_str());
	return bResult;
}

//////////////////////////////////////////////////////////////////////////
bool CXGame::IsMultiplayer()
{
	// cannot be in multiplayer when in editor
	if (m_bEditor)
		return false;

	bool bServer=IsServer();
	bool bClient=IsClient();

	if(!bServer && !bClient)
		return false;

	return !bServer || !bClient || m_pServer->m_bListen;
};

//////////////////////////////////////////////////////////////////////////
void CXGame::ResetState(void)
{
//	m_PlayersWithLighs.clear();
	m_pSystem->GetIEntitySystem()->ResetEntities();
	if (GetMyPlayer())
	{
		m_XAreaMgr.ReTriggerArea(GetMyPlayer(), GetMyPlayer()->GetPos(),false);
		GetMyPlayer()->SetNeedUpdate(true);
	}

	m_pSystem->GetIInput()->Update(1);	// flush the keyboard buffers
	m_pSystem->GetIInput()->Update(1);	// flush the keyboard buffers
	m_pSystem->GetIInput()->GetIKeyboard()->ClearKeyState();
}

//////////////////////////////////////////////////////////////////////////
bool CXGame::ConstrainToSandbox(IEntity *pEntity)
{
	bool bPosChanged = false;
	if (IsMultiplayer())
	{
		Vec3 vPos, vBounds[2]={Vec3(0,0,0),Vec3(0,0,0)};
		IPhysicalEntity *pPhysEnt = pEntity->GetPhysics();
		if (pPhysEnt)
		{
			pe_status_pos sp;
			pPhysEnt->GetStatus(&sp);
			vPos = sp.pos;
		}
		else
			vPos=pEntity->GetPos();
		
		vBounds[1].x = vBounds[1].y = (float)m_p3DEngine->GetTerrainSize();
		vBounds[0].z = -100; vBounds[1].z = 500;
		for(int i=0;i<3;i++) 
			if (!inrange(vPos[i], vBounds[0][i],vBounds[1][i]))
			{
				vPos[i] = vBounds[isneg((vBounds[0][i]+vBounds[1][i])*0.5f-vPos[i])][i];
				bPosChanged = true;
			}

		if (bPosChanged)
		{
			if (pPhysEnt)
			{
				pe_params_pos pp;
				pp.pos = vPos;
				pPhysEnt->SetParams(&pp);
			}
			else
				pEntity->SetPos(vPos);
		}
	}
	return bPosChanged;
}

//////////////////////////////////////////////////////////////////////////
void CXGame::GotoMenu(bool bTriggerOnSwitch)
{
	if(m_bEditor)
		return;

	if(!m_pUISystem)			// e.g. dedicated server
		return;

	if (!IsInMenu())
	{
		DeleteMessage("Switch");
		SendMessage("Switch");
	}
	else if (bTriggerOnSwitch)
	{
		m_pUISystem->GetScriptObjectUI()->OnSwitch(1);
	}
}

//////////////////////////////////////////////////////////////////////////
void CXGame::GotoGame(bool bTriggerOnSwitch)
{
	if (m_bEditor)
	{
		return;
	}

	if (IsInMenu())
	{
		DeleteMessage("Switch");
		SendMessage("Switch");
	}
	else if (bTriggerOnSwitch)
	{
		m_pUISystem->GetScriptObjectUI()->OnSwitch(0);
	}
}

//////////////////////////////////////////////////////////////////////////
void CXGame::MenuOn()
{
	ISoundSystem* snd = m_pSystem->GetISoundSystem();
	// stop sounds and timers affected by game pause
	if (snd)
	{
		snd->Pause(true,true);
	}

	m_pScriptTimerMgr->Pause(true);

	if (m_pSystem->GetIMusicSystem())
	{
		m_pSystem->GetIMusicSystem()->Pause(true);
		//m_pSystem->GetIMusicSystem()->Silence();
	}

	if (m_pUISystem && m_pUISystem->IsEnabled())
	{
		m_pSystem->GetIInput()->AddEventListener(m_pUISystem);
		m_pSystem->GetIInput()->ClearKeyState();
		m_pUISystem->GetScriptObjectUI()->OnSwitch(1);
	}

	m_bMenuOverlay = 1;

	if (!m_bEditor)
	{
		if (m_pUISystem->GetScriptObjectUI()->CanRenderGame())
		{
			m_pSystem->GetILog()->Log("UISystem: Enabled 3D Engine!");
			m_p3DEngine->Enable(1);
		}
		else
		{
			m_pSystem->GetILog()->Log("UISystem: Disabled 3D Engine!");
			m_p3DEngine->Enable(0);
		}
	}
	
	_SmartScriptObject pClientStuff(m_pScriptSystem, true);

	if(m_pScriptSystem->GetGlobalValue("ClientStuff",pClientStuff))
	{
		m_pScriptSystem->BeginCall("ClientStuff","OnPauseGame");
		m_pScriptSystem->PushFuncParam(pClientStuff);
		m_pScriptSystem->EndCall();
	}
}

//////////////////////////////////////////////////////////////////////////
void CXGame::MenuOff()
{
	// resume sounds and timers affected by game pause
	ISoundSystem* snd = m_pSystem->GetISoundSystem();
	IMusicSystem* mus = m_pSystem->GetIMusicSystem();
	if (snd)
	{
		snd->Pause(false);
	}
	if (mus)
	{
		mus->Pause(false);
	}

	m_pScriptTimerMgr->Pause(false);


	if (m_pUISystem && m_pUISystem->IsEnabled())
	{
		m_pSystem->GetIInput()->RemoveEventListener(m_pUISystem);
		m_pSystem->GetIInput()->ClearKeyState();
		m_pUISystem->GetScriptObjectUI()->OnSwitch(0);

		if (GetMyPlayer())
			m_XAreaMgr.ReTriggerArea(GetMyPlayer(), GetMyPlayer()->GetPos(),false);
		//					m_XAreaMgr.ReTriggerArea(GetMyPlayer(), m_pSystem->GetISoundSystem()->GetListenerPos(),false);
	}

	m_bMenuOverlay = 0;

	m_pSystem->SetIProcess(m_p3DEngine);
	m_pSystem->GetIProcess()->SetFlags(PROC_3DENGINE);

	if (!m_bEditor)
	{
		m_pSystem->GetILog()->Log("UISystem: Enabled 3D Engine!");
		m_p3DEngine->Enable(1);
	}

	_SmartScriptObject pClientStuff(m_pScriptSystem, true);

	if(m_pScriptSystem->GetGlobalValue("ClientStuff",pClientStuff))
	{
		m_pScriptSystem->BeginCall("ClientStuff","OnResumeGame");
		m_pScriptSystem->PushFuncParam(pClientStuff);
		m_pScriptSystem->EndCall();
	}
}

//////////////////////////////////////////////////////////////////////////
void CXGame::DeleteMessage(const char *szMessage)
{
	StringQueue NewQueue;

	while(!m_qMessages.empty())
	{
		string qMessage = m_qMessages.front();

		m_qMessages.pop();

		if (qMessage != szMessage)
		{
			NewQueue.push(qMessage);
		}
	}

	m_qMessages = NewQueue;
}

void CXGame::DeleteStartLevelMessages(void)
{
	StringQueue NewQueue;

	while(!m_qMessages.empty())
	{
		string qMessage = m_qMessages.front();

		m_qMessages.pop();

		if (strnicmp(qMessage.c_str(), "StartLevel", 10))
		{
			NewQueue.push(qMessage);
		}
	}

	m_qMessages = NewQueue;
}

//////////////////////////////////////////////////////////////////////////
IScriptObject *CXGame::GetScriptObject()
{
	if (!m_pScriptObjectGame)
	{
		return 0;
	}
	
	return m_pScriptObjectGame->GetScriptObject();
}


void CXGame::PlaySubtitle(ISound * pSound)
{
	if (m_pMovieUser)
		m_pMovieUser->PlaySubtitles(pSound);
}

vector2f CXGame::GetSubtitleSize(const string &szMessage, float sizex, float sizey, const string &szFontName, const string &szFontEffect)
{
	IFFont *pFont = m_pSystem->GetICryFont()->GetFont(szFontName.c_str());

	pFont->Reset();
	pFont->SetEffect(szFontEffect.c_str());
	pFont->SetSize(vector2f(sizex, sizey));

	wstring szwString;

	m_StringTableMgr.Localize(szMessage, szwString);

	return pFont->GetTextSizeW(szwString.c_str());
}

void CXGame::WriteSubtitle(const string &szMessage, float x, float y, float sizex, float sizey, const color4f &cColor, const string &szFontName, const string &szFontEffect)
{
	IFFont *pFont = m_pSystem->GetICryFont()->GetFont(szFontName.c_str());

	pFont->Reset();
	pFont->SetEffect(szFontEffect.c_str());
	pFont->SetSize(vector2f(sizex, sizey));
	pFont->SetColor(cColor);

	wstring szwString;

	m_StringTableMgr.Localize(szMessage, szwString);

	pFont->DrawStringW(x, y, szwString.c_str());
}

//////////////////////////////////////////////////////////////////////////
IGameMods* CXGame::GetModsInterface()
{
	return m_pGameMods;	
};

//////////////////////////////////////////////////////////////////////////
void CXGame::LoadingError(const char *szError)
{
	m_pRenderer->ClearColorBuffer(Vec3(0,0,0));
	GetSystem()->GetIConsole()->ResetProgressBar(0);
	m_pSystem->GetIConsole()->ShowConsole(false);
	m_pSystem->GetIConsole()->SetScrollMax(600/2);

	m_pScriptSystem->BeginCall("Game", "OnLoadingError");
	m_pScriptSystem->PushFuncParam(GetScriptObject());
	m_pScriptSystem->PushFuncParam(szError);
	m_pScriptSystem->EndCall();
}
//------------------------------------------------------------------------------------------------- 
bool CXGame::GetCDPath(string &szCDPath)
{
	bool bRet( false );
#ifdef WIN32
	DWORD nBufferSize( GetLogicalDriveStrings( 0, 0 ) );
	if( 0 < nBufferSize )
	{
		// get list of all available logical drives
		tvector rawDriveLetters( nBufferSize + 1 );
		GetLogicalDriveStrings( nBufferSize, &rawDriveLetters[ 0 ] );

		// quickly scan all drives
		tvector::size_type i( 0 );
		while( true )
		{
			// check if current drive is cd/dvd drive
			if( DRIVE_CDROM == GetDriveType( &rawDriveLetters[ i ] ) )
			{
				// get volume name
				tvector cdVolumeName( MAX_VOLUME_ID_SIZE + 1 );
				if( FALSE != GetVolumeInformation( &rawDriveLetters[ i ],
					&cdVolumeName[ 0 ], (DWORD) cdVolumeName.size(), 0, 0, 0, 0, 0 ) )
				{
					// check volume name to verify it's Far Cry's game cd/dvd
					tstring cdVolumeLabel( &cdVolumeName[ 0 ] );
					tstring farCryDisk1Label( _T( "FARCRY_1" ) );
					if( cdVolumeLabel == farCryDisk1Label  )
					{
						// found Far Cry's game cd/dvd, copy information and bail out
						szCDPath = &rawDriveLetters[ i ];
						bRet = true;
						break;
					}
				}
			}

			// proceed to next drive
			while( 0 != rawDriveLetters[ i ] )
			{
				++i;
			}
			++i; // skip null termination of current drive

			// check if we're out of drive letters
			if( 0 == rawDriveLetters[ i ] )
			{
				// double null termination found, bail out
				break;
			}
		}
	}
#endif
	return( bRet );
}

ITagPointManager* CXGame::GetTagPointManager()
{
	return m_pTagPointManager;
}

#ifdef __ANDROID__
// First run: copy the game folder's Profiles/Player (default profile, its checkpoint saves and
// cfgs) into the user folder, so the profile screen has something to show.
static void CopyTree(const string& sSrc, const string& sDst)
{
	mkdir(sDst.c_str(), 0755);
	DIR* pDir = opendir(sSrc.c_str());
	if (!pDir)
		return;
	while (struct dirent* d = readdir(pDir))
	{
		if (d->d_name[0] == '.')
			continue;
		string sFrom = sSrc + "/" + d->d_name, sTo = sDst + "/" + d->d_name;
		// FUSE storage reports DT_UNKNOWN, so ask stat.
		struct stat st;
		if (d->d_type == DT_DIR || (d->d_type == DT_UNKNOWN && stat(sFrom.c_str(), &st) == 0 && S_ISDIR(st.st_mode)))
		{
			CopyTree(sFrom, sTo);
			continue;
		}
		FILE* fIn = fopen(sFrom.c_str(), "rb");
		if (!fIn)
			continue;
		if (FILE* fOut = fopen(sTo.c_str(), "wb"))
		{
			char buf[65536];
			size_t n;
			while ((n = fread(buf, 1, sizeof(buf), fIn)) > 0)
				fwrite(buf, 1, n, fOut);
			fclose(fOut);
		}
		fclose(fIn);
	}
	closedir(pDir);
}
#endif

string CXGame::GetPlayerProfilePath()
{
#ifdef __ANDROID__
	static string sProfiles;
	if (sProfiles.empty())
	{
		char szPath[1024];
		CryUserFile("Profiles/Player/", szPath, sizeof(szPath));
		sProfiles = szPath;
		m_pSystem->GetIPak()->MakeDir(szPath); // nested; CopyTree's mkdir is not
		struct stat st;
		char szSrc[1024];
		// The game root when the launcher gave one (secondary storage: no cwd to resolve against, and
		// both storages are case-insensitive), else a case-corrected path from the cwd.
		bool bSrc = CryGameRoot() ? (snprintf(szSrc, sizeof(szSrc), "%s/Profiles/Player", CryGameRoot()), stat(szSrc, &st) == 0)
		                          : casepath("Profiles/Player", szSrc) != 0;
		if (stat((sProfiles + "default").c_str(), &st) != 0 && bSrc)
		{
			printf("Seeding player profiles from %s\n", szSrc);
			CopyTree(szSrc, sProfiles.substr(0, sProfiles.length() - 1));
		}
	}
	return sProfiles;
#elif defined(LINUX)
	DIR *fdir;
	int found_profiles = 0;
	int found_player = 0;
	struct dirent *d;
	string ret;

	fdir = opendir(".");
	if (fdir == NULL)
	{
		__builtin_trap();
		closedir(fdir);
		return "";
	}

	while ((d = readdir(fdir)) != NULL)
	{
		if ((d->d_type == DT_DIR || d->d_type == DT_LNK) && !strcasecmp(d->d_name, "profiles"))
		{
			found_profiles = 1;
			ret += d->d_name;
			ret += "/";
			break;
		}
	}

	closedir(fdir);
	if (!found_profiles)
	{
		__builtin_trap();
		return "";
	}

	fdir = opendir(ret.c_str());

	if (fdir == NULL)
	{
		__builtin_trap();
		closedir(fdir);
		return "";
	}

	while ((d = readdir(fdir)) != NULL)
	{
		if ((d->d_type == DT_DIR || d->d_type == DT_LNK) && !strcasecmp(d->d_name, "player"))
		{
			found_player = 1;
			ret += d->d_name;
			ret += "/";
			break;
		}
	}

	if (!found_player)
	{
		__builtin_trap();
		return "";
	}

	return ret;
#else
	return "Profiles/Player/";
#endif
}