#ifndef CRY_NO_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
}
#endif // CRY_NO_FFMPEG

#include <list>
#include <sys/time.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include "StdAfx.h"
#include "ICryPak.h"
#include "ISound.h"
#include "UIVideoFFmpeg.h"

class MutexLocker
{
public:
	MutexLocker(SDL_Mutex* mutex)	{ SDL_LockMutex(m_mutex = mutex); }
	~MutexLocker()					{ SDL_UnlockMutex(m_mutex); }
private:
	SDL_Mutex* m_mutex;
};

//-----------------------------------------------

class CTimer
{
public:
	CTimer();

	// returns time in seconds
	double	GetTime(bool reset = false);

	// returns milliseconds
	uint64	GetTimeMS(bool reset = false);

protected:
#ifdef _WIN32
	uint64			m_clockStart;
#else
	timeval			m_timeStart;
#endif // _WIN32
};


//-----------------------------------------------

CTimer::CTimer()
{
#ifdef _WIN32
	QueryPerformanceCounter((LARGE_INTEGER*)&m_clockStart);
#else
	gettimeofday(&m_timeStart, nullptr);
#endif // _WIN32
}

double CTimer::GetTime(bool reset /*= false*/)
{
#ifdef _WIN32
	LARGE_INTEGER curr;
	LARGE_INTEGER performanceFrequency;
	QueryPerformanceFrequency(&performanceFrequency);
	QueryPerformanceCounter(&curr);

	const double value = double(curr.QuadPart - m_clockStart) / double(performanceFrequency.QuadPart);

	if (reset)
		m_clockStart = curr.QuadPart;
#else
	timeval curr;
	gettimeofday(&curr, nullptr);

	double value = (float(curr.tv_sec - m_timeStart.tv_sec) + 0.000001f * float(curr.tv_usec - m_timeStart.tv_usec));

	if (reset)
		m_timeStart = curr;
#endif // _WIN32

	return value;
}


uint64 CTimer::GetTimeMS(bool reset)
{
#ifdef _WIN32
	LARGE_INTEGER curr;
	QueryPerformanceCounter(&curr);
	LARGE_INTEGER performanceFrequency;
	QueryPerformanceFrequency(&performanceFrequency);

	const uint64 value = (curr.QuadPart - m_clockStart * 1000) / performanceFrequency.QuadPart;

	if (reset)
		m_clockStart = curr.QuadPart;
#else
	timeval curr;
	gettimeofday(&curr, nullptr);

	const uint64 value = 1000 * (double(curr.tv_sec - m_timeStart.tv_sec) + 0.000001 * double(curr.tv_usec - m_timeStart.tv_usec));

	if (reset)
		m_timeStart = curr;
#endif // _WIN32

	return value;
}

//-----------------------------------------------

struct AVPacket;
struct AVFrame;
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVBufferRef;
struct SwrContext;
struct SwsContext;

static constexpr const int AV_PACKET_AUDIO_CAPACITY = 40;
static constexpr const int AV_PACKET_VIDEO_CAPACITY = 10;

using APacketQueue = std::list<AVPacket*>;	// AV_PACKET_AUDIO_CAPACITY
using VPacketQueue = std::list<AVPacket*>;	// AV_PACKET_VIDEO_CAPACITY
using FrameQueue = std::list<AVFrame*>;		// AV_PACKET_AUDIO_CAPACITY

enum DecodeState : int
{
	DEC_ERROR = -1,
	DEC_DEQUEUE_PACKET = 0,
	DEC_SEND_PACKET,
	DEC_RECV_FRAME,
	DEC_READY_FRAME,
};

enum EPlayerCmd : int
{
	PLAYER_CMD_NONE = 0,
	PLAYER_CMD_PLAYING,
	PLAYER_CMD_REWIND,
	PLAYER_CMD_STOP,
};

struct MoviePlayerData
{
#ifndef CRY_NO_FFMPEG
	AVPacket			packet;
	FILE*				file;
	AVFormatContext*	formatCtx{ nullptr };
	SDL_Mutex*			audioSourceMutex = nullptr;

	// Video
	struct VideoState
	{
		CTimer		timer;
		int64_t		videoOffset{ 0 };
		int64_t		lastVideoPts{ 0 };
		double		presentationDelay{ 0.0f };

