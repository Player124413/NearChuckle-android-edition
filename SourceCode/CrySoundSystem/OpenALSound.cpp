#ifndef LINUX64
#include "CrySound.h"
#else
#include "CrySound64.h"
#endif

#include <AL/al.h>
#include <AL/alc.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "stb_vorbis.c"

#define MAX_SOUND_FILENAME 128
#define MIN_QUEUED_BUFFERS 20

#ifndef LINUX
#define __builtin_trap void
#endif

#if 0
#define AL_LOG(...) printf(__VA_ARGS__);
#else
#define AL_LOG
#endif

typedef struct
{
	ALuint buf;
	int flags;
	char filename[MAX_SOUND_FILENAME];
} ALSample_t;

typedef struct
{
	stb_vorbis* ogg;
	unsigned char* filebuf;
	bool loop;
	bool ended;
} AL_OGG_Userdata_t;

typedef struct
{
	CS_STREAMCALLBACK callback;
	char* buffer;
	int len;
#ifdef LINUX64
	void* userdata;
#else
	int userdata;
#endif
	ALuint source;
	int channel;
	ALenum format;
	int rate;
} ALStream_t;

ALCdevice* aldevice;
ALCcontext* alcontext;
#define MAX_SOURCES 30
ALuint sources[MAX_SOURCES];
std::vector<ALSample_t*> buffers;
std::vector<ALStream_t*> streams;
CS_OPENCALLBACK my_fopen;
CS_CLOSECALLBACK my_fclose;
CS_READCALLBACK my_fread;
CS_SEEKCALLBACK my_fseek;
CS_TELLCALLBACK my_ftell;

#define SOURCE_OUT_OF_BOUNDS 0

// FARCRY_SND_DEBUG=1: source/stream dumps and stream play, stop and pause calls on stderr.
static bool SndDebug()
{
	static int n = -1;
	if (n < 0)
		n = getenv("FARCRY_SND_DEBUG") != NULL;
	return n != 0;
}

ALuint GetSourceOfChannel(int channel)
{
	size_t i;
	if (channel < 0)
	{
		__builtin_trap();
		return SOURCE_OUT_OF_BOUNDS;
	}

	if (channel >= MAX_SOURCES)
	{
		for (i = 0; i < streams.size(); i++)
		{
			if (streams[i]->channel == channel)
			{
				return streams[i]->source;
			}
		}
		__builtin_trap();
		return SOURCE_OUT_OF_BOUNDS;
	}
	else
	{
		return sources[channel];
	}
}

ALSample_t* GetSampleFromName(const char* filename)
{
	for (size_t i = 0; i < buffers.size(); i++)
	{
		if (!strcmp(buffers[i]->filename, filename))
		{
			return buffers[i];
		}
	}

	return NULL;
}

int audio_next_available_source(void)
{
	int status;
	unsigned int i;

	for (i = 0; i < MAX_SOURCES; i++)
	{
		if (sources[i] == 0)
		{
			__builtin_trap();
			return -1;
		}
		alGetSourcei(sources[i], AL_SOURCE_STATE, &status);
		// Not a paused one: its buffer cannot be replaced, so the new play would resume the old sound
		// (a menu sound over the game's paused ones).
		if (status == AL_STOPPED || status == AL_INITIAL)
		{
			return (ALint)i;
		}
	}

	return -1;
}

DLL_API signed char     F_API CS_Init(int mixrate, int maxsoftwarechannels, unsigned int flags)
{
	aldevice = alcOpenDevice(0);
	alcontext = alcCreateContext(aldevice, 0);
	alcMakeContextCurrent(alcontext);
	alGenSources(MAX_SOURCES, sources);
	return 1;
}

DLL_API void            F_API CS_Close()
{
	AL_LOG("OpenAL: Deleting %lu buffers.\n", buffers.size());
	size_t i;
	for (i = 0; i < buffers.size(); i++)
	{
		alDeleteBuffers(1, &buffers[i]->buf);
		delete buffers[i];
	}
	buffers.clear();
	for (i = 0; i < streams.size(); i++)
	{
		CS_Stream_Close((CS_STREAM*)streams[i]);
	}

	alDeleteSources(MAX_SOURCES, sources);
	alcMakeContextCurrent(0);
	if (alcontext) alcDestroyContext(alcontext);
	if (aldevice) alcCloseDevice(aldevice);
}

DLL_API signed char     F_API CS_SetOutput(int outputtype)
{
	return 0;
}
DLL_API signed char     F_API CS_SetDriver(int driver)
{
	return 0;
}
DLL_API signed char     F_API CS_SetMixer(int mixer)
{
	return 0;
}
DLL_API signed char     F_API CS_SetBufferSize(int len_ms)
{
	return 0;
}
DLL_API signed char     F_API CS_SetHWND(void *hwnd)
{
	return 0;
}
DLL_API signed char     F_API CS_SetMinHardwareChannels(int min)
{
	return 0;
}
DLL_API signed char     F_API CS_SetMaxHardwareChannels(int max)
{
	return 0;
}
DLL_API signed char     F_API CS_SetMemorySystem(void *pool, 
                                                     int poollen, 
                                                     CS_ALLOCCALLBACK   useralloc,
                                                     CS_REALLOCCALLBACK userrealloc,
                                                     CS_FREECALLBACK    userfree)
{
	return 0;
}

