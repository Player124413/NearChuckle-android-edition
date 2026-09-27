
//////////////////////////////////////////////////////////////////////
//
//	Crytek CryENGINE Source code
// 
//	File: SystemCFG.cpp
//  Description: handles system cfg
// 
//	History:
//	-Jan 21,2004: created
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "System.h"
#include <time.h>
#include "XConsole.h"
#include <IGame.h>
#include <IScriptSystem.h>
#include "SystemCFG.h"
#if defined(LINUX)
#include "ILog.h"
#ifndef VERSION_INFO
#define VERSION_INFO 1
#endif
#endif

//////////////////////////////////////////////////////////////////////////
const SFileVersion& CSystem::GetFileVersion()
{
	return m_fileVersion;
}

//////////////////////////////////////////////////////////////////////////
const SFileVersion& CSystem::GetProductVersion()
{
	return m_productVersion;
}

//////////////////////////////////////////////////////////////////////////
void CSystem::QueryVersionInfo()   
{
#if defined(LINUX)
		//do we need some other values here?
		m_fileVersion.v[0] = VERSION_INFO; 
		m_fileVersion.v[1] = 1;
		m_fileVersion.v[2] = 1;
		m_fileVersion.v[3] = 1;
 
		m_productVersion.v[0] = VERSION_INFO;
		m_productVersion.v[1] = 1;
		m_productVersion.v[2] = 1;
		m_productVersion.v[3] = 1;
#else 
	char moduleName[_MAX_PATH];
	DWORD dwHandle;
	UINT len;

	char ver[1024*8];

//	GetModuleFileName( NULL, moduleName, _MAX_PATH );//retrieves the PATH for the current module
	strcpy(moduleName,"CrySystem.dll");	// we want to version from the system dll (FarCry.exe we cannot change because of CopyProtection)

	int verSize = GetFileVersionInfoSize( moduleName,&dwHandle );
	if (verSize > 0)
	{
		GetFileVersionInfo( moduleName,dwHandle,1024*8,ver );
		VS_FIXEDFILEINFO *vinfo;
		VerQueryValue( ver,"\\",(void**)&vinfo,&len );

		m_fileVersion.v[0] = vinfo->dwFileVersionLS & 0xFFFF;
		m_fileVersion.v[1] = vinfo->dwFileVersionLS >> 16;
		m_fileVersion.v[2] = vinfo->dwFileVersionMS & 0xFFFF;
		m_fileVersion.v[3] = vinfo->dwFileVersionMS >> 16;

		m_productVersion.v[0] = vinfo->dwProductVersionLS & 0xFFFF;
		m_productVersion.v[1] = vinfo->dwProductVersionLS >> 16;
		m_productVersion.v[2] = vinfo->dwProductVersionMS & 0xFFFF;
		m_productVersion.v[3] = vinfo->dwProductVersionMS >> 16;
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
void CSystem::LogVersion()
{
	//! Get time.
	time_t ltime;
	time( &ltime );
	tm *today = localtime( &ltime );

	char s[1024];
	//! Use strftime to build a customized time string.
	//strftime( timebuf,128,"Logged at %A, %B %d,%Y\n\n", today );
	strftime( s,128,"Log Started at %#c", today );
	CryLogAlways( s );
	CryLogAlways( "FileVersion: %d.%d.%d.%d",m_fileVersion.v[3],m_fileVersion.v[2],m_fileVersion.v[1],m_fileVersion.v[0] );
	CryLogAlways( "ProductVersion: %d.%d.%d.%d",m_productVersion.v[3],m_productVersion.v[2],m_productVersion.v[1],m_productVersion.v[0] );
	CryLogAlways( "" );
}

//////////////////////////////////////////////////////////////////////////
void CSystem::SaveConfiguration()
{
	// save config before we quit
	if (!m_pGame)
		return;

	string sSave=m_rDriver->GetString();
	if(m_sSavedRDriver!="")
		m_rDriver->Set(m_sSavedRDriver.c_str());

	// get player's profile
	ICVar *pProfile=m_pConsole->GetCVar("g_playerprofile");
	if (pProfile)
	{	
		const char *sProfileName=pProfile->GetString();
		m_pGame->SaveConfiguration( "system.cfg","game.cfg",sProfileName);
	}
	// always save the current profile in the root, otherwise, nexttime, the game will have the default one
	// wich is annoying
	m_pGame->SaveConfiguration( "system.cfg","game.cfg",NULL);

	m_rDriver->Set(sSave.c_str());
}

//////////////////////////////////////////////////////////////////////////
ESystemConfigSpec CSystem::GetConfigSpec()
{
	if (m_sys_spec)
		return (ESystemConfigSpec)m_sys_spec->GetIVal();
	return CONFIG_VERYHIGH_SPEC; // highest spec.
}

//////////////////////////////////////////////////////////////////////////
// system cfg
//////////////////////////////////////////////////////////////////////////
CSystemConfiguration::CSystemConfiguration(const string& strSysConfigFilePath,CSystem *pSystem)
: m_strSysConfigFilePath( strSysConfigFilePath )
//, m_colCCVars()
{
	m_pSystem=pSystem;
	ParseSystemConfig();
}

//////////////////////////////////////////////////////////////////////////
CSystemConfiguration::~CSystemConfiguration()
{
}

//////////////////////////////////////////////////////////////////////////
void CSystemConfiguration::ParseSystemConfig()
{
	//m_pScriptSystem->ExecuteFile(sFilename.c_str(),false);

	FILE *pFile=fxopen(m_strSysConfigFilePath.c_str(), "rb");
	if (!pFile)
		return;
	
	char szLine[512];
	char szBuffer[512];
	while (fgets(szLine,512,pFile))
	{			
		string strLine(szLine);

		// skip comments
		if (0<strLine.find( "--" ))
		{
			// extract key
			string::size_type posEq( strLine.find( "=", 0 ) );
			if (string::npos!=posEq)
			{
#if defined(LINUX)	
				string s( strLine, 0, posEq );
				string strKey( RemoveWhiteSpaces(s) );
#else
				string strKey( RemoveWhiteSpaces( string( strLine, 0, posEq ) ) );
#endif
				if (!strKey.empty())
				{
						// extract value
					string::size_type posValueStart( strLine.find( "\"", posEq + 1 ) + 1 );
					string::size_type posValueEnd( strLine.find( "\"", posValueStart ) );
					
					if( string::npos != posValueStart && string::npos != posValueEnd )
					{
						string strValue( strLine, posValueStart, posValueEnd - posValueStart );						
						
						ICVar *pCvar=m_pSystem->GetIConsole()->GetCVar(strKey.c_str(),false);		// false=not case sensitive (slow but more convenient)
						if (pCvar)
						{
							m_pSystem->GetILog()->Log("Setting %s to %s",strKey.c_str(),strValue.c_str());
							pCvar->Set(strValue.c_str());
						}
						else
						{
							if (strstr(strKey.c_str(),"#") || strstr(strValue.c_str(),"#"))
							{
								m_pSystem->GetILog()->Log("Invalid buffer (%s,%s)",strKey.c_str(),strValue.c_str());								
							}
							else
							{								
								m_pSystem->GetILog()->Log("Lua cvar: (%s,%s)",strKey.c_str(),strValue.c_str());								
								sprintf(szBuffer,"%s = \"%s\"",strKey.c_str(),strValue.c_str());
								m_pSystem->GetIScriptSystem()->ExecuteBuffer(szBuffer,strlen(szBuffer));
							}
						}
					}
				}					
			}
		} //--
	} // while fgets

	fclose(pFile);
}

#ifdef __ANDROID__
// The Advanced Video menu's High preset (Scripts/MenuScreens/Options/VideoAdv.lua). The engine defaults
// match no preset, so a first run would show most options as "Custom".
static const char* s_szHighSpec[][2] = {
  { "ca_EnableDecals", "1" }, { "ca_ambient_light_intensity", "0.2" }, { "ca_ambient_light_range", "10" },
  { "cl_projectile_light", "1" }, { "cl_weapon_light", "1" }, { "e_EntitySuppressionLevel", "0" },
  { "e_active_shadow_maps_receving", "1" }, { "e_beach", "1" }, { "e_cgf_load_lods", "1" },
  { "e_decals", "1" }, { "e_decals_life_time_scale", "2.0" }, { "e_detail_texture_quality", "1" },
  { "e_flocks", "1" }, { "e_light_maps_quality", "2" }, { "e_max_entity_lights", "3" },
  { "e_obj_lod_ratio", "10" }, { "e_overlay_geometry", "1" }, { "e_particles_lod", "1.0" },
  { "e_particles_max_count", "4096" }, { "e_shadow_maps", "1" }, { "e_shadow_maps_view_dist_ratio", "15" },
  { "e_stencil_shadows", "1" }, { "e_stencil_shadows_only_from_strongest_light", "0" },
  { "e_use_global_fog_in_fog_volumes", "0" }, { "e_vegetation_min_size", "0" },
  { "e_vegetation_sprites_distance_ratio", "1.0" }, { "es_EnableCloth", "1" }, { "p_lightrange", "15" },
  { "r_Beams", "1" }, { "r_CoronaFade", "0.1625" }, { "r_Coronas", "1" }, { "r_CryvisionType", "0" },
  { "r_DetailDistance", "8" }, { "r_DetailNumLayers", "1" }, { "r_DetailTextures", "1" },
  { "r_DisableSfx", "0" }, { "r_EnvCMResolution", "2" }, { "r_EnvCMupdateInterval", "0.1" },
  { "r_EnvLCMupdateInterval", "0.1" }, { "r_EnvLightCMSize", "8" }, { "r_EnvTexResolution", "3" },
  { "r_EnvTexUpdateInterval", "0.05" }, { "r_Flares", "1" }, { "r_Glare", "1" }, { "r_GlareQuality", "2" },
  { "r_HeatHaze", "1" }, { "r_MotionBlur", "1" }, { "r_ProcFlares", "1" }, { "r_Quality_BumpMapping", "2" },
  { "r_Quality_Reflection", "0" }, { "r_ScopeLens_fx", "1" }, { "r_ShadowBlur", "1" },
  { "r_TexBumpResolution", "0" }, { "r_TexResolution", "0" }, { "r_TexSkyResolution", "0" },
  { "r_Vegetation_PerpixelLight", "1" }, { "r_VolumetricFog", "1" }, { "r_WaterReflections", "1" },
  { "r_WaterRefractions", "1" }, { "r_WaterUpdateFactor", "0.01" }, { "r_checkSunVis", "2" },
  { "sys_skiponlowspec", "0" }, { "GL_TextureFilter", "GL_LINEAR_MIPMAP_LINEAR" },
  { "r_Texture_Anisotropic_Level", "1" },
};

// First run: no saved config anywhere, so start from the High preset.
static bool WriteDefaultSystemConfig(const char* szPath)
{
	FILE* f = fopen(szPath, "wb");
	if (!f)
		return false;
	fputs("-- [System-Configuration]\r\n-- First-run defaults: the High preset of the Advanced Video options.\r\n\r\n", f);
	for (size_t i = 0; i < sizeof(s_szHighSpec) / sizeof(s_szHighSpec[0]); i++)
		fprintf(f, "%s = \"%s\"\r\n", s_szHighSpec[i][0], s_szHighSpec[i][1]);
	fclose(f);
	return true;
}
#endif

//////////////////////////////////////////////////////////////////////////
void CSystem::LoadConfiguration(const string &sFilename)
{
	if (!sFilename.empty())
	{	
		//m_pScriptSystem->ExecuteFile(sFilename.c_str(),false);
		m_pLog->Log("Loading system configuration");
#ifdef __ANDROID__
		// The saved system.cfg lives in the user folder; the game folder's copy is only the seed.
		string sPath = sFilename;
		char szBuf[1024];
		if (!strchr(sFilename.c_str(), '/') && access(CryUserFile(sFilename.c_str(), szBuf, sizeof(szBuf)), R_OK) == 0)
			sPath = szBuf;
		else if (sFilename[0] != '/' && CryGameRoot())
		{
			sPath = string(CryGameRoot()) + "/" + sFilename; // no cwd on secondary storage
			FILE* f = fopen(sPath.c_str(), "rb");
			if (f)
				fclose(f);
			else if (sFilename == "system.cfg" && WriteDefaultSystemConfig(CryUserFile(sFilename.c_str(), szBuf, sizeof(szBuf))))
				sPath = szBuf;
		}
		CSystemConfiguration tempConfig(sPath,this);
#else
		CSystemConfiguration tempConfig(sFilename,this);
#endif
	}
}