		AVFrame*	presentFrame{ nullptr };

		AVFrame*	frame{ nullptr };
		AVPacket*	deqPacket{ nullptr };
		DecodeState state{ DEC_ERROR };

	} videoState;

	AVStream*		videoStream{ nullptr };
	AVCodecContext* videoCodec{ nullptr };
	SwsContext*		videoSws{ nullptr };
	VPacketQueue	videoPacketQueue;

	// Audio
	struct AudioState
	{
		int64_t		audioOffset{ 0 };
		int64_t		lastAudioPts{ 0 };

		AVFrame*	frame{ nullptr };
		AVPacket*	deqPacket{ nullptr };

		DecodeState state{ DEC_ERROR };
	} audioState;

	AVStream*		audioStream{ nullptr };
	AVCodecContext* audioCodec{ nullptr };
	SwrContext*		audioSwr{ nullptr };
	APacketQueue	audioPacketQueue;

	float			clockSpeed{ 1.0f };
	int64_t			clockStartTime{ AV_NOPTS_VALUE };
#endif // CRY_NO_FFMPEG
};

static int CreateCodec(AVCodecContext** _cc, AVStream* stream, AVBufferRef* hwDeviceCtx)
{
#ifndef CRY_NO_FFMPEG
	const AVCodec* c = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!c)
		return -1;

	AVCodecContext* cc = avcodec_alloc_context3(c);
	if (cc == nullptr)
		return -1;

	int r = avcodec_parameters_to_context(cc, stream->codecpar);
	if (r != 0)
	{
		avcodec_free_context(&cc);
		return -1;
	}

	if (hwDeviceCtx)
		cc->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

	r = avcodec_open2(cc, c, nullptr);
	if (r < 0)
	{
		avcodec_free_context(&cc);
		return -1;
	}

	*_cc = cc;
#endif // CRY_NO_FFMPEG
	return 0;
}

static void FreePlayerData(MoviePlayerData* player)
{
	if (!player)
		return;
#ifndef CRY_NO_FFMPEG
	if (player->formatCtx && player->formatCtx->pb)
	{
		av_free(player->formatCtx->pb->buffer);
		avio_context_free(&player->formatCtx->pb);
	}
	avformat_close_input(&player->formatCtx);

	if (player->videoStream)
	{
		avcodec_free_context(&player->videoCodec);
		sws_freeContext(player->videoSws);

		if (player->videoState.deqPacket)
			av_packet_free(&player->videoState.deqPacket);

		if (player->videoState.frame)
			av_frame_free(&player->videoState.frame);
	}

	if (player->audioStream)
	{
		avcodec_free_context(&player->audioCodec);
		swr_free(&player->audioSwr);

		if (player->audioState.deqPacket)
			av_packet_free(&player->audioState.deqPacket);

		if (player->audioState.frame)
			av_frame_free(&player->audioState.frame);

		player->audioStream = nullptr;
	}
#endif // CRY_NO_FFMPEG
}

static void avDebugLog(void* ptr, int level, const char* fmt, va_list vargs)
{
	ILog* iLog = GetISystem()->GetILog();
	iLog->LogV(IMiniLog::eWarningAlways, fmt, vargs);
}

#ifdef WIN32
#define realpath(N,R) _fullpath((R),(N), _MAX_PATH)
#endif

