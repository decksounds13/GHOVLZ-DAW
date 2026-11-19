#include "midi/MidiIO.h"

#include "RtMidi.h"
#include "engine/Engine.h"

#include <vector>

namespace ghovlz {

struct MidiIO::Impl {
    std::vector<RtMidiIn*> inputs;
};

MidiIO::MidiIO() : impl_(new Impl) {}

MidiIO::~MidiIO() {
    close();
    delete impl_;
}

bool MidiIO::open() {
    close();
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "GHOVLZ DAW probe");
        const unsigned count = probe.getPortCount();
        if (count < 1) {
            port_ = "No MIDI input";
            return false;
        }
        port_.clear();
        for (unsigned i = 0; i < count; ++i) {
            auto* input = new RtMidiIn(RtMidi::UNSPECIFIED, "GHOVLZ DAW");
            const std::string name = probe.getPortName(i);
            if (!port_.empty())
                port_ += ", ";
            port_ += name;
            input->openPort(i);
            input->ignoreTypes(true, true, true);
            input->setCallback([](double stamp, std::vector<unsigned char>* message, void* user) {
                auto* self = static_cast<MidiIO*>(user);
                if (!self || !self->engine_ || !message || message->empty())
                    return;
                const auto& bytes = *message;
                if (bytes[0] == 0xF0 || bytes[0] == 0xF7 || bytes.size() > 3)
                    return;
                const int offset = sampleOffsetFromSeconds(stamp, 48000.0);
                const uint8_t d1 = bytes.size() > 1 ? bytes[1] : 0;
                const uint8_t d2 = bytes.size() > 2 ? bytes[2] : 0;
                self->engine_->pushMidi(offset, bytes[0], d1, d2, static_cast<int>(bytes.size()));
            }, this);
            impl_->inputs.push_back(input);
        }
        return true;
    } catch (const RtMidiError&) {
        port_ = "MIDI unavailable";
        close();
        return false;
    }
}

void MidiIO::poll() {
    if (!impl_)
        return;
    try {
        for (auto* input : impl_->inputs) {
            std::vector<unsigned char> message;
            while (input) {
                message.clear();
                input->getMessage(&message);
                if (message.empty())
                    break;
                if (message.size() >= 3 && (message[0] & 0xF0) == 0x90 && message[2] > 0)
                    ++notes_;
            }
        }
    } catch (const RtMidiError&) {
    }
}

void MidiIO::close() {
    if (!impl_)
        return;
    for (auto* input : impl_->inputs) {
        if (!input)
            continue;
        try {
            input->closePort();
        } catch (const RtMidiError&) {
        }
        delete input;
    }
    impl_->inputs.clear();
}

} // namespace ghovlz
