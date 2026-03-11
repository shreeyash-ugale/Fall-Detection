package handler

import (
	"bytes"
	"encoding/json"
	"net/http"
	"os"
	"sync"
)

type pushRegistration struct {
	DeviceID  string `json:"device_id"`
	ExpoToken string `json:"expo_token"`
}

type expoPushMessage struct {
	To    string `json:"to"`
	Title string `json:"title"`
	Body  string `json:"body"`
	Sound string `json:"sound,omitempty"`
}

var (
	pushMu      sync.RWMutex
	pushTokens  = map[string]string{}
)

func RegisterPushHandler(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()

	var payload pushRegistration
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		http.Error(w, "Invalid JSON", http.StatusBadRequest)
		return
	}

	if payload.DeviceID == "" || payload.ExpoToken == "" {
		http.Error(w, "device_id and expo_token are required", http.StatusBadRequest)
		return
	}

	pushMu.Lock()
	pushTokens[payload.DeviceID] = payload.ExpoToken
	pushMu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
}

func sendFallNotification(deviceID string) error {
	pushMu.RLock()
	token, ok := pushTokens[deviceID]
	pushMu.RUnlock()
	if !ok || token == "" {
		return nil
	}

	message := expoPushMessage{
		To:    token,
		Title: "Fall detected",
		Body:  "A fall was detected by the device.",
		Sound: "default",
	}

	payload, err := json.Marshal(message)
	if err != nil {
		return err
	}

	pushURL := os.Getenv("EXPO_PUSH_URL")
	if pushURL == "" {
		pushURL = "https://exp.host/--/api/v2/push/send"
	}

	resp, err := http.Post(pushURL, "application/json", bytes.NewBuffer(payload))
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	return nil
}