static MoviePlayerData* CreatePlayerData(AVBufferRef* hw_device_context, const char* filename)
{
	MoviePlayerData* player = new MoviePlayerData();

#ifndef CRY_NO_FFMPEG
	ILog* iLog = GetISystem()->GetILog();
	ICryPak* iPak = GetISystem()->GetIPak();
	char* corrected = (char*)alloca(strlen(filename) + 3);
	if (casepath(filename, corrected))
	{
		if (avformat_open_input(&player->formatCtx, corrected, nullptr, nullptr) < 0)
		{
			iLog->LogError("Failed to open video file %s", corrected);
			FreePlayerData(player);
			return nullptr;
		}
	}
	else
	{
		if (avformat_open_input(&player->formatCtx, filename, nullptr, nullptr) < 0)
		{
			iLog->LogError("Failed to open video file %s", filename);
			FreePlayerData(player);
			return nullptr;
		}
	}

	if (avformat_find_stream_info(player->formatCtx, nullptr) < 0)
	{
		iLog->LogError("Failed to find stream info");
		FreePlayerData(player);
		return nullptr;
	}

	for (uint i = 0; i < player->formatCtx->nb_streams; ++i)
	{
		AVStream* stream = player->formatCtx->streams[i];

		if (!player->videoStream && (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
			player->videoStream = stream;

		if (!player->audioStream && (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))
			player->audioStream = stream;

		if (player->audioStream && player->videoStream)
			break;
	}

	if (player->videoStream == nullptr)
	{
		iLog->LogError("No video/audio supported stream found");
		FreePlayerData(player);
		return nullptr;
	}

	// Setup video
	if (CreateCodec(&player->videoCodec, player->videoStream, hw_device_context) != 0)
	{
		FreePlayerData(player);
		return nullptr;
	}

	player->videoSws = sws_getContext(
		player->videoCodec->width, player->videoCodec->height,
		player->videoCodec->pix_fmt,
		player->videoCodec->width, player->videoCodec->height,
		AV_PIX_FMT_BGRA,
		SWS_POINT,
		nullptr,
		nullptr,
		nullptr);

	// Setup audio
	if (player->audioStream)
	{
		if (CreateCodec(&player->audioCodec, player->audioStream, nullptr) != 0)
		{
			FreePlayerData(player);
			return nullptr;
		}

		AVChannelLayout outChLayout;
		av_channel_layout_default(&outChLayout, 2);

		swr_alloc_set_opts2(&player->audioSwr,
			&outChLayout, AV_SAMPLE_FMT_S16, 44100,
			&player->audioStream->codecpar->ch_layout,
			(enum AVSampleFormat)player->audioStream->codecpar->format,
			player->audioStream->codecpar->sample_rate,
			0, nullptr);

		if (player->audioSwr == nullptr)
		{
			FreePlayerData(player);
			return nullptr;
		}

		if (swr_init(player->audioSwr) < 0)
		{
			iLog->LogError("Unable to create SWResample");
			FreePlayerData(player);
			return nullptr;
		}
	}
#endif // CRY_NO_FFMPEG
	return player;
}

static bool PlayerDemuxStep(MoviePlayerData* player, CUIVideoFFmpeg::FinishFunc& onFinish)
{
#ifndef CRY_NO_FFMPEG
	if (player->videoPacketQueue.size() >= AV_PACKET_VIDEO_CAPACITY)
		return true;

	if (player->audioPacketQueue.size() >= AV_PACKET_AUDIO_CAPACITY)
		return true;

	MoviePlayerData::VideoState& videoState = player->videoState;
	MoviePlayerData::AudioState& audioState = player->audioState;

	AVPacket& packet = player->packet;

	bool isDone = false;

	ILog* iLog = GetISystem()->GetILog();
	switch (av_read_frame(player->formatCtx, &packet))
	{
	case 0:
		if (player->videoStream && (packet.stream_index == player->videoStream->index))
		{
			AVPacket* videoPacket = av_packet_clone(&packet);
			videoPacket->pts += videoState.videoOffset;
			videoPacket->dts += videoState.videoOffset;
			videoState.lastVideoPts = videoPacket->pts;

			player->videoPacketQueue.push_back(videoPacket);
		}
		else if (player->audioStream && (packet.stream_index == player->audioStream->index))
		{
			AVPacket* audioPacket = av_packet_clone(&packet);
			audioPacket->pts += audioState.audioOffset;
			audioPacket->dts += audioState.audioOffset;
			audioState.lastAudioPts = audioPacket->pts;

			player->audioPacketQueue.push_back(audioPacket);
		}
		break;
	case AVERROR_EOF:
	{
		if(onFinish)
			onFinish();
		break;
	}
	default:
		iLog->LogError("Failed av_read_frame");
		isDone = true;
	}

	// don't forget, otherwise memleak
	av_packet_unref(&packet);

	return !isDone;
#else
	return true;
#endif // CRY_NO_FFMPEG
}

static bool PlayerRewind(MoviePlayerData* player)
{
#ifndef CRY_NO_FFMPEG
	MoviePlayerData::VideoState& videoState = player->videoState;
	MoviePlayerData::AudioState& audioState = player->audioState;

	ILog* iLog = GetISystem()->GetILog();

	// restart
	if (av_seek_frame(player->formatCtx, player->videoStream->index, player->videoStream->start_time, AVSEEK_FLAG_BACKWARD) < 0)
	{
		iLog->LogError("Failed av_seek_frame");
		return false;
	}

	if (player->videoStream)
		videoState.videoOffset = videoState.lastVideoPts;

	if (player->audioStream)
		audioState.audioOffset = audioState.lastAudioPts;
#endif // CRY_NO_FFMPEG
	return true;
}

#ifndef CRY_NO_FFMPEG
static double clock_seconds(int64_t start_time)
{
	return (av_gettime() - start_time) / 1000000.;
}

static double pts_seconds(AVFrame* frame, AVStream* stream)
{
	return frame->pts * av_q2d(stream->time_base);
}
#endif // CRY_NO_FFMPEG

static bool PlayerVideoDecodeStep(MoviePlayerData* player)
{
#ifndef CRY_NO_FFMPEG
	if (!player->videoStream)
		return false;

	MoviePlayerData::VideoState& videoState = player->videoState;
	DecodeState& state = player->videoState.state;

	ILog* iLog = GetISystem()->GetILog();

	if (state == DEC_DEQUEUE_PACKET)
	{
		if (player->videoPacketQueue.size() == 0)
			return false;

		// must delete old packet as it appears to be processed already
		av_packet_free(&videoState.deqPacket);
		videoState.deqPacket = player->videoPacketQueue.front();
		player->videoPacketQueue.pop_front();

		state = DEC_SEND_PACKET;
	}

	if (state == DEC_SEND_PACKET)
	{
		const int r = avcodec_send_packet(player->videoCodec, videoState.deqPacket);
		if (r == 0)
		{
			state = DEC_DEQUEUE_PACKET;
			return false;
		}

		if (r != AVERROR(EAGAIN))
		{
			state = DEC_ERROR;
			return false;
		}
		state = DEC_RECV_FRAME;
	}
	else if (state == DEC_RECV_FRAME)
	{
		const int r = avcodec_receive_frame(player->videoCodec, videoState.frame);
		if (r == AVERROR(EAGAIN))
		{
			state = DEC_SEND_PACKET;
			return false;
		}

		if (r != 0)
		{
			state = DEC_ERROR;
			return false;
		}

		if (videoState.frame->pts != AV_NOPTS_VALUE)
			videoState.frame->pts = videoState.frame->pts / player->clockSpeed;
		else if(videoState.frame->pkt_dts != AV_NOPTS_VALUE)
			videoState.frame->pts = videoState.frame->pkt_dts / player->clockSpeed;
		else
		{
			state = DEC_RECV_FRAME;
			return false;
		}

		const double clock = clock_seconds(player->clockStartTime);
		const double pts_secs = pts_seconds(videoState.frame, player->videoStream);
		const double delta = (pts_secs - clock);

		if (delta < 0)
		{
			state = DEC_RECV_FRAME;
			return false;
		}
		videoState.presentationDelay = delta;

		state = DEC_READY_FRAME;
	}
	else if (state == DEC_READY_FRAME)
	{
		// wait extra amount of time
		if (videoState.presentationDelay > videoState.timer.GetTime())
			return false;

		if (!videoState.presentFrame)
			videoState.presentFrame = av_frame_clone(videoState.frame);

		videoState.timer.GetTime(true);
		state = DEC_RECV_FRAME;
	}

	return (state == DEC_READY_FRAME);
#endif // CRY_NO_FFMPEG
}

static void PlayerAudioDecodeStep(MoviePlayerData* player, FrameQueue& frameQueue)
{
#ifndef CRY_NO_FFMPEG
	if (!player->audioStream)
		return;

	MoviePlayerData::AudioState& audioState = player->audioState;
	DecodeState& state = player->audioState.state;

	if (state == DEC_DEQUEUE_PACKET)
	{
		if (player->audioPacketQueue.size() == 0)
			return;

		av_packet_free(&audioState.deqPacket);
		audioState.deqPacket = player->audioPacketQueue.front();
		player->audioPacketQueue.pop_front();

		state = DEC_SEND_PACKET;
	}
	
	if (state == DEC_SEND_PACKET)
	{
		int r = avcodec_send_packet(player->audioCodec, audioState.deqPacket);
		if (r == 0)
		{
			state = DEC_DEQUEUE_PACKET;
			return;
		}

		if (r != AVERROR(EAGAIN))
		{
			state = DEC_ERROR;
			return;
		}
		state = DEC_RECV_FRAME;
	}
	else if (state == DEC_RECV_FRAME)
	{
		int r = avcodec_receive_frame(player->audioCodec, audioState.frame);
		if (r == AVERROR(EAGAIN))
		{
			state = DEC_SEND_PACKET;
			return;
		}

		if (r != 0)
		{
			state = DEC_ERROR;
			return;
		}

		state = DEC_RECV_FRAME;

		AVFrame* convFrame = av_frame_alloc();
		av_channel_layout_default(&convFrame->ch_layout, 2);

		convFrame->format = AV_SAMPLE_FMT_S16;
		convFrame->sample_rate = 44100;

		if (audioState.frame->pts != AV_NOPTS_VALUE)
			convFrame->pts = static_cast<double>(audioState.frame->pts) / player->clockSpeed;
		else if (audioState.frame->pkt_dts != AV_NOPTS_VALUE)
			convFrame->pts = static_cast<double>(audioState.frame->pkt_dts) / player->clockSpeed;
		else
			convFrame->pts = 0;

		if (swr_convert_frame(player->audioSwr, convFrame, audioState.frame) == 0)
		{
			MutexLocker m(player->audioSourceMutex);
			if (frameQueue.size() == AV_PACKET_AUDIO_CAPACITY)
			{
				AVFrame* frame = frameQueue.front();
				av_frame_free(&frame);
				frameQueue.pop_front();
			}

			frameQueue.push_back(convFrame);
		}
		else
			av_frame_free(&convFrame);
	}
#endif // CRY_NO_FFMPEG
}

static bool StartPlayback(MoviePlayerData* player)
{
	if (!player)
		return false;
#ifndef CRY_NO_FFMPEG
	if (player->videoStream)
	{
		av_init_packet(&player->packet);

		{
			MoviePlayerData::VideoState& videoState = player->videoState;
			videoState.frame = av_frame_alloc();
			videoState.state = DEC_DEQUEUE_PACKET;
			videoState.deqPacket = nullptr;
			videoState.timer.GetTime(true);
		}

		{
			MoviePlayerData::AudioState& audioState = player->audioState;
			audioState.frame = av_frame_alloc();
			audioState.state = DEC_DEQUEUE_PACKET;
			audioState.deqPacket = nullptr;
		}
	}
#endif // CRY_NO_FFMPEG
	return true;
}

//---------------------------------------------------
class CMovieAudioSource
{
	friend class CUIVideoFFmpeg;
public:
	CMovieAudioSource(MoviePlayerData* player);

	bool				ReadSamples(void* pBuffer, int nLength);

	int					GetChannels() const { return m_channels; }
	int					GetSampleRate() const { return m_sampleRate; }

private:
	FrameQueue			m_frameQueue;
	MoviePlayerData*	m_player = nullptr;
	int					m_sampleRate = 0;
	int					m_channels = 0;
};

CMovieAudioSource::CMovieAudioSource(MoviePlayerData* player)
	: m_player(player)
{
	m_channels = 2;
	m_sampleRate = 44100;
}

bool CMovieAudioSource::ReadSamples(void* out, int nLength)
{
#ifndef CRY_NO_FFMPEG
	MutexLocker m(m_player->audioSourceMutex);

	ILog* iLog = GetISystem()->GetILog();
	memset(out, 0, nLength);

	const int sampleSize = m_channels * sizeof(short);
	const int requestedSamples = nLength / sampleSize;

	int samplesToRead = requestedSamples;
	int numSamplesRead = 0;

	auto it = m_frameQueue.begin();
	while (samplesToRead > 0 && it != m_frameQueue.end())
	{
		AVFrame* frame = *it;

		const int frameSamples = frame->nb_samples - frame->height;
		if (frameSamples <= 0)
		{
			++it;
			continue;
		}

		const int paintSamples = crymin(samplesToRead, frameSamples);
		if (paintSamples <= 0)
			break;

		memcpy((byte*)out + numSamplesRead * sampleSize,
			frame->data[0] + frame->height * sampleSize,
			paintSamples * sampleSize);

		numSamplesRead += paintSamples;
		samplesToRead -= paintSamples;

		// keep the frame but we keep read cursor
		frame->height += paintSamples;
		++it;
	}

#if 0
	if (numSamplesRead < requestedSamples)
		iLog->LogWarning("CMovieAudioSource::ReadSamples underpaint - %d of %d", numSamplesRead, requestedSamples);
#endif

	// we don't have frames yet, return 1 because we need a warmup from video system
	return requestedSamples > 0 ? true : false;
#else
	return false;
#endif // CRY_NO_FFMPEG
}

//---------------------------------------------------

CUIVideoFFmpeg::~CUIVideoFFmpeg()
{
	Terminate();
}

int	CUIVideoFFmpeg::Run()
{
	if (!m_player)
		return 0;
#ifndef CRY_NO_FFMPEG
	m_player->clockStartTime = av_gettime();

	bool started = false;
	FrameQueue fakeQueue;
	while (m_playerCmd == PLAYER_CMD_PLAYING)
	{
		SDL_Delay(0);

		if (!PlayerDemuxStep(m_player, m_onFinished))
			break;

		if (m_playerCmd == PLAYER_CMD_REWIND)
		{
			PlayerRewind(m_player);
			m_playerCmd = PLAYER_CMD_PLAYING;
		}

		// wait until fully bufferized
		if (!started)
		{
			if (m_player->videoPacketQueue.size() >= crymin(AV_PACKET_VIDEO_CAPACITY, AV_PACKET_AUDIO_CAPACITY) / 2)
				started = true;
			continue;
		}

		if (m_player->videoCodec)
			PlayerVideoDecodeStep(m_player);

		if (m_player->audioCodec)
		{
			if(m_audioStream)
				PlayerAudioDecodeStep(m_player, m_audioSrc->m_frameQueue);
			else
				PlayerAudioDecodeStep(m_player, fakeQueue);
		}

		if (m_player->videoPacketQueue.size() == 0 && m_player->audioPacketQueue.size() == 0)
		{
			// NOTE: could be unreliable
			if (av_gettime() - m_player->clockStartTime > 10000)
				break;
		}
	}

#endif // MOVIELIB_DISABLE
	m_playerCmd = PLAYER_CMD_NONE;
	return 0;
}

CUIVideoFFmpeg::CUIVideoFFmpeg(const char* aliasName)
{
	m_aliasName = aliasName;
}

signed char FFmpegAudioCallback(CS_STREAM* pStream, void* pBuffer, int nLength, void* nParam)
{
	if (!nParam)
		return 0;
	CMovieAudioSource* streamer = (CMovieAudioSource*)nParam;
	if (streamer)
	{
		return streamer->ReadSamples(pBuffer, nLength);
	}
	return 0;
}

bool CUIVideoFFmpeg::Init(const char* pathToVideo, bool needSound)
{
#ifndef CRY_NO_FFMPEG
	const char* nameOfPlayer = m_aliasName.length() ? m_aliasName.c_str() : pathToVideo;

	m_player = CreatePlayerData(nullptr, pathToVideo);
	if (m_player)
	{
		const AVCodecContext* codec = m_player->videoCodec;

		if (m_player->videoStream)
		{
			m_frameBuffer = new uint8[codec->width * codec->height * 4];
			memset(m_frameBuffer, 0, codec->width * codec->height * 4);
			m_textureId = GetISystem()->GetIRenderer()->DownLoadToVideoMemory(m_frameBuffer, codec->width, codec->height, eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, nullptr, FT_DYNAMIC);
			
			//CRYASSERT_MSG(m_textureId >= 0, "Cannot create texture for movie player");
			if (m_textureId < 0)
			{
				__builtin_trap();
			}
		}

		if (needSound && m_player->audioStream)
		{
			m_audioSrc = new CMovieAudioSource(m_player);
			m_audioStream = CS_Stream_Create(FFmpegAudioCallback,
				4096 * m_audioSrc->GetChannels(), 0,
				m_audioSrc->GetSampleRate(), m_audioSrc);
		}
	}
#endif // CRY_NO_FFMPEG
	return m_player != nullptr;
}

void CUIVideoFFmpeg::Terminate()
{
	Stop();
	FreePlayerData(m_player);
	SAFE_DELETE(m_player);
	SAFE_DELETE_ARRAY(m_frameBuffer);
	
	if (m_audioStream)
	{
		CS_Stream_Close(m_audioStream);
	}
	m_audioStream = nullptr;
	SAFE_DELETE(m_audioSrc);

	if (m_textureId > -1)
	{
		GetISystem()->GetIRenderer()->RemoveTexture(m_textureId);
		m_textureId = -1;
	}
}

int	CUIVideoFFmpeg::ThreadFn(void* data)
{
	CUIVideoFFmpeg* _this = reinterpret_cast<CUIVideoFFmpeg*>(data);
	return _this->Run();
}

void CUIVideoFFmpeg::Start()
{
	if (!StartPlayback(m_player))
	{
		return;
	}

	m_playerCmd = PLAYER_CMD_PLAYING;

	if(!m_threadHandle)
		m_threadHandle = SDL_CreateThread(ThreadFn, "UIVideoThread", this);

	if(m_audioStream)
		CS_Stream_Play(CS_FREE, m_audioStream);
}

void CUIVideoFFmpeg::Stop()
{
	if (!m_player)
		return;

	m_playerCmd = PLAYER_CMD_STOP;

	int status = 0;
	SDL_WaitThread((SDL_Thread*)m_threadHandle, &status);
	m_threadHandle = nullptr;

#ifndef CRY_NO_FFMPEG
	MoviePlayerData* player = m_player;
	for (AVPacket*& packet : player->videoPacketQueue)
		av_packet_free(&packet);

	for (AVPacket*& packet : player->audioPacketQueue)
		av_packet_free(&packet);

	{
		if (m_audioStream)
			CS_Stream_Stop(m_audioStream);

		MutexLocker m(player->audioSourceMutex);
		if (m_audioSrc)
			m_audioSrc->m_frameQueue.clear();

		player->videoPacketQueue.clear();
		player->audioPacketQueue.clear();
	}
#endif // CRY_NO_FFMPEG
}

void CUIVideoFFmpeg::Rewind()
{
	m_playerCmd = PLAYER_CMD_REWIND;
}

bool CUIVideoFFmpeg::IsPlaying() const
{
	return m_playerCmd != PLAYER_CMD_NONE;
}

void CUIVideoFFmpeg::Present()
{
#ifndef CRY_NO_FFMPEG
	if (!m_player)
		return;

	MoviePlayerData* player = m_player;
	MoviePlayerData::VideoState& videoState = m_player->videoState;
	if (m_audioStream)
	{
		CS_Update();
	}

	if (!videoState.presentFrame)
		return;

	const int width = player->videoCodec->width;
	const int height = player->videoCodec->height;

	// time to update renderer texture
	{
		uint8_t* data[1] = {
			(uint8_t*)m_frameBuffer
		};
		const int stride[1] = {
			(int)width * 4
		};
		sws_scale(player->videoSws, videoState.presentFrame->data, videoState.presentFrame->linesize, 0, videoState.presentFrame->height, data, stride);

		GetISystem()->GetIRenderer()->UpdateTextureInVideoMemory(m_textureId, m_frameBuffer, 0, 0, width, height, eTF_8888);
	}

	av_frame_free(&videoState.presentFrame);
#endif // CRY_NO_FFMPEG
}

void CUIVideoFFmpeg::SetTimeScale(float value)
{
#ifndef CRY_NO_FFMPEG
	if (m_player)
		m_player->clockSpeed = value;
#endif // CRY_NO_FFMPEG
}

int CUIVideoFFmpeg::GetTextureId() const
{
	return m_textureId;
}

int	CUIVideoFFmpeg::GetWidth() const
{
	if (!m_player)
		return 1;
	MoviePlayerData::VideoState& videoState = m_player->videoState;
	if (!videoState.presentFrame)
		return 1;
	return videoState.presentFrame->width;
}

int	CUIVideoFFmpeg::GetHeight() const
{
	if (!m_player)
		return 1;
	MoviePlayerData::VideoState& videoState = m_player->videoState;
	if (!videoState.presentFrame)
		return 1;
	return videoState.presentFrame->height;
}