DLL_API signed char     F_API CS_SetFrequency(int channel, int freq)
{
	return 0;
}
DLL_API signed char     F_API CS_SetVolume(int channel, int vol)
{
	ALuint source = GetSourceOfChannel(channel);
	alSourcef(source, AL_GAIN, (float)vol / 255.0f);
	return 1;
}

DLL_API signed char     F_API CS_SetPan(int channel, int pan)
{
	return 0;
}

DLL_API signed char     F_API CS_SetMute(int channel, signed char mute)
{
	return 0;
}
DLL_API signed char     F_API CS_SetPriority(int channel, int priority)
{
	return 0;
}

DLL_API signed char     F_API CS_SetPaused(int channel, signed char paused)
{
	ALuint source = GetSourceOfChannel(channel);
	if (channel >= MAX_SOURCES && SndDebug())
		fprintf(stderr, "SND: SetPaused stream channel %d paused %d\n", channel, (int)paused);

	if (paused)
	{
		alSourcePause(source);
	}
	else
	{
		alSourcePlay(source);
	}

	return 1;
}
DLL_API signed char     F_API CS_SetLoopMode(int channel, unsigned int loopmode)
{
	return 0;
}
DLL_API signed char     F_API CS_SetCurrentPosition(int channel, unsigned int offset)
{
	return 0;
}

static void stream_read(void* dest, void** source, size_t size)
{
	memcpy(dest, *source, size);
	*source = (char*)*source + size;
}

static int audio_wav_from_data_MEM(void* p, int bufsize, ALuint* buf)
{
	ALenum ALformat;
	char header[4], wave_header[4], subchunk1[4], subchunk2[4];
	char* temp_buffer;
	unsigned int size, frequency, subchunk2size;
	unsigned short num_channels, bits_per_sample, format;
	void* start = p;
	*buf = 0;

	stream_read(&header, &p, sizeof(header));

	if (strncmp(header, "RIFF", 4))
	{
		AL_LOG("Bad RIFF header\n");
		return 1;
	}

	stream_read(&size, &p, sizeof(size));
	stream_read(&wave_header, &p, sizeof(wave_header));

	if (strncmp(wave_header, "WAVE", 4))
	{
		AL_LOG("Bad WAVE header\n");
		return 1;
	}

	stream_read(&subchunk1, &p, sizeof(subchunk1));

	if (strncmp(subchunk1, "fmt", 3))
	{
		AL_LOG("Bad subchunk - expected \"fmt\", got %s\n", subchunk1);
		return 1;
	}

	p = (char*)p + sizeof(unsigned int); /* Subchunk 1 size */
	stream_read(&format, &p, sizeof(format));

	if (format != 1)
	{
		AL_LOG("Format is %i, expected 1\n", format);
		return 1;
	}

	stream_read(&num_channels, &p, sizeof(num_channels));
	stream_read(&frequency, &p, sizeof(frequency));

	p = (char*)p + sizeof(unsigned int); /* ByteRate */
	p = (char*)p + sizeof(unsigned short); /* BlockAlign */

	stream_read(&bits_per_sample, &p, sizeof(bits_per_sample));

	switch (bits_per_sample)
	{
		case 8:
			ALformat = num_channels == 2 ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8;
		break;
		case 16:
			ALformat = num_channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
		break;
		default:
			AL_LOG("bits_per_sample is %i, expected 8 for 16\n", bits_per_sample);
			return 1;
		break;
	}

	memset(subchunk2, 0, 4);
	p = start;
	while (strncmp(subchunk2, "data", 4))
	{
		if (((char*)p + 4 - (char*)start) >= bufsize)
		{
			//end of file, give up
			break;
		}
		stream_read(&subchunk2, &p, sizeof(subchunk2));
		if (strncmp(subchunk2, "data", 4))
		{
			p = (char*)p - 3;
		}
		else
		{
			break;
		}
	}

	if (strncmp(subchunk2, "data", 4))
	{
		AL_LOG("Subchunk 2 ID is %s, expected \"data\"\n", subchunk2);
		return 1;
	}

	stream_read(&subchunk2size, &p, sizeof(subchunk2size));

	temp_buffer = new char[subchunk2size];

	stream_read(temp_buffer, &p, subchunk2size);

	alGenBuffers(1, buf);
	alBufferData(*buf, ALformat, temp_buffer, subchunk2size, frequency);
	delete [] temp_buffer;
	return 0;
}

#ifndef LINUX64
DLL_API CS_SAMPLE* F_API CS_Sample_Load(int index, const char* name_or_data, unsigned int mode, int memlength)
#else
DLL_API CS_SAMPLE * F_API CS_Sample_Load(int index, const char *name_or_data, unsigned int mode, int offset, int length)
#endif
{
	ALuint thebuf;
	int ret;
	ALSample_t* samp = nullptr;
	if (mode & CS_LOADMEMORY)
	{
		ret = audio_wav_from_data_MEM((void*)name_or_data, length, &thebuf);
		if (ret == 0)
		{
			samp = new ALSample_t;
			samp->buf = thebuf;
			samp->flags = mode;
			strcpy(samp->filename, "<MEMORY>");

			buffers.push_back(samp);
			AL_LOG("OpenAL: There are now %i buffers.\n", buffers.size());
		}
		else
		{
			AL_LOG("OpenAL: Failed to load wav\n");
		}
	}
	else
	{
		__builtin_trap();
	}
	return (CS_SAMPLE*)samp;
}

