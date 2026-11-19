# Audio device

Settings live in the user profile, not in the project.

- Windows: `%USERPROFILE%\AppData\Roaming\GHOVLZ\settings.json`
- Linux: `~/.config/ghovlz/settings.json`

The file stores the backend (`wasapi`, `wasapi-exclusive`, `asio`, `alsa`, `fake`), device name, sample rate, buffer size, exclusive flag, MIDI port list, plugin folder, renderer (`auto`, `d3d11`, `warp`, `cpu`) and theme scale.

`openOrFallback` tries those settings. When the open fails it clears the device name, turns exclusive off, and maps `asio` onto `wasapi`, then tries once more. The UI shows the message from that second attempt.

Backends:

| Backend | Where it runs |
| --- | --- |
| WASAPI shared | Windows |
| WASAPI exclusive | Windows |
| ASIO | Windows, SDK fetched at configure time |
| ALSA | Linux |
| fake | tests and headless runs |

A device that stops sets an atomic reopen flag. The UI thread polls it and calls `recover()` with a backoff of 50 ms, doubling up to 2 s. Transport and armed tracks are left in place.

ASIO sample conversion covers Int16, Int24 LSB, Int32 LSB, Float32 and Float64. Message selectors Supported, EngineVersion, ResetRequest, BufferSizeChange, ResyncRequest and LatenciesChanged are handled. Resets are applied off the callback. The sample rate passed to `prepare` is the rate `ASIOGetSampleRate` reports. Latency is shown in the UI when the driver returns it. `ASIOControlPanel` is available on Windows. MMCSS is raised on the callback thread and reverted on that same thread.

Still unverified on hardware: a real ASIO driver at 64 samples, unplugging a USB interface, and a desktop D3D11 device.
