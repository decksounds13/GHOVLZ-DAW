#include "host/PluginHost.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
#include <poll.h>
#include <signal.h>
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
    const auto started = std::chrono::steady_clock::now();
    char buf[512];
    auto drain = [&] {
        // A fast scanner can exit between the peek and the wait. Read whatever
        // is left after the process handle is signaled, or the NAME line is lost.
        while (true) {
            DWORD avail = 0;
            DWORD n = 0;
            if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
                break;
            if (!ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) || n == 0)
                break;
            output.append(buf, buf + n);
        }
    };
    bool finished = false;
    while (!finished) {
        drain();
        const DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0)
            finished = true;
        else if (std::chrono::steady_clock::now() - started > std::chrono::seconds(20)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            finished = true;
        }
    }
    // The last flush can become readable just after the process handle is signaled.
    for (int i = 0; i < 5; ++i) {
        const auto before = output.size();
        drain();
        if (output.size() != before)
            i = 0;
        else
            Sleep(10);
    }
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
    const auto started = std::chrono::steady_clock::now();
    char buf[512];
    while (true) {
        pollfd pfd {};
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, 200);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            const ssize_t n = ::read(fds[0], buf, sizeof(buf));
            if (n > 0)
                output.append(buf, buf + n);
            else {
                int status = 0;
                waitpid(pid, &status, 0);
                close(fds[0]);
                if (WIFEXITED(status))
                    return WEXITSTATUS(status);
                if (WIFSIGNALED(status))
                    return 128 + WTERMSIG(status);
                return 1;
            }
        }
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            ssize_t n = 0;
            while ((n = ::read(fds[0], buf, sizeof(buf))) > 0)
                output.append(buf, buf + n);
            close(fds[0]);
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            if (WIFSIGNALED(status))
                return 128 + WTERMSIG(status);
            return 1;
        }
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(20)) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            close(fds[0]);
            return 128 + SIGKILL;
        }
    }
#endif
}

class FixedParamQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    Steinberg::Vst::ParamID pid = 0;
    int count = 0;
    Steinberg::int32 offsets[8] {};
    Steinberg::Vst::ParamValue values[8] {};
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        const Steinberg::FUID id = Steinberg::FUID::fromTUID(iid);
        if (id == Steinberg::Vst::IParamValueQueue::iid || id == Steinberg::FUnknown::iid) {
            *obj = static_cast<Steinberg::Vst::IParamValueQueue*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return pid; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return count; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& offset, Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
        offset = offsets[index];
        value = values[index];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 offset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override {
        if (count >= 8)
            return Steinberg::kResultFalse;
        index = count;
        offsets[count] = offset;
        values[count] = value;
        ++count;
        return Steinberg::kResultTrue;
    }
};

