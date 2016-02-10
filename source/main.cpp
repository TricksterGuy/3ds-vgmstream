#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>

extern "C"
{
    #include <dirent.h>
    #include <3ds.h>
    #include <util.h>
    #include <vgmstream.h>
}

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.hpp"
#include "version.hpp"

#define CONSOLE_WIDTH 50
#define CONSOLE_HEIGHT (28 - 1)

struct stream_buffer
{
    std::vector<sample*> channels;
    unsigned int samples;
};

struct stream_filename
{
    VGMSTREAM* stream;
    std::string filename;
};

enum class PlayMode
{
    /// Grabs samples piece by piece and plays it
    STREAM = 0,
    /// Loads entire song into memory and then plays it
    LOAD = 1,
};

std::vector<std::string> files;
unsigned int current_index = 0;
PlayMode mode = PlayMode::STREAM;

volatile bool runThreads = true;
/// Handle signaling more data is ready to be played
Handle bufferReadyConsumeRequest;
/// Handle signaling more data is ready to be decoded
Handle bufferReadyProduceRequest;

// At any point in time in stream mode one of these will be playing
// and the other will be used for unraveling channel data from vgmstream
stream_buffer playBuffer1;
stream_buffer playBuffer2;
// Raw samples from vgmstream
sample* rawSampleBuffer = NULL;


Result csndPlaySoundMulti(int chn, u32 flags, u32 sampleRate, float vol, float pan, const std::vector<sample*>& play_data, u32 size)
{
	if (!(csndChannels & BIT(chn)))
		return 1;

	std::vector<u32> play_paddr;
	play_paddr.reserve(play_data.size());

	int encoding = (flags >> 12) & 3;
	int loopMode = (flags >> 10) & 3;


	if (!loopMode) flags |= SOUND_ONE_SHOT;

	if (encoding != CSND_ENCODING_PSG)
	{
		for (const auto& addr : play_data)
            play_paddr.push_back(osConvertVirtToPhys(addr));

		/*if (data0 && encoding == CSND_ENCODING_ADPCM)
		{
			int adpcmSample = ((s16*)data0)[-2];
			int adpcmIndex = ((u8*)data0)[-2];
			CSND_SetAdpcmState(chn, 0, adpcmSample, adpcmIndex);
		}*/
	}

	u32 timer = CSND_TIMER(sampleRate);
	if (timer < 0x0042) timer = 0x0042;
	else if (timer > 0xFFFF) timer = 0xFFFF;
	flags &= ~0xFFFF001F;
	u32 volumes = CSND_VOL(vol, pan);

	int channels = play_data.size() / (loopMode ? 2 : 1);

	if (loopMode)
    {
        for (int i = 0; i < channels; i++)
        {
            u32 paddr0 = play_paddr[2 * i];
            u32 paddr1 = play_paddr[2 * i + 1];
            CSND_SetChnRegs(flags | SOUND_ENABLE | SOUND_CHANNEL(chn+i) | (timer << 16), paddr0, paddr1, size, volumes, volumes);
            size -= paddr1 - paddr0;
            CSND_SetBlock(chn, 1, paddr1, size);
        }
    }
    else
    {
        for (int i = 0; i < channels; i++)
        {
            CSND_SetChnRegs(flags | SOUND_ENABLE | SOUND_CHANNEL(chn+i) | (timer << 16), play_paddr[i], 0, size, volumes, volumes);
        }
    }

	return csndExecCmds(true);
}

void getFiles(void)
{
    struct dirent* dir;
    DIR* d = opendir(music_directory.c_str());
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            files.push_back(dir->d_name);
        }
        closedir(d);
    }

    std::sort(files.begin(), files.end());
}

void streamMusic(void* arg)
{
    stream_filename* strm_file = static_cast<stream_filename*>(arg);
    VGMSTREAM* vgmstream = strm_file->stream;

    if (!vgmstream)
        return;

    const int channels = vgmstream->channels;
    u32 audio_playback_status = 1;

    stream_buffer* buffer = &playBuffer1;
    // Wait for first data
    svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
    svcClearEvent(bufferReadyConsumeRequest);
    // Play it
    csndPlaySoundMulti(0x8, SOUND_ONE_SHOT | SOUND_FORMAT_16BIT, vgmstream->sample_rate, 1.0, 0.0, buffer->channels, buffer->samples * sizeof(sample));
    svcSignalEvent(bufferReadyProduceRequest);

    while (runThreads)
    {
        if (R_SUCCEEDED(csndIsPlaying(0x8, (u8*)&audio_playback_status)))
        {
            if (audio_playback_status == 0)
            {
                // Make sure the buffer is flushed.
                for (int i = 0; i < channels; i++)
                    GSPGPU_FlushDataCache(buffer->channels[i], buffer->samples * sizeof(sample));

                // Wait for sound data here
                svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
                svcClearEvent(bufferReadyConsumeRequest);

                // Flip buffers
                if (buffer == &playBuffer1)
                    buffer = &playBuffer2;
                else
                    buffer = &playBuffer1;

                csndPlaySoundMulti(0x8, SOUND_ONE_SHOT | SOUND_FORMAT_16BIT, vgmstream->sample_rate, 1.0, 0.0, buffer->channels, buffer->samples * sizeof(sample));
                audio_playback_status = 1;
                svcSignalEvent(bufferReadyProduceRequest);
            }
        }
    }

    for (int i = 0; i < channels; i++)
        GSPGPU_FlushDataCache(buffer->channels[i], buffer->samples * sizeof(sample));

    for (int i = 0; i < channels; i++)
    {
        CSND_SetPlayState(0x8 + i, 0);
        CSND_UpdateInfo(true);
    }
}

