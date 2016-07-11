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
    #include <stdarg.h>
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

std::vector<std::string> files;
unsigned int current_index = 0;

volatile bool runThreads = true;
/// Handle signaling more data is ready to be played
Handle bufferReadyConsumeRequest;
/// Handle signaling more data is ready to be decoded
Handle bufferReadyProduceRequest;
#ifdef DEBUG
LightLock debug_lock;
#endif

// At any point in time in stream mode one of these will be playing
// and the other will be used for unraveling channel data from vgmstream
stream_buffer playBuffer1;
stream_buffer playBuffer2;
// Raw samples from vgmstream
sample* rawSampleBuffer = NULL;

PrintConsole topScreen, bottomScreen;

static inline void print(const char *format, ...)
{
#ifdef DEBUG
    LightLock_Lock(&debug_lock);
#endif
    consoleSelect(&topScreen);
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
#ifdef DEBUG
    LightLock_Unlock(&debug_lock);
#endif
}

static inline void debug(const char *format, ...)
{
#ifdef DEBUG
    LightLock_Lock(&debug_lock);
    consoleSelect(&bottomScreen);
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    LightLock_Unlock(&debug_lock);
#endif
}

static inline void clearTopScreen(void)
{
    consoleSelect(&topScreen);
    consoleClear();
}

static inline void clearBottomScreen(void)
{
    consoleSelect(&bottomScreen);
    consoleClear();
}

/*
void AptEventHook(APT_HookType hookType, void* param)
{
    switch (hookType)
    {
        case APTHOOK_ONSUSPEND:
            paused = true;
            break;
        case APTHOOK_ONRESTORE:
            paused = false;
            break;
        case APTHOOK_ONSLEEP:
            break;
        case APTHOOK_ONWAKEUP:
            break;
        case APTHOOK_ONEXIT:
            runThreads = false;
            break;
        default:
            break;
    }
}
*/

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

void playSoundChannels(int startchn, int samples, bool loop, std::vector<sample*>& data, std::vector<ndspWaveBuf>& waveBufs)
{
    for (unsigned int i = 0; i < data.size(); i++)
    {
        int channel = startchn + i;
        waveBufs[i].data_vaddr = data[i];
        waveBufs[i].nsamples = samples / data.size();
        waveBufs[i].looping = loop;
        DSP_FlushDataCache(data[i], samples * sizeof(sample));
        ndspChnWaveBufAdd(channel, &waveBufs[i]);
    }
}

void streamMusic(void* arg)
{
    clearBottomScreen();
    debug("play_buffer start\n");
    stream_filename* strm_file = static_cast<stream_filename*>(arg);
    VGMSTREAM* vgmstream = strm_file->stream;

    if (!vgmstream)
        return;

	int channel = 0;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	for (int i = 0; i < vgmstream->channels; i++)
    {
        ndspChnReset(channel + i);
        ndspChnSetInterp(channel + i, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel + i, vgmstream->sample_rate / vgmstream->channels);
        ndspChnSetFormat(channel + i, NDSP_FORMAT_STEREO_PCM16);
    }

    std::vector<ndspWaveBuf> waveBufs1(vgmstream->channels);
    std::vector<ndspWaveBuf> waveBufs2(vgmstream->channels);
    for (auto& waveBuf : waveBufs1)
        memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    for (auto& waveBuf : waveBufs2)
        memset(&waveBuf, 0, sizeof(ndspWaveBuf));

    debug("play_buffer signal produce\n");
    svcSignalEvent(bufferReadyProduceRequest);
    // Wait for 2 buffers to play
    debug("play_buffer wait data 1\n");
    svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
    svcClearEvent(bufferReadyConsumeRequest);

    debug("play_buffer signal produce\n");
    svcSignalEvent(bufferReadyProduceRequest);
    debug("play_buffer wait data 2\n");
    svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
    svcClearEvent(bufferReadyConsumeRequest);

    // Play it
    debug("play_buffer play\n");
    playSoundChannels(channel, playBuffer1.samples, false, playBuffer1.channels, waveBufs1);
    playSoundChannels(channel, playBuffer2.samples, false, playBuffer2.channels, waveBufs2);
    stream_buffer* buffer = &playBuffer2;
    stream_buffer* playingBuf = &playBuffer1;
    std::vector<ndspWaveBuf>* waveBuf = &waveBufs2;
    std::vector<ndspWaveBuf>* playingWaveBuf = &waveBufs1;

    debug("play_buffer signal produce\n");
    svcSignalEvent(bufferReadyProduceRequest);

    while (runThreads)
    {
        if (playingWaveBuf->at(0).status == NDSP_WBUF_DONE) {
            debug("play_buffer wait data\n");
            // Wait for sound data here
            svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
            svcClearEvent(bufferReadyConsumeRequest);

            // Flip buffers
            if (buffer == &playBuffer1)
            {
                buffer = &playBuffer2;
                playingBuf = &playBuffer1;
                waveBuf = &waveBufs2;
                playingWaveBuf = &waveBufs1;
            }
            else
            {
                buffer = &playBuffer1;
                playingBuf = &playBuffer2;
                waveBuf = &waveBufs1;
                playingWaveBuf = &waveBufs2;
            }


            debug("play_buffer play\n");
            playSoundChannels(channel, buffer->samples, false, buffer->channels, *waveBuf);
            debug("play_buffer signal produce\n");
            svcSignalEvent(bufferReadyProduceRequest);
        }
    }

    for (int i = 0; i < vgmstream->channels; i++)
    {
        ndspChnWaveBufClear(channel + i);
    }
    debug("play_buffer done\n");

}

