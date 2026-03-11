import torch
import torch.nn as nn


class FallCNN(nn.Module):
    def __init__(self, in_channels: int = 3, window_size: int = 200, dropout: float = 0.3):
        """
        Args:
            in_channels : 3 (acc only) or 6 (acc + gyro)
            window_size : number of time steps per window (default 200)
            dropout     : dropout probability for regularization
        """
        super(FallCNN, self).__init__()

        self.conv_block1 = nn.Sequential(
            nn.Conv1d(in_channels, 32, kernel_size=5, padding=2),
            nn.BatchNorm1d(32),
            nn.ReLU(),
            nn.MaxPool1d(2),
            nn.Dropout(dropout),
        )

        self.conv_block2 = nn.Sequential(
            nn.Conv1d(32, 64, kernel_size=5, padding=2),
            nn.BatchNorm1d(64),
            nn.ReLU(),
            nn.MaxPool1d(2),
            nn.Dropout(dropout),
        )

        flat_size = self._get_flat_size(in_channels, window_size)

        self.classifier = nn.Sequential(
            nn.Linear(flat_size, 128),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(128, 1),
            nn.Sigmoid(),
        )

    def _get_flat_size(self, in_channels: int, window_size: int) -> int:
        """Run a dummy forward pass to compute flattened feature size."""
        with torch.no_grad():
            dummy = torch.zeros(1, in_channels, window_size)
            dummy = self.conv_block1(dummy)
            dummy = self.conv_block2(dummy)
            return dummy.view(1, -1).shape[1]

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.conv_block1(x)
        x = self.conv_block2(x)
        x = x.view(x.size(0), -1)
        x = self.classifier(x)
        return x