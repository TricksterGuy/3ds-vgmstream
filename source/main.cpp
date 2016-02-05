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

#define CONSOLE_WIDTH 50
#define CONSOLE_HEIGHT 28


std::vector<std::string> files;
unsigned int current_index = 0;

volatile bool runThreads = true;
/// Handle signalling more data is ready to be played
Handle bufferReadyConsumeRequest;
/// Handle signalling more data is ready to be decoded
Handle bufferReadyProduceRequest;

struct stream_buffer
{
    std::vector<sample*> channels;
    unsigned int samples;
};

// At any point in time one of these will be playing
// and the other will be used for unraveling channel data from vgmstream
stream_buffer playBuffer1;
stream_buffer playBuffer2;
// Raw samples from vgmstream
sample* rawSampleBuffer = NULL;

struct stream_filename
{
    VGMSTREAM* stream;
    std::string filename;
};

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

void playMusic(void* arg)
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
    for (int i = 0; i < channels; i++)
    {
        csndPlaySound(0x8 + i, SOUND_ONE_SHOT | SOUND_FORMAT_16BIT, vgmstream->sample_rate, 1.0, 0.0, (u32*)buffer->channels[i], NULL, buffer->samples * sizeof(sample));
    }
    svcSignalEvent(bufferReadyProduceRequest);

    while (runThreads)
    {
        if (R_SUCCEEDED(csndIsPlaying(0x8, (u8*)&audio_playback_status)))
        {
            if (audio_playback_status == 0)
            {
                // Make sure the buffer is flushed.
                unsigned int samples = buffer->samples;
                for (int i = 0; i < channels; i++)
                    GSPGPU_FlushDataCache(buffer->channels[i], samples * sizeof(sample));

                // Wait for sound data here
                svcWaitSynchronization(bufferReadyConsumeRequest, U64_MAX);
                svcClearEvent(bufferReadyConsumeRequest);

                // Flip buffers
                if (buffer == &playBuffer1)
                    buffer = &playBuffer2;
                else
                    buffer = &playBuffer1;

                samples = buffer->samples;
                for (int i = 0; i < channels; i++)
                {
                    csndPlaySound(0x8 + i, SOUND_ONE_SHOT | SOUND_FORMAT_16BIT, vgmstream->sample_rate, 1.0, 0.0, (u32*)buffer->channels[i], NULL, samples *sizeof(sample));
                }
                audio_playback_status = 1;
                svcSignalEvent(bufferReadyProduceRequest);
            }
        }
    }

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
    musicThread = threadCreate(playMusic, &strm_file, 4 * 1024, prio-1, -2, false);

    bool ret;
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
        exit = play_file(filename);
    }

    csndExit();
    gfxExit();

    return 0;
}
