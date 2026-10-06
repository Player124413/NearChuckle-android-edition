
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

#ifdef __ANDROID__
	// Never create or save system.cfg on Android as it corrupts mobile configuration
	// Delete any existing system.cfg files
	remove("system.cfg");
	remove("System.cfg");
	remove("SYSTEM.CFG");
	remove("SystemCfgOverride.Cfg");
	remove("systemcfgoverride.cfg");

	// Save game.cfg only (keybindings, sensitivity)
	ICVar *pProfile=m_pConsole->GetCVar("g_playerprofile");
	if (pProfile)
	{	
		const char *sProfileName=pProfile->GetString();
		m_pGame->SaveConfiguration( "","game.cfg",sProfileName);
	}
	m_pGame->SaveConfiguration( "","game.cfg",NULL);
	return;
#else
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
#endif
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

#ifdef __ANDROID__
	// Completely ignore and delete system.cfg / systemcfgoverride.cfg
	string sLowerPath = m_strSysConfigFilePath;
	std::transform(sLowerPath.begin(), sLowerPath.end(), sLowerPath.begin(), ::tolower);
	if (sLowerPath.find("system.cfg") != string::npos || sLowerPath.find("systemcfgoverride.cfg") != string::npos)
	{
		remove(m_strSysConfigFilePath.c_str());
		return;
	}
#endif

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

#ifdef __ANDROID__
						// Prevent desktop PC settings in system.cfg from breaking mobile environment:
						if (strcasecmp(strKey.c_str(), "r_Driver") == 0)
						{
							m_pSystem->GetILog()->Log("Android: ignoring system.cfg '%s'='%s' (keeping OpenGL)", strKey.c_str(), strValue.c_str());
							continue;
						}
						if (strcasecmp(strKey.c_str(), "r_Width") == 0 || strcasecmp(strKey.c_str(), "r_Height") == 0)
						{
							m_pSystem->GetILog()->Log("Android: ignoring system.cfg '%s'='%s' (using native window size)", strKey.c_str(), strValue.c_str());
							continue;
						}
						if (strcasecmp(strKey.c_str(), "r_Fullscreen") == 0)
						{
							continue;
						}
						if (strcasecmp(strKey.c_str(), "r_NoPS20") == 0 && atoi(strValue.c_str()) != 0)
						{
							m_pSystem->GetILog()->Log("Android: ignoring system.cfg r_NoPS20=1 (must remain 0)");
							continue;
						}
						if (strcasecmp(strKey.c_str(), "r_Quality_BumpMapping") == 0 && atoi(strValue.c_str()) < 3)
						{
							m_pSystem->GetILog()->Log("Android: overriding system.cfg r_Quality_BumpMapping to 3");
							strValue = "3";
						}
						if (strcasecmp(strKey.c_str(), "r_GL_NV30_PS20") == 0 && atoi(strValue.c_str()) == 0)
						{
							continue;
						}
#endif
						
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

//////////////////////////////////////////////////////////////////////////
void CSystem::LoadConfiguration(const string &sFilename)
{
	if (!sFilename.empty())
	{	
#ifdef __ANDROID__
		string sLower = sFilename;
		std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
		if (sLower.find("system.cfg") != string::npos || sLower.find("systemcfgoverride.cfg") != string::npos)
		{
			if (m_pLog) m_pLog->Log("Android: ignoring and removing configuration file '%s'", sFilename.c_str());
			remove(sFilename.c_str());
			return;
		}
#endif
		//m_pScriptSystem->ExecuteFile(sFilename.c_str(),false);
		m_pLog->Log("Loading system configuration");
#ifdef __ANDROID__
		// The saved system.cfg lives in the user folder; the game folder's copy is only the seed.
		string sPath = sFilename;
		char szBuf[1024];
		if (!strchr(sFilename.c_str(), '/') && access(CryUserFile(sFilename.c_str(), szBuf, sizeof(szBuf)), R_OK) == 0)
			sPath = szBuf;
		CSystemConfiguration tempConfig(sPath,this);
#else
		CSystemConfiguration tempConfig(sFilename,this);
#endif
	}
}
