package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"sync"
	"syscall"

	"github.com/gorilla/websocket"
	"github.com/pion/webrtc/v4"

	"github.com/pion/mediadevices"
	"github.com/pion/mediadevices/pkg/codec/opus"
	"github.com/pion/mediadevices/pkg/codec/vpx"
	_ "github.com/pion/mediadevices/pkg/driver/camera"
	_ "github.com/pion/mediadevices/pkg/driver/microphone"
	"github.com/pion/mediadevices/pkg/prop"
)

// Message matches the signaling server JSON structure
type Message struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data"`
}

func main() {
	roomName := flag.String("room", "default-room", "The WebRTC room to join")
	signalingURL := flag.String("signaling", "ws://localhost:8080/ws", "The signaling server URL")
	isCaller := flag.Bool("caller", false, "Whether this client is the caller (initiates the offer)")
	flag.Parse()

	fmt.Printf("Starting WebRTC CLI Client...\n")
	fmt.Printf("Room: %s\n", *roomName)
	fmt.Printf("Signaling Server: %s\n", *signalingURL)
	fmt.Printf("Caller Mode: %v\n", *isCaller)

	// 1. Initialize WebRTC peer connection
	config := webrtc.Configuration{
		ICEServers: []webrtc.ICEServer{
			{URLs: []string{"stun:stun.l.google.com:19302"}},
		},
	}
	peerConnection, err := webrtc.NewPeerConnection(config)
	if err != nil {
		log.Fatalf("Failed to create PeerConnection: %v\n", err)
	}
	defer peerConnection.Close()

	// Handle ICE Connection State changes
	peerConnection.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		fmt.Printf("ICE Connection State changed: %s\n", state.String())
	})

	// 2. Setup WebSocket signaling
	wsURL := fmt.Sprintf("%s?room=%s", *signalingURL, *roomName)
	conn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		log.Fatalf("Failed to connect to signaling server: %v", err)
	}
	defer conn.Close()
	fmt.Println("Connected to Signaling Server.")

	var writeMu sync.Mutex
	writeJSON := func(msg Message) error {
		writeMu.Lock()
		defer writeMu.Unlock()
		return conn.WriteJSON(msg)
	}

	// Send ICE candidates to the signaling server
	peerConnection.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		if candidate == nil {
			return
		}
		data, err := json.Marshal(candidate.ToJSON())
		if err != nil {
			log.Println("Failed to marshal candidate:", err)
			return
		}
		msg := Message{Type: "candidate", Data: data}
		if err := writeJSON(msg); err != nil {
			log.Println("Failed to send candidate:", err)
		}
	})

	// 3. Initialize mediadevices to capture local audio/video feeds
	fmt.Println("Requesting camera and microphone access...")
	vpxParams, _ := vpx.NewVP8Params()
	opusParams, _ := opus.NewParams()

	codecSelector := mediadevices.NewCodecSelector(
		mediadevices.WithVideoEncoders(&vpxParams),
		mediadevices.WithAudioEncoders(&opusParams),
	)

	mediaStream, err := mediadevices.GetUserMedia(mediadevices.MediaStreamConstraints{
		Video: func(c *mediadevices.MediaTrackConstraints) {
			c.Width = prop.Int(640)
			c.Height = prop.Int(480)
			c.FrameRate = prop.Float(30)
		},
		Audio: func(c *mediadevices.MediaTrackConstraints) {},
		Codec: codecSelector,
	})
	if err != nil {
		log.Fatalf("Failed to get user media: %v\n", err)
	}

	// 4. Add local tracks to PeerConnection
	for _, track := range mediaStream.GetTracks() {
		track.OnEnded(func(err error) {
			fmt.Printf("Track ended: %v\n", err)
		})

		_, err = peerConnection.AddTransceiverFromTrack(track,
			webrtc.RTPTransceiverInit{
				Direction: webrtc.RTPTransceiverDirectionSendrecv,
			},
		)
		if err != nil {
			log.Fatalf("Failed to add track: %v\n", err)
		}
		fmt.Printf("Added local track: %s\n", track.Kind().String())
	}

	// 5. Handle remote tracks
	peerConnection.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
		fmt.Printf("Received remote track! ID: %s, Kind: %s\n", track.ID(), track.Kind().String())
		// Basic track handling to keep the connection alive
		go func() {
			for {
				_, _, readErr := track.ReadRTP()
				if readErr != nil {
					fmt.Printf("Failed to read from remote track: %v\n", readErr)
					return
				}
			}
		}()
	})

	// 6. Signaling Loop
	go func() {
		for {
			var msg Message
			if err := conn.ReadJSON(&msg); err != nil {
				log.Println("WebSocket read error:", err)
				break
			}

			switch msg.Type {
			case "offer":
				fmt.Println("Received offer, setting remote description")
				var offer webrtc.SessionDescription
				if err := json.Unmarshal(msg.Data, &offer); err != nil {
					log.Println("Failed to parse offer:", err)
					continue
				}

				if err := peerConnection.SetRemoteDescription(offer); err != nil {
					log.Println("Failed to set remote description:", err)
					continue
				}

				fmt.Println("Creating answer...")
				answer, err := peerConnection.CreateAnswer(nil)
				if err != nil {
					log.Println("Failed to create answer:", err)
					continue
				}
				if err := peerConnection.SetLocalDescription(answer); err != nil {
					log.Println("Failed to set local description:", err)
					continue
				}

				ansData, _ := json.Marshal(answer)
				writeJSON(Message{Type: "answer", Data: ansData})
				fmt.Println("Answer sent.")

			case "answer":
				fmt.Println("Received answer, setting remote description")
				var answer webrtc.SessionDescription
				if err := json.Unmarshal(msg.Data, &answer); err != nil {
					log.Println("Failed to parse answer:", err)
					continue
				}

				if err := peerConnection.SetRemoteDescription(answer); err != nil {
					log.Println("Failed to set remote description:", err)
					continue
				}

			case "candidate":
				var candidate webrtc.ICECandidateInit
				if err := json.Unmarshal(msg.Data, &candidate); err != nil {
					log.Println("Failed to parse candidate:", err)
					continue
				}

				if err := peerConnection.AddICECandidate(candidate); err != nil {
					log.Println("Failed to add ICE candidate:", err)
				}
			}
		}
	}()

	// If we are the caller, send an offer immediately after connecting.
	if *isCaller {
		fmt.Println("Initiating call (creating offer)...")
		offer, err := peerConnection.CreateOffer(nil)
		if err != nil {
			log.Fatalf("Failed to create offer: %v\n", err)
		}
		if err := peerConnection.SetLocalDescription(offer); err != nil {
			log.Fatalf("Failed to set local description: %v\n", err)
		}
		offerData, _ := json.Marshal(offer)
		writeJSON(Message{Type: "offer", Data: offerData})
	}

	fmt.Println("WebRTC Client is running. Press Ctrl+C to exit.")

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	fmt.Println("Shutting down...")
}