class FixedChanges final : public Steinberg::Vst::IParameterChanges {
public:
    FixedParamQueue queues[32];
    int count = 0;
    void reset() { count = 0; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        const Steinberg::FUID id = Steinberg::FUID::fromTUID(iid);
        if (id == Steinberg::Vst::IParameterChanges::iid || id == Steinberg::FUnknown::iid) {
            *obj = static_cast<Steinberg::Vst::IParameterChanges*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return count; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        return index >= 0 && index < count ? &queues[index] : nullptr;
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override {
        if (count >= 32)
            return nullptr;
        index = count;
        queues[count].pid = id;
        queues[count].count = 0;
        return &queues[count++];
    }
};

class FixedEvents final : public Steinberg::Vst::IEventList {
public:
    Steinberg::Vst::Event items[64] {};
    int count = 0;
    void reset() { count = 0; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        const Steinberg::FUID id = Steinberg::FUID::fromTUID(iid);
        if (id == Steinberg::Vst::IEventList::iid || id == Steinberg::FUnknown::iid) {
            *obj = static_cast<Steinberg::Vst::IEventList*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return count; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
        e = items[index];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        if (count >= 64)
            return Steinberg::kResultFalse;
        items[count++] = e;
        return Steinberg::kResultTrue;
    }
};

class EditHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        const Steinberg::FUID id = Steinberg::FUID::fromTUID(iid);
        if (id == Steinberg::Vst::IComponentHandler::iid || id == Steinberg::FUnknown::iid) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
    std::atomic<int> calls {0};
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override {
        calls.fetch_add(1, std::memory_order_relaxed);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override {
        calls.fetch_add(1, std::memory_order_relaxed);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override {
        calls.fetch_add(1, std::memory_order_relaxed);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }
};

class Vst3Device : public Device {
public:
    Steinberg::Vst::HostApplication host;
    EditHandler edits;
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::IPlugView> view;
    void* editorParent = nullptr;
    std::map<std::string, Steinberg::Vst::ParamID> ids;
    Steinberg::Vst::HostProcessData processData;
    std::vector<float> inL, inR, outL, outR;
    std::vector<Steinberg::Vst::ParamID> paramIds;
    std::vector<float> sent;
    FixedChanges changes;
    FixedEvents events;
    bool separateController = false;
    bool prepared = false;
    double sr = 48000;
    int maxBlock = 512;
    double transportBpm = 120;
    double transportPpq = 0;
    bool transportPlaying = false;
    int64_t transportSamples = 0;

    ~Vst3Device() override { shutdown(); }

    void shutdown() {
        if (view) {
            if (editorParent)
                view->removed();
            view.reset();
            editorParent = nullptr;
        }
        if (processor)
            processor->setProcessing(false);
        if (component)
            component->setActive(false);
        if (separateController && controller && component) {
            Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> from(component);
            Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> to(controller);
            if (from && to) {
                from->disconnect(to);
                to->disconnect(from);
            }
            controller->terminate();
        }
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
        component->activateBus(kEvent, kInput, 0, true);
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
        inL.assign(static_cast<size_t>(maxBlock), 0.f);
        inR.assign(static_cast<size_t>(maxBlock), 0.f);
        outL.assign(static_cast<size_t>(maxBlock), 0.f);
        outR.assign(static_cast<size_t>(maxBlock), 0.f);
        sent.assign(params.size(), -1.f);
        prepared = true;
    }

    void process(AudioBuffer& audio, MidiBuffer& midi) override {
        if (bypass.load() || !prepared || !processor)
            return;
        using namespace Steinberg::Vst;
        const int nparams = std::min(static_cast<int>(params.size()), static_cast<int>(paramIds.size()));
        float* ins[2] = {inL.data(), inR.data()};
        float* outs[2] = {outL.data(), outR.data()};
        Steinberg::Vst::ProcessContext context{};
        for (int done = 0; done < audio.frames; done += maxBlock) {
            const int n = std::min(maxBlock, audio.frames - done);
            changes.reset();
            events.reset();
            if (done == 0) {
                for (int i = 0; i < nparams && i < static_cast<int>(sent.size()); ++i) {
                    const float v = params[static_cast<size_t>(i)].target.load(std::memory_order_relaxed);
                    if (v == sent[static_cast<size_t>(i)])
                        continue;
                    Steinberg::int32 index = 0;
                    if (auto* queue = changes.addParameterData(paramIds[static_cast<size_t>(i)], index)) {
                        Steinberg::int32 point = 0;
                        queue->addPoint(0, v, point);
                        sent[static_cast<size_t>(i)] = v;
                    }
                }
            }
            for (const auto& ev : midi.events) {
                if (ev.sample < done || ev.sample >= done + n || events.count >= 64)
                    continue;
                Event e {};
                e.busIndex = 0;
                e.sampleOffset = ev.sample - done;
                e.ppqPosition = transportPpq + static_cast<double>(ev.sample) / std::max(1.0, sr) * transportBpm / 60.0;
                const uint8_t kind = ev.status & 0xF0;
                if (kind == 0x90 && ev.data2 > 0) {
                    e.type = Event::kNoteOnEvent;
                    e.noteOn.channel = ev.status & 0x0F;
                    e.noteOn.pitch = ev.data1;
                    e.noteOn.velocity = ev.data2 / 127.f;
                    e.noteOn.noteId = ev.data1;
                } else if (kind == 0x80 || (kind == 0x90 && ev.data2 == 0)) {
                    e.type = Event::kNoteOffEvent;
                    e.noteOff.channel = ev.status & 0x0F;
                    e.noteOff.pitch = ev.data1;
                    e.noteOff.velocity = 0.f;
                    e.noteOff.noteId = ev.data1;
                } else {
                    continue;
                }
                events.items[events.count++] = e;
            }
            for (int i = 0; i < n; ++i) {
                inL[static_cast<size_t>(i)] = audio.channels > 0 ? audio.ch(0)[done + i] : 0.f;
                inR[static_cast<size_t>(i)] = audio.channels > 1 ? audio.ch(1)[done + i] : inL[static_cast<size_t>(i)];
                outL[static_cast<size_t>(i)] = 0.f;
                outR[static_cast<size_t>(i)] = 0.f;
            }
            context.tempo = transportBpm;
            context.sampleRate = sr;
            context.projectTimeSamples = transportSamples + done;
            context.projectTimeMusic = transportPpq + static_cast<double>(done) / std::max(1.0, sr) * transportBpm / 60.0;
            context.state = transportPlaying ? ProcessContext::kPlaying : 0;
            context.state |= ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
            processData.processContext = &context;
            processData.setChannelBuffers(kInput, 0, ins, 2);
            processData.setChannelBuffers(kOutput, 0, outs, 2);
            processData.numSamples = n;
            processData.inputParameterChanges = &changes;
            processData.inputEvents = &events;
            processor->process(processData);
            for (int i = 0; i < n; ++i) {
                if (audio.channels > 0)
                    audio.ch(0)[done + i] = outL[static_cast<size_t>(i)];
                if (audio.channels > 1)
                    audio.ch(1)[done + i] = outR[static_cast<size_t>(i)];
            }
        }
    }

    bool editParam(const std::string& param) override {
        if (!controller)
            return false;
        const auto it = ids.find(param);
        if (it == ids.end())
            return false;
        using namespace Steinberg::Vst;
        const ParamID id = it->second;
        const ParamValue value = controller->getParamNormalized(id);
        edits.calls.store(0, std::memory_order_relaxed);
        if (controller->setParamNormalized(id, value) != Steinberg::kResultOk)
            return false;
        return edits.calls.load(std::memory_order_relaxed) == 3;
    }

    void setTransport(double bpm, double ppq, bool playing, int64_t samples) override {
        transportBpm = bpm > 0 ? bpm : 120;
        transportPpq = ppq;
        transportPlaying = playing;
        transportSamples = samples;
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
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        if (controller)
            controller->setComponentState(&stream);
        if (controller) {
            for (auto& param : params) {
                auto it = ids.find(param.id);
                if (it == ids.end())
                    continue;
                param.value = static_cast<float>(controller->getParamNormalized(it->second));
                param.target.store(param.value, std::memory_order_relaxed);
                param.smoothed = param.value;
            }
        }
    }
};

} // namespace

void PluginHost::setScannerExecutable(std::string path) { scanner_ = std::move(path); }
void PluginHost::setBlacklistFile(std::string path) { blacklist_ = std::move(path); }

bool PluginHost::isBlacklisted(const std::string& pluginPath) const {
    std::ifstream in(blacklist_);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == pluginPath)
            return true;
    }
    return false;
}

namespace {
void stripCr(std::string& line) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

std::string cacheStamp(const std::string& path) {
    std::error_code ec;
    const auto when = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
    if (ec)
        return {};
    return std::to_string(static_cast<long long>(when));
}
} // namespace

bool PluginHost::scan(const std::string& pluginPath, std::string& error) {
    if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) {
        error = "missing plugin";
        return false;
    }
    if (isBlacklisted(pluginPath)) {
        error = "blacklisted";
        return false;
    }
    if (scanner_.empty()) {
        error = "scanner executable is not set";
        return false;
    }
    const std::string stamp = cacheStamp(pluginPath);
    const std::string cachePath = blacklist_ + ".scan";
    {
        std::ifstream cache(cachePath);
        std::string line;
        while (std::getline(cache, line)) {
            stripCr(line);
            const auto t1 = line.find('\t');
            const auto t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos)
                continue;
            if (line.substr(0, t1) == pluginPath && line.substr(t1 + 1, t2 - t1 - 1) == stamp) {
                lastName_ = line.substr(t2 + 1);
                stripCr(lastName_);
                return true;
            }
        }
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
        // The scanner is a text-mode process. On Windows its newline is CRLF,
        // and the pipe is read as bytes, so getline keeps the CR.
        stripCr(line);
        if (line.rfind("NAME\t", 0) == 0)
            lastName_ = line.substr(5);
    }
    if (lastName_.empty())
        lastName_ = "VST3";
    if (!stamp.empty()) {
        std::ofstream cache(cachePath, std::ios::app);
        cache << pluginPath << '\t' << stamp << '\t' << lastName_ << '\n';
    }
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
        if (!dev->controller) {
            TUID cid {};
            if (dev->component->getControllerClassId(cid) == kResultOk) {
                dev->controller = factory.createInstance<IEditController>(cid);
                if (dev->controller && dev->controller->initialize(&dev->host) == kResultOk) {
                    dev->separateController = true;
                    FUnknownPtr<IConnectionPoint> from(dev->component);
                    FUnknownPtr<IConnectionPoint> to(dev->controller);
                    if (from && to) {
                        from->connect(to);
                        to->connect(from);
                    }
                } else if (dev->controller) {
                    dev->controller->terminate();
                    dev->controller.reset();
                }
            }
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
                p.target.store(p.value, std::memory_order_relaxed);
                p.smoothed = p.value;
                dev->params.push_back(p);
                dev->ids[p.id] = info.id;
                dev->paramIds.push_back(info.id);
            }
        }
        if (dev->controller)
            dev->controller->setComponentHandler(&dev->edits);
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

