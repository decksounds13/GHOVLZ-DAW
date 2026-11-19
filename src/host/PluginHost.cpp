#include "host/PluginHost.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghovlz {
namespace {

std::string utf16ToAscii(const Steinberg::Vst::TChar* s) {
    std::string out;
    if (!s) return out;
    for (int i = 0; i < 128 && s[i]; ++i)
        out.push_back(static_cast<char>(s[i] & 0x7F));
    return out;
}

std::string base64Encode(const char* data, size_t n) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < n) v |= static_cast<unsigned char>(data[i + 1]) << 8;
        if (i + 2 < n) v |= static_cast<unsigned char>(data[i + 2]);
        o.push_back(k[(v >> 18) & 63]);
        o.push_back(k[(v >> 12) & 63]);
        o.push_back(i + 1 < n ? k[(v >> 6) & 63] : '=');
        o.push_back(i + 2 < n ? k[v & 63] : '=');
    }
    return o;
}
std::string base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string o;
    int buf = 0, bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return o;
}

int runProcess(const std::string& exe, const std::string& arg, std::string& output) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return 127;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exe + "\" \"" + arg + "\"";
    std::vector<char> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return 127;
    }
    char buf[512];
    DWORD n = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0)
        output.append(buf, buf + n);
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return static_cast<int>(code);
#else
    int fds[2];
    if (pipe(fds) != 0)
        return 127;
    pid_t pid = fork();
    if (pid < 0)
        return 127;
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        execl(exe.c_str(), exe.c_str(), arg.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);
    char buf[512];
    ssize_t n = 0;
    while ((n = ::read(fds[0], buf, sizeof(buf))) > 0)
        output.append(buf, buf + n);
    close(fds[0]);
    int status = 1;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}

class Vst3Device : public Device {
public:
    Steinberg::Vst::HostApplication host;
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    std::map<std::string, Steinberg::Vst::ParamID> ids;
    Steinberg::Vst::HostProcessData processData;
    bool prepared = false;
    double sr = 48000;
    int maxBlock = 512;

    ~Vst3Device() override { shutdown(); }

    void shutdown() {
        if (processor)
            processor->setProcessing(false);
        if (component)
            component->setActive(false);
        if (component)
            component->terminate();
        processor.reset();
        controller.reset();
        component.reset();
        module.reset();
    }

    void prepare(double sampleRate, int block) override {
        sr = sampleRate;
        maxBlock = std::max(32, block);
        if (!processor || !component)
            return;
        using namespace Steinberg::Vst;
        component->activateBus(kAudio, kInput, 0, true);
        component->activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = maxBlock;
        setup.sampleRate = sampleRate;
        processor->setupProcessing(setup);
        component->setActive(true);
        processor->setProcessing(true);
        // bufferSamples == 0: the host fills channel pointers each block.
        processData.prepare(*component, 0, kSample32);
        prepared = true;
    }

    void process(AudioBuffer& audio, MidiBuffer&) override {
        if (bypass.load() || !prepared || !processor)
            return;
        using namespace Steinberg::Vst;
        const int frames = std::min(audio.frames, maxBlock);
        std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
        if (audio.channels > 0)
            std::copy(audio.ch(0), audio.ch(0) + frames, inL.begin());
        if (audio.channels > 1)
            std::copy(audio.ch(1), audio.ch(1) + frames, inR.begin());
        else
            inR = inL;
        for (const auto& p : params) {
            auto it = ids.find(p.id);
            if (it != ids.end() && controller)
                controller->setParamNormalized(it->second, p.value);
        }
        float* ins[2] = {inL.data(), inR.data()};
        float* outs[2] = {outL.data(), outR.data()};
        processData.setChannelBuffers(kInput, 0, ins, 2);
        processData.setChannelBuffers(kOutput, 0, outs, 2);
        processData.numSamples = frames;
        processor->process(processData);
        if (audio.channels > 0)
            std::copy(outL.begin(), outL.end(), audio.ch(0));
        if (audio.channels > 1)
            std::copy(outR.begin(), outR.end(), audio.ch(1));
    }

