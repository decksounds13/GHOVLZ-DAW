#include "midi/MidiIO.h"

#include "RtMidi.h"

#include <vector>

namespace ghovlz {

struct MidiIO::Impl {
    RtMidiIn* input = nullptr;
};

MidiIO::MidiIO() : impl_(new Impl) {}

MidiIO::~MidiIO() {
    close();
    delete impl_;
}

bool MidiIO::open() {
    close();
    try {
        impl_->input = new RtMidiIn(RtMidi::UNSPECIFIED, "GHOVLZ DAW");
        if (impl_->input->getPortCount() < 1) {
            port_ = "No MIDI input";
            return false;
        }
        port_ = impl_->input->getPortName(0);
        impl_->input->openPort(0);
        impl_->input->ignoreTypes(false, false, false);
        return true;
    } catch (const RtMidiError&) {
        port_ = "MIDI unavailable";
        close();
        return false;
    }
}

void MidiIO::poll() {
    if (!impl_ || !impl_->input)
        return;
    try {
        std::vector<unsigned char> message;
        while (true) {
            message.clear();
            impl_->input->getMessage(&message);
            if (message.empty())
                break;
            if (message.size() >= 3 && (message[0] & 0xF0) == 0x90 && message[2] > 0)
                ++notes_;
        }
    } catch (const RtMidiError&) {
    }
}

void MidiIO::close() {
    if (!impl_ || !impl_->input)
        return;
    try {
        impl_->input->closePort();
    } catch (const RtMidiError&) {
    }
    delete impl_->input;
    impl_->input = nullptr;
}

} // namespace ghovlz
