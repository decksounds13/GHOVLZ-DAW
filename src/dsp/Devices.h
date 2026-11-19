#pragma once

#include "core/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace ghovlz {

std::unique_ptr<Device> createDevice(const std::string& type);
std::vector<std::string> nativeDeviceTypes();
std::string deviceDisplayName(const std::string& type);

// EQ response for the UI thread. Returns dB, one value per frequency.
std::vector<float> eqMagnitudeDb(const Device& eq, double sampleRate, const std::vector<float>& freqs);

AnalyzerSnapshot readAnalyzer(const Device& device);

} // namespace ghovlz
