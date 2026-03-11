import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset
from sklearn.metrics import (
    classification_report,
    confusion_matrix,
    roc_auc_score,
    roc_curve,
)
from sklearn.model_selection import train_test_split
import joblib

from model import FallCNN
from preprocessing import load_sisfall, CHANNELS, WINDOW_SIZE

DATASET_DIR = "dataset"
MODEL_PATH  = "model.pth"
SCALER_PATH = "scaler.pkl"
DEVICE      = torch.device("cpu")

print("Loading dataset...")
X, y = load_sisfall(DATASET_DIR)
_, X_test, _, y_test = train_test_split(X, y, test_size=0.2, random_state=42, stratify=y)

scaler = joblib.load(SCALER_PATH)
N, C, T   = X_test.shape
X_flat    = X_test.transpose(0, 2, 1).reshape(-1, C)
X_test    = scaler.transform(X_flat).reshape(N, T, C).transpose(0, 2, 1).astype(np.float32)

test_loader = DataLoader(
    TensorDataset(
        torch.tensor(X_test),
        torch.tensor(y_test, dtype=torch.float32).unsqueeze(1),
    ),
    batch_size=64,
    shuffle=False,
)

model = FallCNN(in_channels=CHANNELS, window_size=WINDOW_SIZE).to(DEVICE)
model.load_state_dict(torch.load(MODEL_PATH, map_location=DEVICE))
model.eval()
print(f"Model loaded from '{MODEL_PATH}'")

all_probs, all_preds, all_labels = [], [], []

with torch.no_grad():
    for X_batch, y_batch in test_loader:
        probs = model(X_batch).numpy().flatten()
        preds = (probs > 0.5).astype(int)
        all_probs.extend(probs)
        all_preds.extend(preds)
        all_labels.extend(y_batch.numpy().flatten().astype(int))

all_probs  = np.array(all_probs)
all_preds  = np.array(all_preds)
all_labels = np.array(all_labels)

print("\n── Classification Report ──")
print(classification_report(all_labels, all_preds, target_names=["Non-Fall", "Fall"]))

cm = confusion_matrix(all_labels, all_preds)
print("── Confusion Matrix ──")
print(f"             Predicted Non-Fall   Predicted Fall")
print(f"Actual Non-Fall      {cm[0,0]:<10}    {cm[0,1]}")
print(f"Actual Fall          {cm[1,0]:<10}    {cm[1,1]}")

auc = roc_auc_score(all_labels, all_probs)
print(f"\nROC-AUC Score: {auc:.4f}")

tp, fp, fn, tn = cm[1,1], cm[0,1], cm[1,0], cm[0,0]
sensitivity = tp / (tp + fn + 1e-9)
specificity = tn / (tn + fp + 1e-9)
print(f"Sensitivity (Fall Recall) : {sensitivity:.4f}")
print(f"Specificity (Non-Fall Recall): {specificity:.4f}")