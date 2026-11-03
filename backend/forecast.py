"""Demand forecasting for the site's non-EV ("base") load.

A RandomForest is trained at start-up on synthetic multi-day data
(time of day + temperature -> kW). The scheduler and pricing engine use its
look-ahead predictions to anticipate evening peaks before they happen.
"""
import math

import numpy as np
from sklearn.ensemble import RandomForestRegressor
from sklearn.metrics import mean_absolute_error

NOMINAL_KW = 40.0


def temperature(minute_of_day: float) -> float:
    """Outdoor temperature (deg C): coolest ~03:00, warmest ~15:00."""
    h = minute_of_day / 60.0
    return 27.0 + 7.0 * math.sin((h - 9.0) / 24.0 * 2 * math.pi)


def true_profile(minute_of_day: float, temp_offset: float = 0.0) -> float:
    """Ground-truth base load (kW): small morning bump, big evening peak,
    plus an air-conditioning term when it is hot."""
    h = minute_of_day / 60.0
    temp = temperature(minute_of_day) + temp_offset
    shape = (
        0.30
        + 0.30 * math.exp(-((h - 8.5) ** 2) / 3.0)
        + 0.45 * math.exp(-((h - 19.0) ** 2) / 4.0)
    )
    cooling = max(0.0, temp - 28.0) * 0.02
    return NOMINAL_KW * (shape + cooling)


def _features(minutes, temps):
    ang = 2 * np.pi * (np.asarray(minutes, float) % 1440) / 1440
    return np.column_stack([np.sin(ang), np.cos(ang), np.asarray(temps, float)])


class Forecaster:
    def __init__(self, seed: int = 7):
        self.seed = seed
        self.model = RandomForestRegressor(
            n_estimators=60, min_samples_leaf=3, random_state=seed, n_jobs=1
        )
        self.mae_kw = None

    def train(self, days: int = 45):
        rng = np.random.default_rng(self.seed)
        X, y = [], []
        for _ in range(days):
            off = rng.normal(0, 2.5)  # each synthetic day has its own weather
            for m in range(0, 1440, 10):
                X.append(_features([m], [temperature(m) + off])[0])
                y.append(true_profile(m, off) * (1 + rng.normal(0, 0.05)))
        X, y = np.array(X), np.array(y)
        split = int(len(X) * 0.8)
        self.model.fit(X[:split], y[:split])
        self.mae_kw = float(mean_absolute_error(y[split:], self.model.predict(X[split:])))
        self.model.fit(X, y)  # final fit on everything
        return self

    def predict(self, minutes) -> np.ndarray:
        minutes = np.atleast_1d(np.asarray(minutes, float)) % 1440
        temps = [temperature(m) for m in minutes]
        return self.model.predict(_features(minutes, temps))

    def horizon(self, now_min: float, hours: int = 6, step: int = 15):
        """Predicted base load for the next `hours`, every `step` minutes."""
        offs = np.arange(0, hours * 60 + 1, step)
        preds = self.predict(now_min + offs)
        return [(int(o), float(p)) for o, p in zip(offs, preds)]
