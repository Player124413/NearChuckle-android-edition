#include <list>
#include "StdAfx.h"
#include "ISound.h"
#include "UIVideoBinkDec.h"
#include <BinkDecoder.h>

enum EPlayerCmd : int
{
	PLAYER_CMD_NONE = 0,
	PLAYER_CMD_PLAYING,
	PLAYER_CMD_REWIND,
	PLAYER_CMD_STOP,
};

struct MoviePlayerData
{
	BinkHandle binkHandle;
	YUVbuffer yuvBuffer;
	bool hasFrame;
	int	framePos;
	int	lastFramePos;
	int	numFrames;
	uint32_t numAudioTracks;
	uint32_t trackIndex;
	AudioInfo binkInfo;
	bool looping;
	int startTime;
	float frameRate;
	unsigned int vidWidth;
	unsigned int vidHeight;
};

static MoviePlayerData* CreatePlayerData(const char* filename)
{
	MoviePlayerData* player = new MoviePlayerData();
	uint32_t w = 0, h = 0;
	ILog* iLog = GetISystem()->GetILog();
	ICryPak* iPak = GetISystem()->GetIPak();
	char* corrected = (char*)alloca(strlen(filename) + 3);
	player->looping = 0;
	if (casepath(filename, corrected))
	{
		player->binkHandle = Bink_Open( corrected );
		if( !player->binkHandle.isValid )
		{
			iLog->LogError("Failed to open video file %s", filename);
			return nullptr;
		}
	}
	else
	{
		player->binkHandle = Bink_Open( filename );
		if( !player->binkHandle.isValid )
		{
			iLog->LogError("Failed to open video file %s", filename);
			return nullptr;
		}
	}

	Bink_GetFrameSize( player->binkHandle, w, h );
	player->vidWidth = w;
	player->vidHeight = h;

	player->frameRate = Bink_GetFrameRate(player->binkHandle);
	player->numFrames = Bink_GetNumFrames(player->binkHandle);
	float durationSec = player->numFrames / player->frameRate;
	int animationLength = durationSec * 1000;

	player->framePos = -1;
	player-> lastFramePos = -1; 

	return player;
}

CUIVideoBinkDecoder::~CUIVideoBinkDecoder()
{
	Terminate();
}

CUIVideoBinkDecoder::CUIVideoBinkDecoder(const char* aliasName)
{
	m_aliasName = aliasName;
}

signed char BinkDecAudioCallback(CS_STREAM* pStream, void* pBuffer, int nLength, void* nParam)
{
	MoviePlayerData* player = (MoviePlayerData*)nParam;
	int16_t* audioBuffer = (int16_t*)pBuffer;
	memset(audioBuffer, -1, nLength);
	if (!player)
	{
		return 0;
	}
	if (player->framePos < 0)
	{
		return 0;
	}
	if (player->lastFramePos == player->framePos)
	{
		return 0;
	}

	Bink_GetAudioData(player->binkHandle, player->trackIndex, audioBuffer);
	return 1;
}

bool CUIVideoBinkDecoder::Init(const char* pathToVideo, bool needSound)
{
	const char* nameOfPlayer = m_aliasName.length() ? m_aliasName.c_str() : pathToVideo;
	unsigned int w, h;

	m_player = CreatePlayerData(pathToVideo);
	if (m_player)
	{
		Bink_GetFrameSize(m_player->binkHandle, w, h);
	
		m_frameBuffer = new uint8[w * h * 4];
		memset(m_frameBuffer, 0, w * h * 4);
		m_textureId = GetISystem()->GetIRenderer()->DownLoadToVideoMemory(m_frameBuffer,
			w, h, eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, nullptr, FT_DYNAMIC);
	
		if (m_textureId < 0)
		{
			__builtin_trap();
		}

		if (needSound)
		{
			m_player->numAudioTracks = Bink_GetNumAudioTracks(m_player->binkHandle);

			if(m_player->numAudioTracks > 0)
			{
				m_player->trackIndex = 0;
				m_player->binkInfo = Bink_GetAudioTrackDetails(m_player->binkHandle, m_player->trackIndex);
				m_audioStream = CS_Stream_Create(BinkDecAudioCallback,
					m_player->binkInfo.idealBufferSize, 0,
					m_player->binkInfo.sampleRate, m_player);
			}
		}
	}

	return m_player != nullptr;
}

void CUIVideoBinkDecoder::Terminate()
{
	Stop();

	if (m_player)
	{
		Bink_Close(m_player->binkHandle);
	}
	SAFE_DELETE(m_player);
	SAFE_DELETE_ARRAY(m_frameBuffer);
	
	if (m_audioStream)
	{
		CS_Stream_Close(m_audioStream);
	}
	m_audioStream = nullptr;

	if (m_textureId > -1)
	{
		GetISystem()->GetIRenderer()->RemoveTexture(m_textureId);
		m_textureId = -1;
	}
}

