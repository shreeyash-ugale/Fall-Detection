import os
import numpy as np
from pathlib import Path
from sklearn.preprocessing import StandardScaler
import joblib

# ── Constants ──────────────────────────────────────────────────────────────────

WINDOW_SIZE   = 200
STEP_SIZE     = 100
CHANNELS      = 6
SAMPLING_RATE = 200

ACC_SCALE  = (8 * 2 / 65536) * 9.8
GYRO_SCALE = (2000 * 2 / 65536)

FALL_CODES = {f"F{i:02d}" for i in range(1, 16)}

def parse_sisfall_file(filepath: str) -> np.ndarray:
    """
    Parse a single SisFall .txt file into a float32 array.
    """

    data = []

    # Fix for Colab encoding issue
    with open(filepath, "r", encoding="latin-1", errors="ignore") as f:
        for line in f:
            line = line.strip()

            if not line:
                continue

            parts = line.rstrip(";").split(",")

            if len(parts) < 6:
                continue

            try:
                values = [float(p) for p in parts[:6]]
            except ValueError:
                continue

            data.append(values)

    if not data:
        return np.empty((0, CHANNELS), dtype=np.float32)

    arr = np.array(data, dtype=np.float32)

    arr[:, :3] *= ACC_SCALE
    arr[:, 3:] *= GYRO_SCALE

    return arr[:, :CHANNELS]


def sliding_windows(signal: np.ndarray, window: int, step: int):
    """
    Slice signal into overlapping windows
    """

    windows = []
    start = 0

    while start + window <= len(signal):
        windows.append(signal[start:start + window])
        start += step

    return np.array(windows, dtype=np.float32)


def load_sisfall(dataset_dir: str):

    dataset_dir = Path(dataset_dir)

    X_list = []
    y_list = []

    all_files = sorted(dataset_dir.rglob("*.txt"))

    if not all_files:
        raise FileNotFoundError(
            f"No .txt files found in '{dataset_dir}'. "
            "Please download SisFall dataset."
        )

    print(f"Found {len(all_files)} raw files. Processing...")

    for filepath in all_files:

        filename = filepath.stem
        activity_code = filename.split("_")[0]

        label = 1 if activity_code in FALL_CODES else 0

        signal = parse_sisfall_file(str(filepath))

        if len(signal) < WINDOW_SIZE:
            continue

        windows = sliding_windows(signal, WINDOW_SIZE, STEP_SIZE)

        if len(windows) == 0:
            continue

        X_list.append(windows)
        y_list.append(np.full(len(windows), label, dtype=np.int64))

    if not X_list:
        raise ValueError("No valid windows extracted")

    X = np.concatenate(X_list, axis=0)
    y = np.concatenate(y_list, axis=0)

    X = np.transpose(X, (0, 2, 1))

    print(f"Total windows : {len(X)}")
    print(f"Fall windows  : {y.sum()}")
    print(f"ADL windows   : {(y == 0).sum()}")
    print(f"X shape       : {X.shape}")

    return X, y


def normalize(X_train: np.ndarray, X_test: np.ndarray, scaler_path="scaler.pkl"):

    N_train, C, T = X_train.shape
    N_test = X_test.shape[0]

    X_train_flat = X_train.transpose(0, 2, 1).reshape(-1, C)
    X_test_flat  = X_test.transpose(0, 2, 1).reshape(-1, C)

    scaler = StandardScaler()
    scaler.fit(X_train_flat)

    X_train_norm = scaler.transform(X_train_flat)\
        .reshape(N_train, T, C).transpose(0, 2, 1)

    X_test_norm = scaler.transform(X_test_flat)\
        .reshape(N_test, T, C).transpose(0, 2, 1)

    joblib.dump(scaler, scaler_path)

    print(f"Scaler saved to '{scaler_path}'")

    return (
        X_train_norm.astype(np.float32),
        X_test_norm.astype(np.float32),
    )
