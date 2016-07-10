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

ndspWaveBuf waveBufs[23];

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

// At any point in time in stream mode one of these will be playing
// and the other will be used for unraveling channel data from vgmstream
stream_buffer playBuffer1;
stream_buffer playBuffer2;
// Raw samples from vgmstream
sample* rawSampleBuffer = NULL;

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

void playSoundChannels(int startchn, int samples, bool loop, std::vector<sample*>& data)
{
    for (unsigned int i = 0; i < data.size(); i++)
    {
        int channel = startchn + i;
        ndspChnWaveBufClear(channel);
        memset(&waveBufs[channel], 0, sizeof(ndspWaveBuf));
        waveBufs[channel].data_vaddr = data[i];
        waveBufs[channel].nsamples = samples / data.size();
        waveBufs[channel].looping = loop;
        DSP_FlushDataCache(data[i], samples * sizeof(sample));
        ndspChnWaveBufAdd(channel, &waveBufs[channel]);
    }
}

void streamMusic(void* arg)
{
    stream_filename* strm_file = static_cast<stream_filename*>(arg);
    VGMSTREAM* vgmstream = strm_file->stream;

    if (!vgmstream)
        return;

    stream_buffer* buffer = &playBuffer1;
    // Wait for first data
    svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
    svcClearEvent(bufferReadyConsumeRequest);

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	float mix[12] = {0};
	mix[0] = mix[1] = 1.0;

	int channel = 0;
	for (int i = 0; i < vgmstream->channels; i++)
    {
        ndspChnReset(channel + i);
        ndspChnSetMix(channel + i, mix);
        ndspChnSetInterp(channel + i, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel + i, vgmstream->sample_rate / vgmstream->channels);
        ndspChnSetFormat(channel + i, NDSP_FORMAT_STEREO_PCM16);
    }

    // Play it
    playSoundChannels(channel, buffer->samples, false, buffer->channels);
    svcSignalEvent(bufferReadyProduceRequest);
    svcSleepThread((u64)buffer->samples * 1000000000 / vgmstream->sample_rate);

    while (runThreads)
    {
        // Wait for sound data here
        svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
        svcClearEvent(bufferReadyConsumeRequest);

        // Flip buffers
        if (buffer == &playBuffer1)
            buffer = &playBuffer2;
        else
            buffer = &playBuffer1;

        playSoundChannels(channel, buffer->samples, false, buffer->channels);
        svcSignalEvent(bufferReadyProduceRequest);
        svcSleepThread((u64)buffer->samples * 1000000000 / vgmstream->sample_rate);

        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
    }

    for (int i = 0; i < vgmstream->channels; i++)
        ndspChnWaveBufClear(channel + i);
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
        printf("\x1b[29;0HPLAYING %.4lf %.4lf\n", (float)current_sample_pos / vgmstream->sample_rate, (float)/*stream_samples_amount /*/ vgmstream->sample_rate);
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
    printf("3ds-vgmstream v%s\n", version_str);
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

    close_vgmstream(vgmstream);

    return ret;
}

int main(void)
{
    gfxInitDefault();

	if(R_FAILED(ndspInit()))
		return 0;

    consoleInit(GFX_TOP, NULL);

    gfxSetDoubleBuffering(GFX_BOTTOM, false);

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