void decodeThread(void* arg)
{
    debug("decode_buffer start\n");
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
        debug("decode_buffer wait produce\n");
        // Wait for signal to make another stream
        svcWaitSynchronization(bufferReadyProduceRequest, U64_MAX);
        svcClearEvent(bufferReadyProduceRequest);

        u32 toget = max_samples;

        if (!vgmstream->loop_flag)
        {
            if (current_sample_pos >= stream_samples_amount)
                break;
            if (current_sample_pos + toget > stream_samples_amount)
                toget = stream_samples_amount - current_sample_pos;
        }

        debug("decode_buffer decode %d\n", toget);
        // TODO modify render_vgmstream to return not decode channel data sequentially in the buffer passed in.
        render_vgmstream(rawSampleBuffer, toget, vgmstream);

        debug("decode_buffer detangle\n");
        // Detangle audio data...
        buffer->samples = toget;
        for (u32 i = 0; i < max_samples; i++)
        {
            for (int j = 0; j < channels; j++)
            {
                buffer->channels[j][i] = rawSampleBuffer[i * channels + j];
            }
        }

        debug("decode_buffer signal consume\n");
        // Ready to play
        svcSignalEvent(bufferReadyConsumeRequest);

        clearTopScreen();
        print("\x1b[1;0HCurrently playing %s\nPress B to choose another song\nPress Start to exit", filename.c_str());
        print("\x1b[29;0HPLAYING %.4lf %.4lf\n", (float)current_sample_pos / vgmstream->sample_rate, (float)stream_samples_amount / vgmstream->sample_rate);
        current_sample_pos += toget;

        // Flip buffers
        if (buffer == &playBuffer1)
            buffer = &playBuffer2;
        else
            buffer = &playBuffer1;

        debug("decode_buffer decode more\n");
    }
    debug("decode_buffer done\n");
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
    clearTopScreen();
    unsigned int start, end;

    start = current_index + CONSOLE_HEIGHT >= files.size() ?
            (files.size() < CONSOLE_HEIGHT ? 0 : files.size() - CONSOLE_HEIGHT) :
            current_index;
    end = std::min(start + CONSOLE_HEIGHT, files.size() - 1);
    print("3ds-vgmstream v%s\n", version_str);
    for (unsigned int i = start; i <= end; i++)
    {
        print(i == current_index ? ">" : " ");
        print("%s\n", files[i].c_str());
    }
}

std::string select_file(void)
{
    if (files.empty())
    {
        print("Place music files in the following directory\n%s\non root of sd card\n\n", music_directory.c_str());
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
        print("No file selected\n");
        return true;
    }

    VGMSTREAM* vgmstream = init_vgmstream(filename.c_str());
    if (!vgmstream)
    {
        print("Bad file %s\n", filename.c_str());
        return true;
    }

    const int channels = vgmstream->channels;
    u32 buffer_size = max_samples * vgmstream->channels * sizeof(sample);

    rawSampleBuffer = static_cast<sample*>(linearAlloc(buffer_size));
    sample* buffer = static_cast<sample*>(linearAlloc(buffer_size));
    sample* buffer2 = static_cast<sample*>(linearAlloc(buffer_size));
    playBuffer1.samples = max_samples;
    playBuffer2.samples = max_samples;
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
    musicThread = threadCreate(streamMusic, &strm_file, 4 * 1024, prio-1, -2, false);
    produceThread = threadCreate(decodeThread, &strm_file, 4 * 1024, prio-1, -2, false);

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
    svcClearEvent(bufferReadyConsumeRequest);
    svcClearEvent(bufferReadyProduceRequest);


    linearFree(rawSampleBuffer);
    linearFree(buffer);
    linearFree(buffer2);
    playBuffer1.channels.clear();
    playBuffer2.channels.clear();

    close_vgmstream(vgmstream);

    return ret;
}

int main(void)
{
    gfxInitDefault();

	if(R_FAILED(ndspInit()))
		return 0;

#ifdef DEBUG
    LightLock_Init(&debug_lock);
    consoleInit(GFX_BOTTOM, &bottomScreen);
    consoleDebugInit(debugDevice_CONSOLE);
#endif

    consoleInit(GFX_TOP, &topScreen);
    //aptHook(&hookCookie, AptEventHook, NULL);

    svcCreateEvent(&bufferReadyConsumeRequest, RESET_STICKY);
    svcCreateEvent(&bufferReadyProduceRequest, RESET_STICKY);
    getFiles();

    bool exit = false;
    while (!exit)
    {
        std::string filename = select_file();
        exit = stream_file(filename);
    }

    ndspExit();
    gfxExit();

    return 0;
}