    int latency() const override {
        if (!processor)
            return 0;
        return static_cast<int>(processor->getLatencySamples());
    }

    std::string pluginState() const override {
        if (!component)
            return {};
        Steinberg::MemoryStream stream;
        if (component->getState(&stream) != Steinberg::kResultOk)
            return {};
        return base64Encode(stream.getData(), static_cast<size_t>(stream.getSize()));
    }
    void setPluginState(const std::string& encoded) override {
        if (!component || encoded.empty())
            return;
        const std::string raw = base64Decode(encoded);
        Steinberg::MemoryStream stream;
        stream.write(const_cast<char*>(raw.data()), static_cast<Steinberg::int32>(raw.size()), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        component->setState(&stream);
    }
};

} // namespace

void PluginHost::setScannerExecutable(std::string path) { scanner_ = std::move(path); }
void PluginHost::setBlacklistFile(std::string path) { blacklist_ = std::move(path); }

bool PluginHost::isBlacklisted(const std::string& pluginPath) const {
    std::ifstream in(blacklist_);
    std::string line;
    while (std::getline(in, line))
        if (line == pluginPath)
            return true;
    return false;
}

bool PluginHost::scan(const std::string& pluginPath, std::string& error) {
    if (isBlacklisted(pluginPath)) {
        error = "blacklisted";
        return false;
    }
    if (scanner_.empty()) {
        error = "scanner executable is not set";
        return false;
    }
    std::string output;
    const int code = runProcess(scanner_, pluginPath, output);
    if (code != 0) {
        std::ofstream out(blacklist_, std::ios::app);
        out << pluginPath << "\n";
        error = "scanner exit " + std::to_string(code);
        return false;
    }
    lastName_.clear();
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("NAME\t", 0) == 0)
            lastName_ = line.substr(5);
    }
    if (lastName_.empty())
        lastName_ = "VST3";
    return true;
}

std::shared_ptr<Device> PluginHost::instantiate(const std::string& pluginPath, std::string& error) {
    if (isBlacklisted(pluginPath)) {
        error = "blacklisted";
        return nullptr;
    }
    auto dev = std::make_shared<Vst3Device>();
    dev->module = VST3::Hosting::Module::create(pluginPath, error);
    if (!dev->module)
        return nullptr;
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    const auto& factory = dev->module->getFactory();
    factory.setHostContext(&dev->host);
    for (const auto& ci : factory.classInfos()) {
        if (ci.category() != kVstAudioEffectClass)
            continue;
        dev->component = factory.createInstance<IComponent>(ci.ID());
        if (!dev->component) {
            error = "createInstance failed";
            return nullptr;
        }
        if (dev->component->initialize(&dev->host) != kResultOk) {
            error = "initialize failed";
            return nullptr;
        }
        dev->processor = FUnknownPtr<IAudioProcessor>(dev->component);
        dev->controller = FUnknownPtr<IEditController>(dev->component);
        if (!dev->processor) {
            error = "no audio processor";
            return nullptr;
        }
        if (dev->controller) {
            const int32 count = dev->controller->getParameterCount();
            for (int32 i = 0; i < count; ++i) {
                ParameterInfo info{};
                if (dev->controller->getParameterInfo(i, info) != kResultOk)
                    continue;
                if (info.flags & ParameterInfo::kIsReadOnly)
                    continue;
                Param p;
                p.name = utf16ToAscii(info.title);
                p.id = p.name.empty() ? std::to_string(info.id) : p.name;
                p.minV = 0.f;
                p.maxV = 1.f;
                p.value = static_cast<float>(dev->controller->getParamNormalized(info.id));
                dev->params.push_back(p);
                dev->ids[p.id] = info.id;
            }
        }
        dev->type = "vst3";
        dev->name = ci.name();
        lastName_ = ci.name();
        return dev;
    }
    error = "no audio effect class";
    return nullptr;
}

void PluginHost::setState(Device& device, const std::string& encoded) { device.setPluginState(encoded); }
std::string PluginHost::stateOf(const Device& device) const { return device.pluginState(); }

} // namespace ghovlz
