package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strings"
	"sync"
)

type ManagedProcess struct {
	mu  sync.Mutex
	cmd *exec.Cmd
}

func (m *ManagedProcess) Start(logFile string, name string, args ...string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.cmd != nil {
		return fmt.Errorf("process already running")
	}

	m.cmd = exec.Command(name, args...)

	var f *os.File
	if logFile != "" {
		var err error
		f, err = os.OpenFile(logFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
		if err == nil {
			m.cmd.Stdout = io.MultiWriter(os.Stdout, f)
			m.cmd.Stderr = io.MultiWriter(os.Stderr, f)
		} else {
			log.Printf("Failed to open log file %s: %v", logFile, err)
			m.cmd.Stdout = os.Stdout
			m.cmd.Stderr = os.Stderr
		}
	} else {
		m.cmd.Stdout = os.Stdout
		m.cmd.Stderr = os.Stderr
	}

	if err := m.cmd.Start(); err != nil {
		if f != nil {
			f.Close()
		}
		m.cmd = nil
		return err
	}

	go func(c *exec.Cmd, logF *os.File) {
		c.Wait()
		if logF != nil {
			logF.Close()
		}
		m.mu.Lock()
		if m.cmd == c {
			m.cmd = nil // Reset when it finishes naturally or gets killed
		}
		m.mu.Unlock()
	}(m.cmd, f)

	return nil
}

func (m *ManagedProcess) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.cmd == nil {
		return fmt.Errorf("process not running")
	}

	if err := m.cmd.Process.Kill(); err != nil {
		return err
	}

	// The Wait() goroutine will clear the cmd
	return nil
}

func (m *ManagedProcess) IsRunning() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.cmd != nil
}

var (
	signalingProc ManagedProcess
	clientProc    ManagedProcess
)

func statusHandler(w http.ResponseWriter, r *http.Request) {
	out, err := exec.Command("git", "rev-parse", "HEAD").Output()
	commit := ""
	if err == nil {
		commit = strings.TrimSpace(string(out))
	}

	resp := map[string]interface{}{
		"commit":            commit,
		"signaling_running": signalingProc.IsRunning(),
		"client_running":    clientProc.IsRunning(),
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func startSignalingHandler(w http.ResponseWriter, r *http.Request) {
	if err := signalingProc.Start("signaling.log", "./signaling-server"); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	fmt.Fprintf(w, "Signaling server started\n")
}

func stopSignalingHandler(w http.ResponseWriter, r *http.Request) {
	if err := signalingProc.Stop(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	fmt.Fprintf(w, "Signaling server stopped\n")
}

func signalingLogsHandler(w http.ResponseWriter, r *http.Request) {
	out, err := exec.Command("tail", "-n", "100", "signaling.log").CombinedOutput()
	if err != nil {
		http.Error(w, fmt.Sprintf("No logs available yet\n"), http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "text/plain")
	w.Write(out)
}

type ClientConfig struct {
	Room   string `json:"room"`
	Server string `json:"server"`
	Caller bool   `json:"caller"`
}

func startClientHandler(w http.ResponseWriter, r *http.Request) {
	config := ClientConfig{Room: "default-room", Server: "localhost:8080", Caller: false}

	if r.Method == http.MethodPost && r.ContentLength > 0 {
		if err := json.NewDecoder(r.Body).Decode(&config); err != nil {
			http.Error(w, fmt.Sprintf("invalid json: %v", err), http.StatusBadRequest)
			return
		}
	}

	args := []string{
		"-room", config.Room,
		"-server", config.Server,
	}
	if config.Caller {
		args = append(args, "-caller")
	}

	if err := clientProc.Start("client.log", "./clive-cli", args...); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	fmt.Fprintf(w, "Client started\n")
}

func stopClientHandler(w http.ResponseWriter, r *http.Request) {
	if err := clientProc.Stop(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	fmt.Fprintf(w, "Client stopped\n")
}

func clientLogsHandler(w http.ResponseWriter, r *http.Request) {
	out, err := exec.Command("tail", "-n", "100", "client.log").CombinedOutput()
	if err != nil {
		http.Error(w, fmt.Sprintf("No logs available yet\n"), http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "text/plain")
	w.Write(out)
}

func pullHandler(w http.ResponseWriter, r *http.Request) {
	// git pull origin master
	out, err := exec.Command("git", "pull", "origin", "master").CombinedOutput()
	if err != nil {
		http.Error(w, fmt.Sprintf("git pull failed: %v\nOutput: %s", err, string(out)), http.StatusInternalServerError)
		return
	}

	// Stop existing processes before building
	if signalingProc.IsRunning() {
		signalingProc.Stop()
	}
	if clientProc.IsRunning() {
		clientProc.Stop()
	}

	// Wait briefly for processes to fully exit
	// (simplistic wait, relying on kill speed and Wait goroutine)

	// Build using the script
	buildOut, err := exec.Command("./build.sh").CombinedOutput()
	if err != nil {
		http.Error(w, fmt.Sprintf("build failed: %v\nOutput: %s", err, string(buildOut)), http.StatusInternalServerError)
		return
	}

	fmt.Fprintf(w, "Pull and build successful\nOutput:\n%s\n", string(buildOut))
}

func main() {
	http.HandleFunc("/status", statusHandler)
	http.HandleFunc("/signaling/start", startSignalingHandler)
	http.HandleFunc("/signaling/stop", stopSignalingHandler)
	http.HandleFunc("/signaling/logs", signalingLogsHandler)
	http.HandleFunc("/client/start", startClientHandler)
	http.HandleFunc("/client/stop", stopClientHandler)
	http.HandleFunc("/client/logs", clientLogsHandler)
	http.HandleFunc("/pull", pullHandler)

	port := "9090"
	log.Printf("Control server listening on :%s\n", port)
	log.Printf("Endpoints:\n")
	log.Printf("  GET  /status\n")
	log.Printf("  POST /signaling/start\n")
	log.Printf("  POST /signaling/stop\n")
	log.Printf("  GET  /signaling/logs\n")
	log.Printf("  POST /client/start\n")
	log.Printf("  POST /client/stop\n")
	log.Printf("  GET  /client/logs\n")
	log.Printf("  POST /pull\n")

	if err := http.ListenAndServe(":"+port, nil); err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}