DLL_API CS_SAMPLE* F_API CS_Sample_Alloc(int index, int length, unsigned int mode, int deffreq, int defvol, int defpan, int defpri)
{
	return 0;
}

DLL_API void            F_API CS_Sample_Free(CS_SAMPLE* sptr)
{
	std::vector<ALSample_t*>::iterator it;
	ALSample_t* samp = (ALSample_t*)sptr;

	for (it = buffers.begin(); it != buffers.end(); it++)
	{
		if (*it == samp)
		{
			alDeleteBuffers(1, &(*it)->buf);
			buffers.erase(it);
			return;
		}
	}
}
#if 0
DLL_API signed char     F_API CS_Sample_Upload(CS_SAMPLE* sptr, void* srcdata, unsigned int mode)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_Lock(CS_SAMPLE* sptr, int offset, int length, void** ptr1, void** ptr2, unsigned int* len1, unsigned int* len2)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_Unlock(CS_SAMPLE* sptr, void* ptr1, void* ptr2, unsigned int len1, unsigned int len2)
{
	return 0;
}

DLL_API int             F_API CS_GetError()
{
	return 0;
}
#endif
DLL_API float           F_API CS_GetVersion()
{
	return CS_VERSION;
}

DLL_API int             F_API CS_GetOutput()
{
	return 0;
}
#if 0
DLL_API void* F_API CS_GetOutputHandle()
{
	return 0;
}
#endif
DLL_API int             F_API CS_GetDriver()
{
	return 0;
}

DLL_API int             F_API CS_GetMixer()
{
	return 0;
}

DLL_API int             F_API CS_GetNumDrivers()
{
	return 1;
}

#ifndef LINUX64
DLL_API signed char* F_API CS_GetDriverName(int id)
#else
DLL_API const char *    F_API CS_GetDriverName(int id)
#endif
{
	if (id == 0)
	{
#ifndef LINUX64
		return (signed char*)"OpenAL";
#else
		return "OpenAL";
#endif
		
	}
	return nullptr;
}

DLL_API signed char     F_API CS_GetDriverCaps(int id, unsigned int* caps)
{
	return 0;
}

DLL_API int             F_API CS_GetOutputRate()
{
	return 0;
}

DLL_API int             F_API CS_GetMaxChannels()
{
	return 0;
}

DLL_API int             F_API CS_GetMaxSamples()
{
	return 0;
}

DLL_API int             F_API CS_GetSFXMasterVolume()
{
	return 0;
}

DLL_API int             F_API CS_GetNumHardwareChannels()
{
	return 0;
}

DLL_API int             F_API CS_GetChannelsPlaying()
{
	return 0;
}

DLL_API float           F_API CS_GetCPUUsage()
{
	return 0;
}

DLL_API void            F_API CS_GetMemoryStats(unsigned int* currentalloced, unsigned int* maxalloced)
{
	
}

DLL_API signed char     F_API CS_Stream_SetBufferSize(int ms)
{
	return 0;
}

#ifndef LINUX64
DLL_API CS_STREAM* F_API CS_Stream_Open(const char* name_or_data, unsigned int mode, int offset, int length);
#endif

DLL_API CS_STREAM* F_API CS_Stream_OpenFile(const char* filename, unsigned int mode, int memlength)
{
	return CS_Stream_Open(filename, mode, 0, memlength);
}