bool PluginHost::openEditor(Device& device, void* parent, EditorAttach& out, std::string& error) {
    auto* vst = dynamic_cast<Vst3Device*>(&device);
    if (!vst || !vst->controller) {
        error = "no editor controller";
        return false;
    }
    if (vst->view) {
        error = "editor already open";
        return false;
    }
    Steinberg::IPlugView* created = vst->controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!created) {
        error = "plugin has no view";
        return false;
    }
    vst->view = created;
    created->release();
#if defined(_WIN32)
    const char* platform = Steinberg::kPlatformTypeHWND;
#else
    const char* platform = Steinberg::kPlatformTypeX11EmbedWindowID;
#endif
    if (vst->view->isPlatformTypeSupported(platform) != Steinberg::kResultTrue) {
        error = "platform not supported";
        vst->view.reset();
        return false;
    }
    Steinberg::ViewRect rect;
    if (vst->view->getSize(&rect) != Steinberg::kResultOk || rect.getWidth() < 1 || rect.getHeight() < 1) {
        error = "view has no size";
        vst->view.reset();
        return false;
    }
    out.width = rect.getWidth();
    out.height = rect.getHeight();
    if (vst->view->attached(parent, platform) != Steinberg::kResultOk) {
        error = "attach failed";
        vst->view.reset();
        return false;
    }
    vst->editorParent = parent;
    out.open = true;
    return true;
}

void PluginHost::closeEditor(Device& device) {
    auto* vst = dynamic_cast<Vst3Device*>(&device);
    if (!vst || !vst->view)
        return;
    if (vst->editorParent)
        vst->view->removed();
    vst->view.reset();
    vst->editorParent = nullptr;
}

} // namespace ghovlz