void CUIVideoBinkDecoder::Start()
{
	if (!m_player)
	{
		return;
	}
	m_player->startTime = SDL_GetTicks();

	m_playerCmd = PLAYER_CMD_PLAYING;

	if(m_audioStream)
	{
		CS_Stream_Play(CS_FREE, m_audioStream);
	}
}

void CUIVideoBinkDecoder::Stop()
{
	if (!m_player)
		return;

	m_playerCmd = PLAYER_CMD_STOP;

	if (m_audioStream)
		CS_Stream_Stop(m_audioStream);
}

void CUIVideoBinkDecoder::Rewind()
{
	m_playerCmd = PLAYER_CMD_REWIND;
}

bool CUIVideoBinkDecoder::IsPlaying() const
{
	return m_playerCmd != PLAYER_CMD_NONE;
}

void CUIVideoBinkDecoder::BinkDecReset()
{
	m_player->framePos = -1;
	m_player->lastFramePos = -1;

	Bink_GotoFrame( m_player->binkHandle, 0 );
}

void CUIVideoBinkDecoder::DrawYUV(void)
{
	int i, j, k, si, sj;
	MoviePlayerData* player = m_player;
	uint8_t Y, U, V;
	float R, G, B;

	for (i = k = 0; i < player->vidHeight; i++)
	{
		for (j = 0; j < player->vidWidth; j++)
		{
			Y = player->yuvBuffer[0].data[(i * player->yuvBuffer[0].pitch) + j];
			si = (i % 2 == 0) ? i / 2 : (i - 1) / 2;
			sj = (j % 2 == 0) ? j / 2 : (j - 1) / 2;

			U = player->yuvBuffer[1].data[si * player->yuvBuffer[1].pitch + sj];
			V = player->yuvBuffer[2].data[si * player->yuvBuffer[2].pitch + sj];

			R = (float)Y + 1.4075f * ((float)V - 128.0f);
			G = (float)Y - 0.3455f * ((float)U - 128.0f) - 0.7169f * ((float)V - 128.0f);
			B = (float)Y + 1.7790f * ((float)U - 128.0f);

			m_frameBuffer[(i * player->yuvBuffer[0].pitch) + j + k] = (uint8_t)B;
			m_frameBuffer[(i * player->yuvBuffer[0].pitch) + j + k + 1] = (uint8_t)G;
			m_frameBuffer[(i * player->yuvBuffer[0].pitch) + j + k + 2] = (uint8_t)R;
			m_frameBuffer[(i * player->yuvBuffer[0].pitch) + j + k + 3] = 255;

			k += 3;
		}
	}
}

void CUIVideoBinkDecoder::Present()
{
	MoviePlayerData* player = m_player;
	int thisTime = SDL_GetTicks();
	int desiredFrame;

	if (!player)
	{
		return;
	}

	if( !player->binkHandle.isValid )
	{
		return;
	}

	if((!player->hasFrame) || player->startTime == -1)
	{
		if( player->startTime == -1 )
		{
			BinkDecReset();
		}
		player->startTime = thisTime;
	}

	desiredFrame = ((thisTime - player->startTime) * player->frameRate) / 1000.0f;

	if(desiredFrame < 0)
	{
		desiredFrame = 0;
	}

	if(desiredFrame < player->framePos)
	{
		BinkDecReset();
		player->hasFrame = false;
	}

	if( desiredFrame >= player->numFrames )
	{
		//end of video
		if( player->looping )
		{
			desiredFrame = 0;
			BinkDecReset();
			player->hasFrame = false;
			player->startTime = thisTime;
			m_playerCmd = PLAYER_CMD_PLAYING;
		}
		else
		{
			player->hasFrame = false;
			m_playerCmd = PLAYER_CMD_NONE; //?
			return;
		}
	}

	while(player->framePos < desiredFrame)
	{
		player->framePos = Bink_GetNextFrame(player->binkHandle, player->yuvBuffer);
	}

	DrawYUV();

	if (m_audioStream)
	{
		CS_Update();
	}

	player->lastFramePos = player->framePos;

	GetISystem()->GetIRenderer()->UpdateTextureInVideoMemory(m_textureId,
		m_frameBuffer, 0, 0, player->vidWidth, player->vidHeight, eTF_8888);

	player->hasFrame = true;
}

void CUIVideoBinkDecoder::SetTimeScale(float value)
{
	//STUB
}

int CUIVideoBinkDecoder::GetTextureId() const
{
	return m_textureId;
}

int	CUIVideoBinkDecoder::GetWidth() const
{
	if (!m_player)
	{
		return 1;
	}
	return m_player->vidWidth;
}

int	CUIVideoBinkDecoder::GetHeight() const
{
	if (!m_player)
	{
		return 1;
	}
	return m_player->vidHeight;
}