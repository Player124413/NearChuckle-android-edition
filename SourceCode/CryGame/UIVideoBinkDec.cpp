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

// BT.601 YUV 4:2:0 to BGRA in 16.16 fixed point (the float per-texel version was most of the menu's frame).
void CUIVideoBinkDecoder::DrawYUV(void)
{
	const MoviePlayerData* player = m_player;
	const int w = player->vidWidth, h = player->vidHeight;
	for (int i = 0; i < h; i++)
	{
		const uint8_t* py = player->yuvBuffer[0].data + i * player->yuvBuffer[0].pitch;
		const uint8_t* pu = player->yuvBuffer[1].data + (i >> 1) * player->yuvBuffer[1].pitch;
		const uint8_t* pv = player->yuvBuffer[2].data + (i >> 1) * player->yuvBuffer[2].pitch;
		uint8_t* out = m_frameBuffer + i * w * 4;
		for (int j = 0; j < w; j++, out += 4)
		{
			const int Y = py[j] << 16, U = pu[j >> 1] - 128, V = pv[j >> 1] - 128;
			const int R = (Y + 92242 * V) >> 16;
			const int G = (Y - 22643 * U - 46983 * V) >> 16;
			const int B = (Y + 116589 * U) >> 16;
			out[0] = (uint8_t)(B < 0 ? 0 : B > 255 ? 255 : B);
			out[1] = (uint8_t)(G < 0 ? 0 : G > 255 ? 255 : G);
			out[2] = (uint8_t)(R < 0 ? 0 : R > 255 ? 255 : R);
			out[3] = 255;
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

	bool bNewFrame = !player->hasFrame;
	while(player->framePos < desiredFrame)
	{
		player->framePos = Bink_GetNextFrame(player->binkHandle, player->yuvBuffer);
		bNewFrame = true;
	}

	// The game renders faster than the video plays: convert and upload only frames that changed.
	if (bNewFrame)
		DrawYUV();

	if (m_audioStream)
	{
		CS_Update();
	}

	player->lastFramePos = player->framePos;

	if (bNewFrame)
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