static int audio_ogg_from_data(unsigned char* p, int bufsize, ALuint* buf)
{
	ALshort* ogg_buffer;
	int size, length_samples, vorbis_error;
	stb_vorbis_info info;
	ALenum format;
	stb_vorbis* ogg = stb_vorbis_open_memory(p, bufsize, &vorbis_error, NULL);

	if (!ogg)
	{
		return vorbis_error;
	}

	info = stb_vorbis_get_info(ogg);
	format = (info.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	length_samples = stb_vorbis_stream_length_in_samples(ogg) * info.channels;
	size = length_samples * sizeof(ALshort);

	ogg_buffer = (ALshort*)malloc(size);

	stb_vorbis_get_samples_short_interleaved(ogg,
		info.channels, ogg_buffer, length_samples);

	stb_vorbis_close(ogg);

	alGenBuffers(1, buf);
	alBufferData(*buf, format, ogg_buffer, size, info.sample_rate);

	free(ogg_buffer);

	return 0;
}

#ifdef LINUX64
signed char StreamOGGCallback(CS_STREAM* pStream, void *pBuffer, int nLength, void* nParam)
#else
signed char StreamOGGCallback(CS_STREAM* pStream, void* pBuffer, int nLength, int nParam)
#endif
{
	AL_OGG_Userdata_t* userdata = (AL_OGG_Userdata_t*)nParam;
	int channels = userdata->ogg->channels;
	int want = nLength / (int)sizeof(short) / channels;
	int got = 0;
	while (got < want)
	{
		int n = stb_vorbis_get_samples_short_interleaved(userdata->ogg, channels, (short*)pBuffer + got * channels, (want - got) * channels);
		if (n > 0) { got += n; continue; }
		if (!userdata->loop) break;
		stb_vorbis_seek_start(userdata->ogg);
	}
	if (got < want)
	{
		// Finished: pad with silence once, then UpdateStream stops queueing.
		memset((short*)pBuffer + got * channels, 0, (want - got) * channels * sizeof(short));
		userdata->ended = true;
	}
	return 0;
}

DLL_API CS_STREAM*    F_API CS_Stream_Open(const char *name_or_data, unsigned int mode, int offset, int length)
{
#ifndef LINUX64
	unsigned int file = my_fopen(name_or_data);
#else
	FILE *file = (FILE*)my_fopen(name_or_data);
#endif
	int len, ret, vorbis_error;
	unsigned char* buf;
	ALuint thebuf = 0;
	stb_vorbis* ogg;
	stb_vorbis_info info;
	
	ALStream_t* stream = nullptr;
	AL_OGG_Userdata_t* userdata = nullptr;
	const char* ext = strrchr(name_or_data, '.');

	if (strlen(name_or_data) >= MAX_SOUND_FILENAME)
	{
		__builtin_trap();
	}

	if (!strcmp(ext, ".ogg"))
	{
		if (file)
		{
			my_fseek(file, 0, SEEK_END);
			len = my_ftell(file);
			
			buf = new unsigned char [len];
			my_fseek(file, 0, SEEK_SET);
			my_fread(buf, len, file);
			my_fclose(file);

			ogg = stb_vorbis_open_memory(buf, len, &vorbis_error, NULL);

			if (!ogg)
			{
				__builtin_trap();
				return NULL;
			}

			info = stb_vorbis_get_info(ogg);
			stream = new ALStream_t;

			userdata = new AL_OGG_Userdata_t;
			userdata->ogg = ogg;
			userdata->filebuf = buf;
			userdata->loop = (mode & CS_LOOP_NORMAL) != 0;
			userdata->ended = false;

			alGenSources(1, &stream->source);
			// ~93 ms of stereo per chunk: one chunk a frame has to outrun the mixer even on slow frames.
			stream->buffer = new char[16384];
			stream->len = 16384;
			stream->format = (info.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
			stream->rate = info.sample_rate;
			stream->callback = StreamOGGCallback;
#ifdef LINUX64
			stream->userdata = userdata;
#else
			stream->userdata = (int)userdata;
#endif
			stream->channel = CS_FREE;
			streams.push_back(stream);

			AL_LOG("OpenAL %s: There are now %lu streams.\n", __func__, streams.size());
		}
		else
		{
			__builtin_trap();
		}

	}
	else if (!strcmp(ext, ".wav"))
	{
		AL_LOG("Error, WAV stream not handled\n");
		return NULL;
		//__builtin_trap();
#if 0
		if (file)
		{
			my_fseek(file, 0, SEEK_END);
			len = my_ftell(file);
			
			buf = new unsigned char[len];
			my_fseek(file, 0, SEEK_SET);
			my_fread(buf, len, file);
			my_fclose(file);
			
			ret = audio_wav_from_data_MEM(buf, len, &thebuf);
			if (ret == 0)
			{
				samp = new ALSample_t;
				samp->buf = thebuf;
				samp->flags = mode;
				strcpy(samp->filename, name_or_data);
				
				buffers.push_back(samp);
				AL_LOG("OpenAL: There are now %lu buffers.\n", buffers.size());
			}
			else
			{
				AL_LOG("OpenAL: %s has a bad format!\n", name_or_data);
			}
		}
		else
		{
			__builtin_trap();
		}
#endif
	}
	else
	{
		__builtin_trap();
	}

	return (CS_STREAM*)stream;
}

#ifndef LINUX64
DLL_API CS_STREAM* F_API CS_Stream_Create(CS_STREAMCALLBACK callback, int length, unsigned int mode, int samplerate, int userdata)
#else
DLL_API CS_STREAM* F_API CS_Stream_Create(CS_STREAMCALLBACK callback, int length, unsigned int mode, int samplerate, void *userdata)
#endif
{
	ALStream_t* stream = new ALStream_t;
	alGenSources(1, &stream->source);
	stream->buffer = new char[length];
	stream->len = length;
	stream->callback = callback;
	stream->userdata = userdata;
	stream->channel = CS_FREE;
	stream->format = (mode & CS_MONO) ? ((mode & CS_8BITS) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16)
	                                  : ((mode & CS_8BITS) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16);
	stream->rate = samplerate > 0 ? samplerate : 44100;

	memset(stream->buffer, 0, length);

	streams.push_back(stream);
	AL_LOG("OpenAL %s: There are now %lu streams.\n", __func__, streams.size());

	return (CS_STREAM*)stream;
}

DLL_API signed char     F_API CS_Stream_Close(CS_STREAM* stream)
{
	ALStream_t* strm = (ALStream_t*)stream;
	AL_OGG_Userdata_t* ogg_userdata;
	std::vector<ALStream_t*>::iterator it;

	if (!stream)
	{
		return 0;
	}

	for (it = streams.begin(); it != streams.end(); it++)
	{
		if (*it == strm)
		{
			streams.erase(it);
			AL_LOG("OpenAL %s: There are now %lu streams.\n", __func__, streams.size());
			break;
		}
	}

	if (strm->callback == &StreamOGGCallback)
	{
		ogg_userdata = (AL_OGG_Userdata_t*)strm->userdata;
		stb_vorbis_close(ogg_userdata->ogg);
		delete [] ogg_userdata->filebuf;
	}

	alDeleteSources(1, &strm->source);
	delete [] strm->buffer;
	delete strm;

	return 1;
}

// A file stream replayed after it finished starts over.
static void RewindOGG(ALStream_t* strm)
{
	if (strm->callback != &StreamOGGCallback)
		return;
	AL_OGG_Userdata_t* ogg = (AL_OGG_Userdata_t*)(intptr_t)strm->userdata;
	if (ogg->ended)
	{
		ogg->ended = false;
		stb_vorbis_seek_start(ogg->ogg);
	}
}

DLL_API int             F_API CS_Stream_Play(int channel, CS_STREAM* stream)
{
	if (SndDebug()) fprintf(stderr, "SND: %s %p\n", __func__, stream);
	ALStream_t* strm = (ALStream_t*)stream;
	int i;
	if (channel != CS_FREE)
	{
		__builtin_trap();
		return -1;
	}
	RewindOGG(strm);

	alSourcei(strm->source, AL_SOURCE_RELATIVE, AL_TRUE);
	alSource3f(strm->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(strm->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	alSourcePlay(strm->source);

	for (i = 0; i < streams.size(); i++)
	{
		if (strm == streams[i])
		{
			break;
		}
	}

	strm->channel = MAX_SOURCES + i;
	return MAX_SOURCES + i;
}

DLL_API int             F_API CS_Stream_PlayEx(int channel, CS_STREAM* stream, CS_DSPUNIT* dsp, signed char startpaused)
{
	if (SndDebug()) fprintf(stderr, "SND: %s %p\n", __func__, stream);
	ALStream_t* strm = (ALStream_t*)stream;
	int i;
	ALuint stream_buf;
	if (channel != CS_FREE)
	{
		__builtin_trap();
		return -1;
	}
	RewindOGG(strm);

	alSourcei(strm->source, AL_SOURCE_RELATIVE, AL_TRUE);
	alSource3f(strm->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(strm->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);

	if (strm->callback != &StreamOGGCallback)
		memset(strm->buffer, 0, strm->len);
	strm->callback((CS_STREAM*)stream, strm->buffer,
				strm->len, strm->userdata);

	alGenBuffers(1, &stream_buf);
	alBufferData(stream_buf, strm->format, (ALvoid *)strm->buffer, strm->len, strm->rate);
	alSourceQueueBuffers(strm->source, 1, &stream_buf);

	alSourcePlay(strm->source);
	if (startpaused)
	{
		alSourcePause(strm->source);
	}

	for (i = 0; i < streams.size(); i++)
	{
		if (strm == streams[i])
		{
			break;
		}
	}

	strm->channel = MAX_SOURCES + i;
	return MAX_SOURCES + i;
}

DLL_API signed char     F_API CS_Stream_Stop(CS_STREAM* stream)
{
	ALStream_t* strm;
	int i, num_buffers;
	ALuint buffer;

	if (!stream)
	{
		return 0;
	}

	strm = (ALStream_t*)stream;
	if (SndDebug()) fprintf(stderr, "SND: CS_Stream_Stop %p channel %d\n", stream, strm->channel);
	strm->channel = CS_FREE;
	alSourceStop(strm->source);
	// A stopped file stream plays from the start next time.
	if (strm->callback == &StreamOGGCallback)
	{
		AL_OGG_Userdata_t* ogg = (AL_OGG_Userdata_t*)(intptr_t)strm->userdata;
		ogg->ended = false;
		stb_vorbis_seek_start(ogg->ogg);
	}
	alGetSourcei(strm->source, AL_BUFFERS_QUEUED, &num_buffers);

	for (i = 0; i < num_buffers; i++)
	{
		alSourceUnqueueBuffers(strm->source, 1, &buffer);
		alDeleteBuffers(1, &buffer);
	}

	return 1;
}
#if 0
DLL_API int             F_API CS_Stream_GetOpenState(CS_STREAM* stream)
{
	return 0;
}
#endif
DLL_API signed char     F_API CS_Stream_SetPosition(CS_STREAM* stream, unsigned int position)
{
	return 0;
}

DLL_API unsigned int    F_API CS_Stream_GetPosition(CS_STREAM* stream)
{
	return 0;
}

DLL_API signed char     F_API CS_Stream_SetTime(CS_STREAM* stream, int ms)
{
	return 0;
}

DLL_API int             F_API CS_Stream_GetTime(CS_STREAM* stream)
{
	return 0;
}

DLL_API int             F_API CS_Stream_GetLength(CS_STREAM* stream)
{
	return 0;
}

DLL_API int             F_API CS_Stream_GetLengthMs(CS_STREAM* stream)
{
	unsigned int lengthInSamples = CS_Stream_GetLength(stream);
	ALSample_t* samp = (ALSample_t*)stream;
	ALint frequency;
	float durationInMilliseconds;
	alGetBufferi(samp->buf, AL_FREQUENCY, &frequency);
	durationInMilliseconds = ((float)lengthInSamples / (float)frequency) * 1000.0f;
	return (int)durationInMilliseconds;
}

DLL_API int             F_API CS_FX_Enable(int channel, unsigned int fx)
{
	return 0;
}

DLL_API signed char     F_API CS_FX_SetI3DL2Reverb(int fxid, int Room, int RoomHF,
	float RoomRolloffFactor, float DecayTime, float DecayHFRatio, int Reflections,
	float ReflectionsDelay, int Reverb, float ReverbDelay, float Diffusion,
	float Density, float HFReference)
{
	return 0;
}

DLL_API signed char     F_API CS_FX_SetParamEQ(int fxid, float Center, float Bandwidth,
	float Gain)
{
	return 0;
}
DLL_API signed char     F_API CS_FX_SetWavesReverb(int fxid, float InGain, float ReverbMix,
	float ReverbTime, float HighFreqRTRatio)
{
	return 0;
}

//A hack for Bink audio streams from libbinkdec, which
//buffer a large amount of bytes at the start, but then
//send much less afterward. Check how many bytes were
//actually processed, then send only that much to OpenAL.
static int BytesFromBinkDec(char* buffer, int len)
{
	int i, j, bytes_processed;
	const int MAX_END_CHECK = 100;
	bool hit_end = false;

	for (i = bytes_processed = 0; i < len - MAX_END_CHECK; i++)
	{
		if (buffer[i] != -1)
		{
			bytes_processed++;
		}
		else
		{
			hit_end = true;
			for (j = 1; j < MAX_END_CHECK; j++)
			{
				if (buffer[i + j] != -1)
				{
					hit_end = false;
					break;
				}
			}
			
			if (hit_end)
			{
				break;
			}
			else
			{
				bytes_processed++;
			}
		}
	}

	if (bytes_processed == len - MAX_END_CHECK)
	{
		bytes_processed += MAX_END_CHECK;
	}

	return bytes_processed;
}

static void UpdateStream(ALStream_t* stream)
{
	ALenum state;
	ALuint buffer, stream_buf;
	int i, bytes_processed;
	int num_processed_buffers = 0;
	int num_queued_buffers = 0;

	alGetSourcei(stream->source, AL_SOURCE_STATE, &state);
	alGetSourcei(stream->source, AL_BUFFERS_PROCESSED, &num_processed_buffers);

	for (i = 0; i < num_processed_buffers; i++)
	{
		alSourceUnqueueBuffers(stream->source, 1, &buffer);
		alDeleteBuffers(1, &buffer);
	}

	alGetSourcei(stream->source, AL_BUFFERS_QUEUED, &num_queued_buffers);

	if (!stream->callback)
		return;

	// Top the queue up with a few chunks per call: one chunk a frame starved the source on any
	// frame longer than a chunk, and a starved source stops and was never restarted.
	bool bOgg = stream->callback == &StreamOGGCallback;
	AL_OGG_Userdata_t* ogg = bOgg ? (AL_OGG_Userdata_t*)(intptr_t)stream->userdata : NULL;
	// As in FMOD, a stream runs only between CS_Stream_Play and CS_Stream_Stop (every user plays and
	// stops its streams). A stopped music stream kept being refilled here while its callback, no longer
	// playing, wrote nothing: the last 100 ms chunk repeated at 10 Hz in the menu after death. A file
	// stream may still prefill before its first play.
	if (stream->channel == CS_FREE && !(bOgg && state == AL_INITIAL))
		return;
	// Vorbis is decoded on the game thread, so keep file streams' lead short (~0.5 s) and their fills gentle.
	int target = bOgg ? 6 : MIN_QUEUED_BUFFERS;
	for (int fills = 0; num_queued_buffers < target && fills < (bOgg ? 2 : 4); fills++)
	{
		if (ogg && ogg->ended)
			break;
		if (!bOgg)
			memset(stream->buffer, 0, stream->len); // a callback that writes nothing gives silence, not the last chunk again
		stream->callback((CS_STREAM*)stream, stream->buffer,
			stream->len, stream->userdata);

		if (stream->len == 138240)
		{
			bytes_processed = BytesFromBinkDec(stream->buffer, stream->len);
		}
		else
		{
			bytes_processed = stream->len;
		}

		if (bytes_processed <= 0)
			break;
		alGenBuffers(1, &stream_buf);
		alBufferData(stream_buf, stream->format, (ALvoid *)stream->buffer, bytes_processed, stream->rate);
		alSourceQueueBuffers(stream->source, 1, &stream_buf);
		num_queued_buffers++;
	}

	// Restart a starved source only; a paused one stays paused (menu pause, PlayEx's paused start).
	if ((state == AL_STOPPED || state == AL_INITIAL) && num_queued_buffers > 0 && stream->channel != CS_FREE)
	{
		alSourcePlay(stream->source);
	}
}

// FARCRY_SND_DEBUG=1: every 120 updates, every source that is playing or paused and every stream.
static void DebugDumpSources()
{
	static int nCalls;
	if (!SndDebug() || ++nCalls % 120)
		return;
	for (int i = 0; i < MAX_SOURCES; i++)
	{
		ALint state = 0, looping = 0, buf = 0, queued = 0;
		alGetSourcei(sources[i], AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING && state != AL_PAUSED)
			continue;
		alGetSourcei(sources[i], AL_LOOPING, &looping);
		alGetSourcei(sources[i], AL_BUFFER, &buf);
		alGetSourcei(sources[i], AL_BUFFERS_QUEUED, &queued);
		const char* szName = "?";
		for (size_t n = 0; n < buffers.size(); n++)
			if ((ALint)buffers[n]->buf == buf)
				szName = buffers[n]->filename;
		fprintf(stderr, "SND: source %d %s loop %d buf %d queued %d %s\n", i, state == AL_PLAYING ? "PLAYING" : "PAUSED", looping, buf, queued, szName);
	}
	for (size_t i = 0; i < streams.size(); i++)
	{
		ALint state = 0, queued = 0;
		alGetSourcei(streams[i]->source, AL_SOURCE_STATE, &state);
		alGetSourcei(streams[i]->source, AL_BUFFERS_QUEUED, &queued);
		fprintf(stderr, "SND: stream %d %s channel %d state 0x%x queued %d len %d\n", (int)i,
			streams[i]->callback == &StreamOGGCallback ? "ogg" : "callback", streams[i]->channel, state, queued, streams[i]->len);
	}
}

DLL_API void            F_API CS_Update()
{
	size_t i;
	for (i = 0; i < streams.size(); i++)
	{
		UpdateStream(streams[i]);
	}
	DebugDumpSources();
}

DLL_API void            F_API CS_SetSpeakerMode(unsigned int speakermode)
{

}
DLL_API void            F_API CS_SetSFXMasterVolume(int volume)
{
	size_t i;
	for (i = 0; i < MAX_SOURCES; i++)
	{
		alSourcef(sources[i], AL_GAIN, (float)volume / 255.0f);
	}
}
DLL_API void            F_API CS_SetPanSeperation(float pansep)
{

}
DLL_API void            F_API CS_File_SetCallbacks(CS_OPENCALLBACK  useropen,
                                                       CS_CLOSECALLBACK userclose,
                                                       CS_READCALLBACK  userread,
                                                       CS_SEEKCALLBACK  userseek,
                                                       CS_TELLCALLBACK  usertell)
{
	my_fopen = useropen;
	my_fclose = userclose;
	my_fread = userread;
	my_fseek = userseek;
	my_ftell = usertell;
}
#if 0
DLL_API void            F_API CS_3D_SetDopplerFactor(float scale)
{

}

DLL_API void            F_API CS_3D_SetDistanceFactor(float scale)
{

}
#endif
DLL_API void            F_API CS_3D_SetRolloffFactor(float scale)
{

}

#ifndef LINUX64
DLL_API signed char     F_API CS_3D_SetAttributes(int channel, float *pos, float *vel)
#else
DLL_API signed char     F_API CS_3D_SetAttributes(int channel, const float *pos, const float *vel)
#endif
{
	if (channel < 0 || channel >= MAX_SOURCES)
	{
		return 0;
	}

	alSourcei(sources[channel], AL_SOURCE_RELATIVE, AL_FALSE);

	if (pos)
	{
		alSource3f(sources[channel], AL_POSITION, pos[0], pos[1], pos[2]);
	}
	
	if (vel)
	{
		alSource3f(sources[channel], AL_VELOCITY, vel[0], vel[1], vel[2]);
	}
	
	return 1;
}
#if 0
DLL_API signed char     F_API CS_3D_GetAttributes(int channel, float *pos, float *vel)
{
	return 0;
}

DLL_API void            F_API CS_3D_Listener_SetCurrent(int current, int numlisteners)
{

}
#endif
#ifndef LINUX64
DLL_API void            F_API CS_3D_Listener_SetAttributes(float *pos, float *vel, float fx,
	float fy, float fz, float tx, float ty, float tz)
#else
DLL_API void            F_API CS_3D_Listener_SetAttributes(const float *pos, const float *vel,
	float fx, float fy, float fz, float tx, float ty, float tz)
#endif
{
	ALfloat ori[6];
	if (pos)
	{
		alListener3f(AL_POSITION, pos[0], pos[1], pos[2]);
	}
	if (vel)
	{
		alListener3f(AL_VELOCITY, vel[0], vel[1], vel[2]);
	}

	ori[0] = -fx;
	ori[1] = fy;
	ori[2] = -fz;
	ori[3] = tx;
	ori[4] = ty;
	ori[5] = tz;
	alListenerfv(AL_ORIENTATION, ori);
}
#if 0
DLL_API void            F_API CS_3D_Listener_GetAttributes(float *pos, float *vel, float *fx,
	float *fy, float *fz, float *tx, float *ty, float *tz)
{

}
#endif
DLL_API signed char     F_API CS_IsPlaying(int channel)
{
	int status;
	ALuint source = GetSourceOfChannel(channel);
	alGetSourcei(source, AL_SOURCE_STATE, &status);
	if (status == AL_PLAYING)
	{
		return 1;
	}
	return 0;
}
#if 0
DLL_API signed char     F_API CS_GetReserved(int channel)
{
	return 0;
}
#endif
DLL_API unsigned int    F_API CS_GetLoopMode(int channel)
{
	return 0;
}
DLL_API unsigned int    F_API CS_GetCurrentPosition(int channel)
{
	int currbytes, size;
	if (channel < 0 || channel >= MAX_SOURCES)
	{
		//__builtin_trap();
		return SOURCE_OUT_OF_BOUNDS;
	}

	alGetSourcei(sources[channel], AL_BYTE_OFFSET, &currbytes);

	return currbytes;
}
DLL_API CS_SAMPLE * F_API CS_GetCurrentSample(int channel)
{
	return nullptr;
}
DLL_API signed char     F_API CS_GetCurrentLevels(int channel, float *l, float *r)
{
	return 0;
}

DLL_API signed char     F_API CS_DSP_MixBuffers(void *destbuffer, void *srcbuffer, int len, int freq, int vol, int pan, unsigned int mode)
{
	return 0;
}

DLL_API void            F_API CS_DSP_ClearMixBuffer()
{

}

DLL_API int             F_API CS_DSP_GetBufferLength()
{
	return 0;
}

DLL_API int             F_API CS_DSP_GetBufferLengthTotal()
{
	return 0;
}

DLL_API float *         F_API CS_DSP_GetSpectrum()
{
	return nullptr;
}

DLL_API CS_DSPUNIT *F_API CS_DSP_Create(CS_DSPCALLBACK callback, int priority, void *userdata)
{
	return nullptr;
}

DLL_API void            F_API CS_DSP_Free(CS_DSPUNIT *unit)
{
}

DLL_API void            F_API CS_DSP_SetActive(CS_DSPUNIT *unit, signed char active)
{
}

DLL_API unsigned int    F_API CS_Sample_GetLength(CS_SAMPLE *sptr)
{
	ALSample_t* samp = (ALSample_t*)sptr;
	ALint sizeInBytes;
	ALint channels;
	ALint bits;
	ALint frequency;
	int lengthInSamples;

	alGetBufferi(samp->buf, AL_SIZE, &sizeInBytes);
	alGetBufferi(samp->buf, AL_CHANNELS, &channels);
	alGetBufferi(samp->buf, AL_BITS, &bits);
	alGetBufferi(samp->buf, AL_FREQUENCY, &frequency);
	lengthInSamples = sizeInBytes * 8 / (channels * bits);

	return lengthInSamples;
}

DLL_API signed char     F_API CS_Sample_GetLoopPoints(CS_SAMPLE *sptr, int *loopstart, int *loopend)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_GetDefaults(CS_SAMPLE *sptr, int *deffreq, int *defvol, int *defpan, int *defpri)
{
	ALSample_t* samp = (ALSample_t*)sptr;
	if (deffreq != nullptr)
	{
		alGetBufferi(samp->buf, AL_FREQUENCY, deffreq);
	}

	return 0;
}

DLL_API signed char     F_API CS_Sample_GetDefaultsEx(CS_SAMPLE *sptr, int *deffreq, int *defvol, int *defpan, int *defpri, int *varfreq, int *varvol, int *varpan)
{
	return 0;
}

DLL_API int             F_API CS_PlaySound(int channel, CS_SAMPLE *sptr)
{
	return 0;
}

DLL_API int             F_API CS_PlaySoundEx(int channel, CS_SAMPLE *sptr, CS_DSPUNIT *dsp, signed char startpaused)
{
	ALSample_t* samp = (ALSample_t*)sptr;
	int i;
	ALuint src;

	if (channel == CS_FREE)
	{
		i = audio_next_available_source();
	}
	else
	{
		__builtin_trap();
		return -1;
	}

	if (i >= 0)
	{
		src = sources[i];
	}
	else
	{
		return -1;
	}

	if (i >= MAX_SOURCES)
	{
		__builtin_trap();
	}

	alSourcei(src, AL_BUFFER, samp->buf);
	if (SndDebug())
		fprintf(stderr, "SND: play source %d buf %d loop %d\n", i, samp->buf, (samp->flags & CS_LOOP_NORMAL) ? 1 : 0);
	alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
	alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(src, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	alSourcei(src, AL_LOOPING, samp->flags & CS_LOOP_NORMAL ? AL_TRUE : AL_FALSE);
	alSourcePlay(src);
	if (startpaused)
	{
		alSourcePause(src);
	}
	
	return i;
}

DLL_API signed char     F_API CS_StopSound(int channel)
{
	size_t i;
	if (channel >= MAX_SOURCES)
	{
		__builtin_trap();
		return SOURCE_OUT_OF_BOUNDS;
	}

	if (channel == CS_FREE)
	{
		for (i = 0; i < MAX_SOURCES; i++)
		{
			alSourceStop(sources[i]);
			alSourcei(sources[i], AL_BUFFER, 0);
		}
		return 1;
	}

	alSourceStop(sources[channel]);
	alSourcei(sources[channel], AL_BUFFER, 0);
	return 1;
}

DLL_API signed char     F_API CS_Sample_SetMode(CS_SAMPLE *sptr, unsigned int mode)
{
	ALSample_t* samp = (ALSample_t*)sptr;
	samp->flags = mode;
	return 1;
}

DLL_API signed char     F_API CS_Sample_SetLoopPoints(CS_SAMPLE *sptr, int loopstart, int loopend)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_SetDefaults(CS_SAMPLE *sptr, int deffreq, int defvol, int defpan, int defpri)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_SetMinMaxDistance(CS_SAMPLE *sptr, float min, float max)
{
	return 0;
}

DLL_API signed char     F_API CS_Sample_SetMaxPlaybacks(CS_SAMPLE *sptr, int max)
{
	return 0;
}

#ifndef LINUX64
DLL_API signed char   F_API   CS_Reverb_SetProperties(CS_REVERB_PROPERTIES *prop)
#else
DLL_API signed char   F_API   CS_Reverb_SetProperties(const CS_REVERB_PROPERTIES *prop)
#endif
{
	return 0;
}

DLL_API signed char     F_API CS_Reverb_GetProperties(CS_REVERB_PROPERTIES *prop)
{
	return 0;
}

DLL_API signed char     F_API CS_Reverb_SetChannelProperties(int channel, const CS_REVERB_CHANNELPROPERTIES *prop)
{
	return 0;
}

DLL_API signed char     F_API CS_Reverb_GetChannelProperties(int channel, CS_REVERB_CHANNELPROPERTIES *prop)
{
	return 0;
}