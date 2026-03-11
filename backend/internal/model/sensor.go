package model

type SensorBatch struct {
	DeviceID string      `json:"device_id"`
	Acc      [][]float64 `json:"acc"`
	Gyro     [][]float64 `json:"gyro"`
}

type MLResponse struct {
	Fall       bool    `json:"fall"`
	Confidence float64 `json:"confidence"`
}