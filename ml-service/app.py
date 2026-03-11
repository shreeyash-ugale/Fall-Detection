import numpy as np
import torch
import joblib
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List

from model import FallCNN
from preprocessing import CHANNELS, WINDOW_SIZE

MODEL_PATH  = "model.pth"
SCALER_PATH = "scaler.pkl"
DEVICE      = torch.device("cpu")
THRESHOLD   = 0.5
# Minimum RMS of dynamic (DC-removed) acceleration to run the model.
# A real fall always has a significant impact spike. A still or near-still
# sensor will always be below this, so we skip inference and return no-fall
# rather than relying on the model which was never trained on static inputs.
ACTIVITY_RMS_THRESHOLD = 0.5  # m/s²

model = FallCNN(in_channels=CHANNELS, window_size=WINDOW_SIZE).to(DEVICE)
model.load_state_dict(torch.load(MODEL_PATH, map_location=DEVICE))
model.eval()

scaler = joblib.load(SCALER_PATH)

app = FastAPI(title="Fall Detection API", version="1.0.0")

class SensorWindow(BaseModel):
    acc: List[List[float]]
    gyro: List[List[float]]


def _to_window(signal: np.ndarray, name: str) -> np.ndarray:
    if signal.ndim != 2 or signal.shape[1] != 3:
        raise HTTPException(status_code=422, detail=f"{name} must be a 2D array with 3 columns (x, y, z)")
    if signal.shape[0] == 0:
        raise HTTPException(status_code=422, detail=f"{name} must contain at least 1 time step")

    # Keep most recent samples when there are extra points.
    if signal.shape[0] > WINDOW_SIZE:
        return signal[-WINDOW_SIZE:]

    # Right-pad short sequences with the last sample to preserve continuity.
    if signal.shape[0] < WINDOW_SIZE:
        pad_rows = WINDOW_SIZE - signal.shape[0]
        pad = np.repeat(signal[-1:, :], pad_rows, axis=0)
        return np.concatenate([signal, pad], axis=0)

    return signal

@app.post("/predict")
def predict(data: SensorWindow):
    acc = _to_window(np.array(data.acc, dtype=np.float32), "acc")
    gyro = _to_window(np.array(data.gyro, dtype=np.float32), "gyro")

    # The model was trained on SisFall data where the accelerometer has no
    # static gravity component (near-zero DC). Live sensor data includes
    # gravity (~9.8 m/s² on whichever axis is vertical). Remove the DC bias
    # per-channel so the input distribution matches training.
    acc_dynamic = acc - acc.mean(axis=0, keepdims=True)

    # Pre-filter: skip inference when the sensor is essentially still.
    # A fall always produces a significant impact spike; static noise never
    # exceeds this RMS threshold, so there is no need to run the model.
    acc_rms = float(np.sqrt(np.mean(acc_dynamic ** 2)))
    acc_mean = np.mean(acc, axis=0).round(4).tolist()
    gyro_mean = np.mean(gyro, axis=0).round(4).tolist()
    if acc_rms < ACTIVITY_RMS_THRESHOLD:
        print(
            f"Prediction probability: 0.000000 (still, rms={acc_rms:.4f}) | "
            f"acc_mean(x,y,z)={acc_mean} | "
            f"gyro_mean(x,y,z)={gyro_mean}"
        )
        return {"fall": False, "confidence": 0.0}

    features = np.concatenate([acc_dynamic, gyro], axis=1)
    features_norm = scaler.transform(features)
    tensor = torch.tensor(features_norm.T[np.newaxis], dtype=torch.float32).to(DEVICE)

    with torch.no_grad():
        prob = model(tensor).item()

    acc_mean = np.mean(acc, axis=0).round(4).tolist()
    gyro_mean = np.mean(gyro, axis=0).round(4).tolist()
    print(
        f"Prediction probability: {prob:.6f} | "
        f"acc_mean(x,y,z)={acc_mean} | "
        f"gyro_mean(x,y,z)={gyro_mean}"
    )

    return {
        "fall":       prob >= THRESHOLD,
        "confidence": round(prob, 4),
    }


@app.get("/health")
def health():
    return {"status": "ok"}