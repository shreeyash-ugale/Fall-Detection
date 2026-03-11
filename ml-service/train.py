import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset, WeightedRandomSampler
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix

from model import FallCNN
from preprocessing import load_sisfall, normalize, CHANNELS, WINDOW_SIZE

DATASET_DIR  = "dataset/SisFall_dataset"
EPOCHS       = 50
BATCH_SIZE   = 64
LR           = 1e-3
PATIENCE     = 7         
MODEL_PATH   = "model.pth"
SCALER_PATH  = "scaler.pkl"
DEVICE       = torch.device("cuda" if torch.cuda.is_available() else "cpu")

print(f"Using device: {DEVICE}")

print("\n── Loading SisFall dataset ──")
X, y = load_sisfall(DATASET_DIR)

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

print("\n── Normalizing ──")
X_train, X_test = normalize(X_train, X_test, scaler_path=SCALER_PATH)

class_counts  = np.bincount(y_train)
class_weights = 1.0 / class_counts
sample_weights = class_weights[y_train]
sampler = WeightedRandomSampler(
    weights=torch.tensor(sample_weights, dtype=torch.float32),
    num_samples=len(sample_weights),
    replacement=True,
)

train_dataset = TensorDataset(
    torch.tensor(X_train, dtype=torch.float32),
    torch.tensor(y_train, dtype=torch.float32).unsqueeze(1),
)
test_dataset = TensorDataset(
    torch.tensor(X_test, dtype=torch.float32),
    torch.tensor(y_test, dtype=torch.float32).unsqueeze(1),
)

train_loader = DataLoader(train_dataset, batch_size=BATCH_SIZE, sampler=sampler)
test_loader  = DataLoader(test_dataset,  batch_size=BATCH_SIZE, shuffle=False)

model = FallCNN(in_channels=CHANNELS, window_size=WINDOW_SIZE).to(DEVICE)
criterion = nn.BCELoss()
optimizer = optim.Adam(model.parameters(), lr=LR, weight_decay=1e-4)
scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, patience=3, factor=0.5)

total_params = sum(p.numel() for p in model.parameters())
print(f"\nModel parameters: {total_params:,}")

print("\n── Training ──")

best_val_loss  = float("inf")
patience_count = 0
best_weights   = None

for epoch in range(1, EPOCHS + 1):

    model.train()
    train_loss = 0.0
    for X_batch, y_batch in train_loader:
        X_batch, y_batch = X_batch.to(DEVICE), y_batch.to(DEVICE)
        optimizer.zero_grad()
        outputs = model(X_batch)
        loss = criterion(outputs, y_batch)
        loss.backward()
        optimizer.step()
        train_loss += loss.item() * len(X_batch)
    train_loss /= len(train_dataset)

    model.eval()
    val_loss = 0.0
    correct  = 0
    with torch.no_grad():
        for X_batch, y_batch in test_loader:
            X_batch, y_batch = X_batch.to(DEVICE), y_batch.to(DEVICE)
            outputs = model(X_batch)
            val_loss += criterion(outputs, y_batch).item() * len(X_batch)
            preds = (outputs > 0.5).float()
            correct += (preds == y_batch).sum().item()
    val_loss /= len(test_dataset)
    val_acc   = correct / len(test_dataset)

    scheduler.step(val_loss)

    print(f"Epoch {epoch:03d}/{EPOCHS} | "
          f"Train Loss: {train_loss:.4f} | "
          f"Val Loss: {val_loss:.4f} | "
          f"Val Acc: {val_acc:.4f}")

    if val_loss < best_val_loss:
        best_val_loss = val_loss
        best_weights  = {k: v.clone() for k, v in model.state_dict().items()}
        patience_count = 0
    else:
        patience_count += 1
        if patience_count >= PATIENCE:
            print(f"\nEarly stopping triggered at epoch {epoch}.")
            break

model.load_state_dict(best_weights)

print("\n── Evaluation ──")
model.eval()
all_preds, all_labels = [], []

with torch.no_grad():
    for X_batch, y_batch in test_loader:
        X_batch = X_batch.to(DEVICE)
        outputs = model(X_batch)
        preds   = (outputs > 0.5).float().cpu().numpy()
        all_preds.extend(preds.flatten())
        all_labels.extend(y_batch.numpy().flatten())

all_preds  = np.array(all_preds,  dtype=int)
all_labels = np.array(all_labels, dtype=int)

print("\nClassification Report:")
print(classification_report(all_labels, all_preds, target_names=["Non-Fall", "Fall"]))

print("Confusion Matrix:")
cm = confusion_matrix(all_labels, all_preds)
print(f"  TN={cm[0,0]}  FP={cm[0,1]}")
print(f"  FN={cm[1,0]}  TP={cm[1,1]}")


torch.save(model.state_dict(), MODEL_PATH)
print(f"\nModel saved to '{MODEL_PATH}'")
