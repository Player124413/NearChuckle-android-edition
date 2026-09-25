#pragma once
#include <functional>
#include "platform.h"
#include "ISound.h"

struct MoviePlayerData;
struct ISoundStream;

class CUIVideoBinkDecoder
{
public:

	CUIVideoBinkDecoder() = default;
	CUIVideoBinkDecoder(const char* aliasName);
	~CUIVideoBinkDecoder();

	bool				Init(const char* pathToVideo, bool needSound);
	void				Terminate();

	void				Start();
	void				Stop();
	void				Rewind();

	bool				IsPlaying() const;

	// if frame is decoded this will update texture image
	void				Present();
	int					GetTextureId() const;
	int					GetWidth() const;
	int					GetHeight() const;

	void				SetTimeScale(float value);

protected:
	void BinkDecReset(void);
	void DrawYUV(void);

	string				m_aliasName;

	uint8*				m_frameBuffer{ nullptr };
	MoviePlayerData*	m_player{ nullptr };
	CS_STREAM*	m_audioStream = nullptr;
	int					m_playerCmd{ 0 };
	int					m_textureId{ -1 };
};