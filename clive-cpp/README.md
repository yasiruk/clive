# Clive C++ Client

This is a C++ implementation of the Clive WebRTC client using GStreamer.

## Prerequisites

- CMake (3.10+)
- GStreamer 1.0 (with `gstreamer-webrtc`, `gstreamer-sdp`, `gstreamer-plugins-base`, `gstreamer-plugins-good`, `gstreamer-plugins-bad`)
- `json-glib`
- `libsoup` (3.0)
- A C++17 compliant compiler

## Building

```bash
./build.sh
```

## Running

The executable is located in `build/clive-cpp`.

```bash
# Run as receiver (default)
./build/clive-cpp -r my-room -s localhost:8080

# Run as caller
./build/clive-cpp -r my-room -s localhost:8080 -c
```

This implementation connects to the existing Go signaling server.