void decodeThread(void* arg)
{
    stream_filename* strm_file = static_cast<stream_filename*>(arg);
    VGMSTREAM* vgmstream = strm_file->stream;
    std::string& filename = strm_file->filename;

    if (!vgmstream)
        return;

    const int channels = vgmstream->channels;
    const u32 stream_samples_amount = get_vgmstream_play_samples(1, 0, 0, vgmstream);
    u32 current_sample_pos = 0;
    stream_buffer* buffer = &playBuffer1;

    while (runThreads)
    {
        u32 toget = max_samples;

        if (!vgmstream->loop_flag)
        {
            if (current_sample_pos >= stream_samples_amount)
                break;
            if (current_sample_pos + toget > stream_samples_amount)
                toget = stream_samples_amount - current_sample_pos;
        }

        // TODO modify render_vgmstream to return not decode channel data sequentially in the buffer passed in.
        render_vgmstream(rawSampleBuffer, toget, vgmstream);

        // Detangle audio data...
        buffer->samples = toget;
        for (u32 i = 0; i < max_samples; i++)
        {
            for (int j = 0; j < channels; j++)
            {
                buffer->channels[j][i] = rawSampleBuffer[i * channels + j];
            }
        }

        // Ready to play
        svcSignalEvent(bufferReadyConsumeRequest);

        consoleClear();
        printf("\x1b[1;0HCurrently playing %s\nPress B to choose another song\nPress Start to exit", filename.c_str());
        printf("\x1b[29;0HPLAYING %.4lf %.4lf\n", (float)current_sample_pos / vgmstream->sample_rate, (float)stream_samples_amount / vgmstream->sample_rate);
        current_sample_pos += toget;

        // Flip buffers
        if (buffer == &playBuffer1)
            buffer = &playBuffer2;
        else
            buffer = &playBuffer1;

        // Wait for signal to make another stream
        svcWaitSynchronization(bufferReadyProduceRequest, U64_MAX);
        svcClearEvent(bufferReadyProduceRequest);

    }

    close_vgmstream(vgmstream);
}

u32 getKeyState()
{
    static u32 last_held = 0;
    u32 held = hidKeysHeld();

    u32 ret = (~last_held) & held;
    last_held = held;
    return ret;
}

void refresh(void)
{
    consoleClear();
    unsigned int start, end;

    start = current_index + CONSOLE_HEIGHT >= files.size() ?
            (files.size() < CONSOLE_HEIGHT ? 0 : files.size() - CONSOLE_HEIGHT) :
            current_index;
    end = std::min(start + CONSOLE_HEIGHT, files.size() - 1);
    printf("3ds-vgmstream v%s - play mode: %s\n", version_str, (mode == PlayMode::STREAM ? "Stream" : "Play"));
    for (unsigned int i = start; i <= end; i++)
    {
        printf(i == current_index ? ">" : " ");
        printf("%s\n", files[i].c_str());
    }
}

std::string select_file(void)
{
    if (files.empty())
    {
        printf("Place music files in the following directory\n%s\non root of sd card\n\n", music_directory.c_str());
        return "";
    }

    refresh();

    bool quitting = false;
    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = getKeyState();
        if (kDown & KEY_START || kDown & KEY_A)
        {
            quitting = kDown & KEY_START;
            break;
        }
        if (kDown & KEY_UP)
        {
            current_index = (current_index == 0) ? 0 : current_index - 1;
            refresh();
        }
        if (kDown & KEY_DOWN)
        {
            current_index = std::min(current_index + 1, files.size() - 1);
            refresh();
        }
        if (kDown & KEY_R)
        {
            mode = (mode == PlayMode::STREAM) ? PlayMode::LOAD : PlayMode::STREAM;
            refresh();
        }

        gfxFlushBuffers();
        gfxSwapBuffers();

        gspWaitForVBlank();
    }

    if (quitting)
        return "";

    std::string ret = music_directory + "/" + files[current_index];
    return ret;
}

