# Clive

A Go-based CLI tool and signaling server for WebRTC video and audio streaming using [Pion](https://github.com/pion/webrtc) and [MediaDevices](https://github.com/pion/mediadevices).

## Prerequisites

- [Go](https://golang.org/doc/install) 1.24 or later
- A C compiler (required for `cgo` by some media codecs)

*Note: Depending on your OS, you may need to grant terminal permissions for Camera and Microphone access.*

## Building

You can build the CLI client and the signaling server separately:

```bash
# Build the signaling server
go build -o signaling-server ./cmd/signaling

# Build the WebRTC CLI client
go build -o clive-cli ./cmd/cli
```

## Running

To establish a peer-to-peer connection, you need to run the signaling server and at least two CLI clients (one receiver and one caller).

**1. Start the signaling server:**
```bash
./signaling-server -addr :8080
```

**2. Start the Receiver:**
Open a new terminal and run the CLI client in receiver mode (waits for an offer):
```bash
./clive-cli -room my-room -server localhost:8080 -caller=false
```

**3. Start the Caller:**
Open another terminal and run the CLI client in caller mode (initiates the offer):
```bash
./clive-cli -room my-room -server localhost:8080 -caller=true
```

Once both clients are running, they will connect through the signaling server, negotiate the WebRTC connection, and begin sharing audio/video tracks.
