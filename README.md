# Fall Detection System

End-to-end fall detection pipeline with:
- ESP32 + MPU6050 sensor sampling
- Go backend API gateway
- Python FastAPI ML inference service (PyTorch CNN)
- Node.js real-time Docker log viewer

The system accepts batched accelerometer/gyroscope windows, predicts fall probability, and can send push notifications when a fall is detected.

## Architecture

- Device sends sensor batch to backend: POST /api/sensor
- Backend forwards acc/gyro data to ML service: POST /predict
- ML service returns { fall, confidence }
- Backend returns prediction to caller
- If fall is true, backend attempts Expo push notification for the registered device token
- Log service streams backend and ML container logs over WebSocket

## Repository Layout

```text
.
|-- docker-compose.yml
|-- ep32.ino
|-- request_tests.py
|-- backend/
|   |-- cmd/server/main.go
|   |-- internal/handler/push.go
|   |-- internal/handler/sensor.go
|   |-- internal/model/sensor.go
|   |-- Dockerfile
|   `-- go.mod
|-- ml-service/
|   |-- app.py
|   |-- model.py
|   |-- preprocessing.py
|   |-- train.py
|   |-- evaluate.py
|   |-- requirements.txt
|   `-- Dockerfile
`-- log-service/
    |-- server.js
    |-- package.json
    |-- pnpm-lock.yaml
    `-- Dockerfile
```

## Services and Ports

- Backend: http://localhost:8080
- ML service: http://localhost:8000
- Log service UI: http://localhost:3000/logs

Container names used by the log service:
- fwds-backend
- fwds-ml

## Prerequisites

### Docker deployment

- Docker Desktop (or Docker Engine + Compose plugin)

### Local development

- Go 1.25+
- Python 3.11+
- Node.js 20+
- pnpm 10+

## Quick Start (Docker Compose)

From the project root:

```bash
docker compose up --build
```

Run detached:

```bash
docker compose up --build -d
```

Stop:

```bash
docker compose down
```

Notes:
- Backend gets ML endpoint from ML_URL, set in compose as http://ml-service:8000.
- Log service mounts /var/run/docker.sock. If you are on Windows/macOS, you may need to adjust the volume mapping for your Docker setup.

## API Reference

### Backend

Base URL: http://localhost:8080

#### POST /api/sensor

Accepts one sensor window and returns fall prediction.

Request body:

```json
{
  "device_id": "ESP32-NODE-1",
  "acc": [[0.1, 0.2, 9.8]],
  "gyro": [[0.0, 0.1, 0.0]]
}
```

- acc: list of [x, y, z] accelerometer samples (m/s^2)
- gyro: list of [x, y, z] gyroscope samples (deg/s)
- device_id: logical device identifier for push token routing

Response body:

```json
{
  "fall": false,
  "confidence": 0.1432
}
```

#### POST /api/push/register

Registers Expo push token for a device.

Request body:

```json
{
  "device_id": "ESP32-NODE-1",
  "expo_token": "ExponentPushToken[xxxxxxxxxxxxxxxxxxxxxx]"
}
```

Response body:

```json
{
  "status": "ok"
}
```

### ML Service

Base URL: http://localhost:8000

#### POST /predict

Input:

```json
{
  "acc": [[0.1, 0.2, 9.8], [0.1, 0.2, 9.7]],
  "gyro": [[0.0, 0.1, 0.0], [0.0, 0.1, 0.1]]
}
```

Behavior:
- Pads/truncates to WINDOW_SIZE = 200
- Removes accelerometer DC component (gravity bias)
- Skips inference when dynamic acceleration RMS is below threshold
- Runs CNN + sigmoid and returns probability

Output:

```json
{
  "fall": true,
  "confidence": 0.9876
}
```

#### GET /health

```json
{
  "status": "ok"
}
```

## Local Development

### 1) ML service

```bash
cd ml-service
python -m venv .venv
# activate venv
pip install -r requirements.txt
uvicorn app:app --host 0.0.0.0 --port 8000 --workers 1
```

Required runtime artifacts in ml-service:
- model.pth
- scaler.pkl

### 2) Backend service

```bash
cd backend
set ML_URL=http://localhost:8000
go run ./cmd/server
```

Backend starts on port 8080.

### 3) Log service

```bash
cd log-service
pnpm install
node server.js
```

Open http://localhost:3000/logs.

## Training and Evaluation

Dataset expected by training code:
- train.py uses DATASET_DIR = dataset/SisFall_dataset
- evaluate.py uses DATASET_DIR = dataset

Run training:

```bash
cd ml-service
python train.py
```

Outputs:
- model.pth
- scaler.pkl

Run evaluation:

```bash
cd ml-service
python evaluate.py
```

Prints classification report, confusion matrix, ROC-AUC, sensitivity, specificity.

## ESP32 Firmware Notes

File: ep32.ino

Current firmware behavior:
- Reads MPU6050 at 100 Hz
- Builds batches of 200 samples
- Sends JSON to backend /api/sensor
- Blinks LED on high-confidence fall

Before flashing, update in firmware:
- Wi-Fi SSID/password
- serverUrl
- DEVICE_ID

## Test Script

request_tests.py performs sequential HTTP checks (minimum 30 requests), measures latency and pass/fail stats, and sends synthetic sensor windows.

Run:

```bash
python request_tests.py
```

Update TARGET_URL in the script if needed.

## Environment Variables

Backend:
- ML_URL (default: http://localhost:8000)
- EXPO_PUSH_URL (default: https://exp.host/--/api/v2/push/send)

## Troubleshooting

- 500 from backend /api/sensor:
  - Confirm ML service is up on port 8000
  - Check backend ML_URL value
- ML service fails at startup:
  - Ensure model.pth and scaler.pkl exist
  - Ensure package versions from requirements.txt are installed
- No logs in log viewer:
  - Verify container names are fwds-backend and fwds-ml
  - Verify docker socket mount is valid on your OS
- No push notifications:
  - Register token first via /api/push/register
  - Confirm device_id in /api/sensor matches registered token

## Current Limitations

- Push token storage is in-memory only (lost on backend restart)
- No auth/rate limiting on public endpoints
- Minimal request validation in backend (ML service handles most shape validation)
- docker-compose.yml has Linux-specific Docker socket path for log service

## Next Improvements

- Persist push tokens (Redis/Postgres)
- Add structured logging and request IDs
- Add backend input validation for sample dimensions and ranges
- Add CI tests for backend and ML inference contract
- Add secure configuration for production (TLS, auth, secrets)