bool stream_file(const std::string& filename)
{
    if (filename.empty())
    {
        printf("No file selected\n");
        return true;
    }

    VGMSTREAM* vgmstream = init_vgmstream(filename.c_str());
    if (!vgmstream)
    {
        printf("Bad file %s\n", filename.c_str());
        return true;
    }

    const int channels = vgmstream->channels;
    u32 buffer_size = max_samples * vgmstream->channels * sizeof(sample);

    rawSampleBuffer = static_cast<sample*>(linearAlloc(buffer_size));
    sample* buffer = static_cast<sample*>(linearAlloc(buffer_size));
    sample* buffer2 = static_cast<sample*>(linearAlloc(buffer_size));
    for (int i = 0; i < channels; i++)
    {
        playBuffer1.channels.push_back(buffer + i * max_samples);
        playBuffer2.channels.push_back(buffer2 + i * max_samples);
    }

    stream_filename strm_file;
    strm_file.filename = filename;
    strm_file.stream = vgmstream;

    runThreads = true;

    s32 prio = 0;
    Thread musicThread;
    Thread produceThread;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    produceThread = threadCreate(decodeThread, &strm_file, 4 * 1024, prio-1, -2, false);
    musicThread = threadCreate(streamMusic, &strm_file, 4 * 1024, prio-1, -2, false);

    bool ret = false;
    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START || kDown & KEY_B)
        {
            ret = kDown & KEY_START;
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();

        gspWaitForVBlank();
    }

    runThreads = false;
    svcSignalEvent(bufferReadyProduceRequest);
    svcSignalEvent(bufferReadyConsumeRequest);
    threadJoin(musicThread, U64_MAX);
    threadJoin(produceThread, U64_MAX);
    threadFree(musicThread);
    threadFree(produceThread);


    linearFree(rawSampleBuffer);
    linearFree(buffer);
    linearFree(buffer2);
    playBuffer1.channels.clear();
    playBuffer2.channels.clear();

    return ret;
}

bool play_file(const std::string& filename)
{
    if (filename.empty())
    {
        printf("No file selected\n");
        return true;
    }

    VGMSTREAM* vgmstream = init_vgmstream(filename.c_str());
    if (!vgmstream)
    {
        printf("Bad file %s\n", filename.c_str());
        return true;
    }

    const int channels = vgmstream->channels;
    const u32 stream_samples_amount = get_vgmstream_play_samples(1, 0, 0, vgmstream);
    u32 buffer_size = stream_samples_amount * vgmstream->channels * sizeof(sample);

    rawSampleBuffer = static_cast<sample*>(linearAlloc(buffer_size));
    sample* buffer = static_cast<sample*>(linearAlloc(buffer_size));
    for (int i = 0; i < channels; i++)
        playBuffer1.channels.push_back(buffer + i * max_samples);

    render_vgmstream(rawSampleBuffer, stream_samples_amount, vgmstream);

    playBuffer1.samples = stream_samples_amount;
    for (u32 i = 0; i < playBuffer1.samples; i++)
    {
        for (int j = 0; j < channels; j++)
        {
            playBuffer1.channels[j][i] = rawSampleBuffer[i * channels + j];
        }
    }

    std::vector<sample*> addrs;
    for (int i = 0; i < channels; i++)
    {
        sample* start = playBuffer1.channels[i];
        sample* loop_start = start + vgmstream->loop_start_sample;
        addrs.push_back(start);
        addrs.push_back(loop_start);
    }
    csndPlaySoundMulti(0x8, SOUND_REPEAT | SOUND_FORMAT_16BIT, vgmstream->sample_rate, 1.0, 0.0, addrs, playBuffer1.samples * sizeof(sample));


    bool ret = false;
    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START || kDown & KEY_B)
        {
            ret = kDown & KEY_START;
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();

        gspWaitForVBlank();
    }

    linearFree(rawSampleBuffer);
    linearFree(buffer);
    playBuffer1.channels.clear();

    for (int i = 0; i < channels; i++)
    {
        CSND_SetPlayState(0x8 + i, 0);
        CSND_UpdateInfo(true);
    }

    return ret;
}

int main(void)
{
    gfxInitDefault();
    csndInit();

    consoleInit(GFX_TOP, NULL);

    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    svcCreateEvent(&bufferReadyConsumeRequest, 0);
    svcCreateEvent(&bufferReadyProduceRequest, 0);
    getFiles();

    bool exit = false;
    while (!exit)
    {
        std::string filename = select_file();
        exit = mode == PlayMode::STREAM ? stream_file(filename) : play_file(filename);
    }

    csndExit();
    gfxExit();

    return 0;
}
