#include "audio/AsioBackend.h"

#if defined(GHOVLZ_WITH_ASIO) && defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>

#include "asio.h"
#include "asiodrivers.h"

#include "engine/Engine.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace ghovlz {
namespace {

AsioDrivers* drivers = nullptr;
Engine* bound = nullptr;
ASIOBufferInfo infos[4] {};
ASIOSampleType sampleType = ASIOSTFloat32LSB;
long bufferFrames = 256;
int inputCount = 0;
int outputCount = 0;
std::vector<float> interleavedIn;
std::vector<float> interleavedOut;
HANDLE mmcss = nullptr;

float readSample(const void* buffer, int index) {
    if (sampleType == ASIOSTFloat32LSB)
        return static_cast<const float*>(buffer)[index];
    if (sampleType == ASIOSTInt32LSB)
        return static_cast<const int32_t*>(buffer)[index] / 2147483648.f;
    if (sampleType == ASIOSTInt16LSB)
        return static_cast<const int16_t*>(buffer)[index] / 32768.f;
    return 0.f;
}

void writeSample(void* buffer, int index, float value) {
    if (sampleType == ASIOSTFloat32LSB)
        static_cast<float*>(buffer)[index] = value;
    else if (sampleType == ASIOSTInt32LSB)
        static_cast<int32_t*>(buffer)[index] = static_cast<int32_t>(std::clamp(value, -1.f, 1.f) * 2147483647.f);
    else if (sampleType == ASIOSTInt16LSB)
        static_cast<int16_t*>(buffer)[index] = static_cast<int16_t>(std::clamp(value, -1.f, 1.f) * 32767.f);
}

void bufferSwitch(long index, ASIOBool) {
    if (!mmcss) {
        DWORD task = 0;
        mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    }
    const int frames = static_cast<int>(bufferFrames);
    if (static_cast<int>(interleavedOut.size()) < frames * 2 || !bound)
        return;
    for (int i = 0; i < frames; ++i) {
        interleavedIn[static_cast<size_t>(i) * 2] = inputCount > 0 ? readSample(infos[0].buffers[index], i) : 0.f;
        interleavedIn[static_cast<size_t>(i) * 2 + 1] = inputCount > 1 ? readSample(infos[1].buffers[index], i) : interleavedIn[static_cast<size_t>(i) * 2];
    }
    if (bound)
        bound->process(interleavedOut.data(), interleavedIn.data(), frames);
    const int outBase = inputCount;
    for (int i = 0; i < frames; ++i) {
        if (outputCount > 0)
            writeSample(infos[outBase].buffers[index], i, interleavedOut[static_cast<size_t>(i) * 2]);
        if (outputCount > 1)
            writeSample(infos[outBase + 1].buffers[index], i, interleavedOut[static_cast<size_t>(i) * 2 + 1]);
    }
    ASIOOutputReady();
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool direct) {
    bufferSwitch(index, direct);
    return nullptr;
}

void sampleRateChanged(ASIOSampleRate) {}

long asioMessage(long selector, long, void*, double*) {
    if (selector == kAsioEngineVersion)
        return 2;
    if (selector == kAsioSupportsTimeInfo)
        return 1;
    return 0;
}

ASIOCallbacks callbacks {bufferSwitch, sampleRateChanged, asioMessage, bufferSwitchTimeInfo};

} // namespace

std::string AsioBackend::open(Engine* engine, const std::string& device, int sampleRate, int frames) {
    close();
    if (!drivers)
        drivers = new AsioDrivers();
    char names[16][128] {};
    char* ptrs[16];
    for (int i = 0; i < 16; ++i)
        ptrs[i] = names[i];
    const long count = drivers->getDriverNames(ptrs, 16);
    if (count < 1)
        return "no ASIO drivers";
    std::string chosen = device.empty() ? names[0] : device;
    bool found = false;
    for (long i = 0; i < count; ++i)
        if (chosen == names[i])
            found = true;
    if (!found)
        chosen = names[0];
    std::vector<char> mutableName(chosen.begin(), chosen.end());
    mutableName.push_back(0);
    if (!drivers->loadDriver(mutableName.data()))
        return "ASIO driver failed to load";
    ASIODriverInfo info {};
    info.asioVersion = 2;
    info.sysRef = GetForegroundWindow();
    if (ASIOInit(&info) != ASE_OK)
        return "ASIOInit failed";
    long ins = 0, outs = 0;
    ASIOGetChannels(&ins, &outs);
    inputCount = std::min(2, static_cast<int>(ins));
    outputCount = std::min(2, static_cast<int>(outs));
    if (outputCount < 1) {
        ASIOExit();
        return "ASIO driver has no outputs";
    }
    long minSize = 0, maxSize = 0, pref = 0, gran = 0;
    ASIOGetBufferSize(&minSize, &maxSize, &pref, &gran);
    bufferFrames = frames > 0 ? frames : pref;
    if (bufferFrames < minSize)
        bufferFrames = minSize;
    if (maxSize > 0 && bufferFrames > maxSize)
        bufferFrames = maxSize;
    ASIOSetSampleRate(sampleRate > 0 ? sampleRate : 48000);
    int n = 0;
    for (int c = 0; c < inputCount; ++c, ++n) {
        infos[n].isInput = ASIOTrue;
        infos[n].channelNum = c;
    }
    for (int c = 0; c < outputCount; ++c, ++n) {
        infos[n].isInput = ASIOFalse;
        infos[n].channelNum = c;
    }
    ASIOChannelInfo channel {};
    channel.channel = 0;
    channel.isInput = ASIOFalse;
    if (ASIOGetChannelInfo(&channel) == ASE_OK)
        sampleType = channel.type;
    if (ASIOCreateBuffers(infos, n, bufferFrames, &callbacks) != ASE_OK) {
        ASIOExit();
        return "ASIOCreateBuffers failed";
    }
    bound = engine;
    interleavedIn.assign(static_cast<size_t>(bufferFrames) * 2, 0.f);
    interleavedOut.assign(static_cast<size_t>(bufferFrames) * 2, 0.f);
    if (engine)
        engine->prepare(sampleRate > 0 ? sampleRate : 48000, static_cast<int>(bufferFrames));
    if (ASIOStart() != ASE_OK) {
        close();
        return "ASIOStart failed";
    }
    opened_ = true;
    name_ = chosen;
    return {};
}

void AsioBackend::close() {
    if (!opened_)
        return;
    ASIOStop();
    ASIODisposeBuffers();
    ASIOExit();
    if (drivers)
        drivers->removeCurrentDriver();
    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
        mmcss = nullptr;
    }
    bound = nullptr;
    opened_ = false;
    name_.clear();
}

const char* AsioBackend::name() const { return name_.empty() ? "ASIO" : name_.c_str(); }

} // namespace ghovlz

#else

namespace ghovlz {
std::string AsioBackend::open(Engine*, const std::string&, int, int) { return "ASIO is built on Windows when GHOVLZ_WITH_ASIO is on"; }
void AsioBackend::close() {}
const char* AsioBackend::name() const { return "ASIO"; }
} // namespace ghovlz

#endif
