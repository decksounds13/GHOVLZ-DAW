#pragma once

#include <string>

namespace ghovlz {

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
