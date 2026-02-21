# Clive

A Go-based CLI tool and signaling server for WebRTC video and audio streaming using [Pion](https://github.com/pion/webrtc) and [MediaDevices](https://github.com/pion/mediadevices).

## Prerequisites

- [Go](https://golang.org/doc/install) 1.24 or later
- A C compiler (required for `cgo` by some media codecs)

*Note: Depending on your OS, you may need to grant terminal permissions for Camera and Microphone access.*

## Building

The easiest way to build all components (Signaling Server, CLI Client, and Controller) is to use the provided build script:

```bash
./build.sh
```

Alternatively, you can build them manually:

```bash
# Build the signaling server
go build -o signaling-server ./cmd/signaling

# Build the WebRTC CLI client
go build -o clive-cli ./cmd/cli

# Build the Controller
go build -o clive-controller ./cmd/controller
```

## Running Manually

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

## Test Mode / Remote Control (Controller)

If you are deploying `clive` to a remote peer (like a Raspberry Pi or another server) for testing, it is easier to use the included `clive-controller`. This lightweight HTTP server allows you to remotely manage the signaling server, the WebRTC client, and keep the code up to date.

**1. Start the Controller:**
```bash
./build.sh
./clive-controller &
```
*The controller will run in the background on port `9090`.*

**2. API Endpoints:**

You can now use `curl` from any terminal (or remotely) to control the application:

* **Status:** Check if processes are running and the current Git commit.
  ```bash
  curl http://localhost:9090/status
  ```

* **Signaling Server:** Start, stop, or get logs.
  ```bash
  curl -X POST http://localhost:9090/signaling/start
  curl http://localhost:9090/signaling/logs
  curl -X POST http://localhost:9090/signaling/stop
  ```

* **CLI Client:** Start, stop, or get logs. (You can optionally pass a JSON payload to override default parameters).
  ```bash
  # Start the receiver
  curl -X POST -H "Content-Type: application/json" -d '{"room": "my-room", "server": "localhost:8080", "caller": false}' http://localhost:9090/client/start
  
  # Start the caller
  curl -X POST -H "Content-Type: application/json" -d '{"room": "my-room", "server": "localhost:8080", "caller": true}' http://localhost:9090/client/start
  
  # View recent logs
  curl http://localhost:9090/client/logs
  
  # Stop the client
  curl -X POST http://localhost:9090/client/stop
  ```

* **Update Code (Pull):** Automatically pulls the latest changes from the `master` branch via Git, stops running processes, and rebuilds the binaries.
  ```bash
  curl -X POST http://localhost:9090/pull
  ```
