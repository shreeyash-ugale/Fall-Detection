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
ACTIVITY_RMS_THRESHOLD = 5.0  # m/s²

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
    acc_rms = float(np.sqrt(np.mean(acc_dynamic ** 2)))
    if acc_rms < ACTIVITY_RMS_THRESHOLD:
        return {"fall": False, "confidence": 0.0}

    features = np.concatenate([acc_dynamic, gyro], axis=1)
    features_norm = scaler.transform(features)
    tensor = torch.tensor(features_norm.T[np.newaxis], dtype=torch.float32).to(DEVICE)

    with torch.no_grad():
        prob = model(tensor).item()
        
    # Heuristic Penalty for small movements to handle sensitivity.
    # We penalize the NN confidence if the physical metrics don't resemble a big jerk/fall.
    acc_magnitude = np.linalg.norm(acc, axis=1)
    max_acc = float(np.max(acc_magnitude))
    
    gyro_magnitude = np.linalg.norm(gyro, axis=1)
    max_gyro = float(np.max(gyro_magnitude))

    penalty = 1.0
    
    # Penalize low impact acceleration (less than ~3.0g to 4.5g)
    if max_acc < 30.0:
        penalty *= 0.1
    elif max_acc < 45.0:
        penalty *= 0.1 + 0.9 * ((max_acc - 30.0) / 15.0)

    # Penalize low rotation (less than 250 to 450 deg/s)
    if max_gyro < 250.0:
        penalty *= 0.2
    elif max_gyro < 450.0:
        penalty *= 0.2 + 0.8 * ((max_gyro - 250.0) / 200.0)

    prob = prob * penalty

    acc_mean = np.mean(acc, axis=0).round(4).tolist()
    raw_prob = prob / penalty if penalty > 0 else 0.0
    print(
        f"Raw Prob: {raw_prob:.4f} | "
        f"Mod Prob: {prob:.4f} | "
        f"max_acc: {max_acc:.1f} m/s^2 | max_gyro: {max_gyro:.1f} deg/s | "
        f"acc_mean(x,y,z)={acc_mean}"
    )

    return {
        "fall":       prob >= THRESHOLD,
        "confidence": round(prob, 4),
    }

@app.get("/health")
def health():
    return {"status": "ok"}