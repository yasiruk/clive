package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true // Allow all origins for the prototype
	},
}

// Room manages a set of connected clients
type Room struct {
	Clients map[*websocket.Conn]bool
	mu      sync.Mutex
}

// NewRoom creates a new Room instance
func NewRoom() *Room {
	return &Room{
		Clients: make(map[*websocket.Conn]bool),
	}
}

// Global rooms map: roomName -> *Room
var rooms = make(map[string]*Room)
var roomsMu sync.Mutex

// Message represents the signaling JSON structure
type Message struct {
	Type string          `json:"type"` // e.g., "offer", "answer", "candidate", "join"
	Data json.RawMessage `json:"data"` // Contains the SDP or ICE candidate
}

func handleWebSocket(w http.ResponseWriter, r *http.Request) {
	roomName := r.URL.Query().Get("room")
	if roomName == "" {
		roomName = "default" // Default room if none provided
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("Upgrade error:", err)
		return
	}

	// Add client to room
	roomsMu.Lock()
	room, exists := rooms[roomName]
	if !exists {
		room = NewRoom()
		rooms[roomName] = room
	}
	roomsMu.Unlock()

	room.mu.Lock()
	room.Clients[conn] = true
	room.mu.Unlock()

	log.Printf("Client connected to room: %s. Total clients: %d\n", roomName, len(room.Clients))

	defer func() {
		room.mu.Lock()
		delete(room.Clients, conn)
		log.Printf("Client disconnected from room: %s. Total clients: %d\n", roomName, len(room.Clients))
		room.mu.Unlock()
		conn.Close()
	}()

	for {
		messageType, p, err := conn.ReadMessage()
		if err != nil {
			log.Println("Read error:", err)
			break
		}

		if messageType != websocket.TextMessage {
			continue
		}

		// Broadcast message to all OTHER clients in the room
		room.mu.Lock()
		for client := range room.Clients {
			if client != conn {
				err := client.WriteMessage(websocket.TextMessage, p)
				if err != nil {
					log.Printf("Write error to a client: %v\n", err)
					client.Close()
					delete(room.Clients, client)
				}
			}
		}
		room.mu.Unlock()
	}
}

func main() {
	addr := flag.String("addr", ":8080", "Host:port to run signaling server on (e.g., :8080 or localhost:8080)")
	flag.Parse()

	http.HandleFunc("/ws", handleWebSocket)

	displayAddr := *addr
	if displayAddr[0] == ':' {
		displayAddr = "localhost" + displayAddr
	}
	fmt.Printf("Signaling Server starting on ws://%s/ws\n", displayAddr)
	fmt.Println("Connect with query parameter: /ws?room=myroom")

	err := http.ListenAndServe(*addr, nil)
	if err != nil {
		log.Fatal("ListenAndServe:", err)
	}
}
