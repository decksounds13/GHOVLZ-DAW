#pragma once

#include <cstdint>
#include <string>

namespace ghovlz {

// RtMidi stamps are seconds. 1 ms at 48 kHz is 48 samples.
inline int sampleOffsetFromSeconds(double seconds, double sampleRate) {
    const double shifted = seconds * sampleRate;
    const double rounded = shifted >= 0 ? shifted + 0.5 : shifted - 0.5;
    return static_cast<int>(rounded);
}

class MidiIO {
public:
    MidiIO();
    ~MidiIO();
    MidiIO(const MidiIO&) = delete;
    MidiIO& operator=(const MidiIO&) = delete;

    void setEngine(class Engine* engine) { engine_ = engine; }
    bool open();
    void close();
    void poll();
    const std::string& portName() const { return port_; }
    int pendingNotes() const { return notes_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    class Engine* engine_ = nullptr;
    std::string port_;
    int notes_ = 0;
};

} // namespace ghovlz
