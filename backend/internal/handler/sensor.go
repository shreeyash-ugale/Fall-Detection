package handler

import (
	"bytes"
	"encoding/json"
	"net/http"
	"os"

	"github.com/ABHINAVGARG05/embedded-project/internal/model"
)

func SensorHandler(w http.ResponseWriter, r *http.Request) {

	defer r.Body.Close()

	var batch model.SensorBatch

	err := json.NewDecoder(r.Body).Decode(&batch)
	if err != nil {
		http.Error(w, "Invalid JSON", http.StatusBadRequest)
		return
	}

	jsonData, err := json.Marshal(struct {
		Acc  [][]float64 `json:"acc"`
		Gyro [][]float64 `json:"gyro"`
	}{
		Acc:  batch.Acc,
		Gyro: batch.Gyro,
	})
	if err != nil {
		http.Error(w, "JSON Marshalling Error", http.StatusInternalServerError)
		return
	}

	mlURL := os.Getenv("ML_URL")
	if mlURL == "" {
		mlURL = "http://localhost:8000"
	}

	resp, err := http.Post(
		mlURL+"/predict",
		"application/json",
		bytes.NewBuffer(jsonData),
	)

	if err != nil {
		http.Error(w, "ML Service Error", http.StatusInternalServerError)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		http.Error(w, "ML Service Failed", http.StatusInternalServerError)
		return
	}

	var result model.MLResponse
	err = json.NewDecoder(resp.Body).Decode(&result)
	if err != nil {
		http.Error(w, "Invalid ML Response", http.StatusInternalServerError)
		return
	}

	if result.Fall {
		_ = sendFallNotification(batch.DeviceID)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}