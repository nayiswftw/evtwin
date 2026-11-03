"""Dynamic pricing: the tighter the grid, the more a kWh costs (INR)."""

BASE_PRICE = 8.0
MIN_PRICE = 6.0
MAX_PRICE = 30.0


def price_per_kwh(utilisation: float) -> float:
    """utilisation = (expected base load + EV demand) / grid capacity."""
    u = max(0.0, utilisation)
    p = BASE_PRICE * (1.0 + 2.2 * u * u)
    return round(min(MAX_PRICE, max(MIN_PRICE, p)), 2)
