# Order Data Schema

This document is the **contract** between the q data generator (`gen_orders.q`) and the C++ orderbook consumer (`orderbook.cpp`). Both sides must stay in sync with it. If you change either the generator or the consumer, update this document first.

---

## CSV Layout

The file `orders.csv` is written by `gen_orders.q` using q's `save` builtin. It contains a header row followed by N data rows. There are no blank lines and no trailing commas.

| Column | Position | CSV Type | q Type | C++ Type | Description | Example |
|--------|----------|----------|--------|----------|-------------|---------|
| `price` | 1 | Decimal number | float (`f`) | `double` | Limit price of the order, rounded to 2 decimal places | `99.85` |
| `qty` | 2 | Decimal number | float (`f`) | `double` | Order quantity (number of units), minimum 1.0 | `47.0` |
| `side` | 3 | String | sym (`s`) | `Side` enum | Which side of the book: `B` for Buy, `S` for Sell | `B` |

### Example CSV (first 4 lines)

```
price,qty,side
99.85,47,B
100.1,12,S
99.95,310,B
```

---

## Column Details

### `price`

- **Type**: IEEE 754 double-precision float
- **Valid range**: Guaranteed within [MID - PRICE_BAND, MID + PRICE_BAND] = [97.50, 102.50] with default settings
- **Precision**: Rounded to 2 decimal places (e.g. `100.05`, never `100.053`)
- **Distribution**: Mean-reverting; prices are concentrated near MID (100.0) with a standard deviation well below PRICE_BAND
- **Generation method**: Ornstein-Uhlenbeck (OU) mean-reverting walk seeded at MID. Each step reverts toward MID with strength THETA, then adds Uniform[-STEP, +STEP] noise. Prices are subsequently clamped to [MID - PRICE_BAND, MID + PRICE_BAND] as a hard safety net.

### `qty`

- **Type**: IEEE 754 double-precision float, always a whole number value
- **Valid range**: [1.0, 1000.0] — minimum 1 unit, maximum 1000 units
- **Distribution**: Right-skewed (exponential-like). Most orders are small (1–100); large orders up to 1000 appear occasionally
- **Generation method**: Exponential draw with mean ~50 (`QTY_SCALE`), floored and clamped to [1, `QTY_MAX`]

### `side`

- **Type**: Single character string in the CSV (`B` or `S`)
- **Values**:
  - `B` — Buy order (bid side)
  - `S` — Sell order (ask side)
- **Distribution**: Approximately 50/50 Buy/Sell (independent Bernoulli with p=0.5)
- **In q**: Stored as symbol (`` `B `` or `` `S ``); q's `save` writes symbols without the backtick

---

## Mapping to the C++ `Order` Struct

```cpp
struct Order {
    int    id;        // NOT in CSV — assigned by C++ on insert
    double price;     // CSV column 1: "price"
    double quantity;  // CSV column 2: "qty"
    Side   side;      // CSV column 3: "side" — parse "B" -> Side::Buy, "S" -> Side::Sell
};
```

| C++ field | CSV column | Notes |
|-----------|------------|-------|
| `id` | — | Not present in CSV. C++ assigns this sequentially on `addOrder()`. |
| `price` | `price` | Parse as double with `std::stod`. No suffix to strip. |
| `quantity` | `qty` | Parse as double with `std::stod`. Value is always integral (e.g. `47`). |
| `side` | `side` | Map `"B"` -> `Side::Buy`, `"S"` -> `Side::Sell`. Any other value is malformed input. |

---

## Parsing Notes for the C++ Consumer

1. **Skip the header row**: The first line is always `price,qty,side`.
2. **Float values are clean**: The generator strips q's trailing `f` suffix after writing, so `price` and `qty` values are plain decimal strings (e.g. `99.85`, `47`). Use `std::stod` directly.
3. **Column order is fixed**: Columns are always in the order `price, qty, side`. Do not rely on header parsing to determine column positions.
4. **No nulls**: The generator does not produce null or missing values.
5. **No quotes**: String values (`B`, `S`) are not quoted in the CSV.

---

## Generator Configuration

The following constants at the top of `gen_orders.q` control the generated data:

| Variable | Default | Effect |
|----------|---------|--------|
| `N` | `1000000` | Number of rows |
| `MID` | `100.0` | Starting mid-price and OU reversion target |
| `STEP` | `0.05` | Per-tick noise amplitude (tick size) |
| `THETA` | `0.05` | OU reversion strength — higher values pull prices more aggressively back toward MID, producing a tighter price range |
| `PRICE_BAND` | `2.50` | Hard price band around MID; any price outside [MID - PRICE_BAND, MID + PRICE_BAND] is clamped to the nearest boundary |
| `QTY_SCALE` | `50.0` | Mean quantity before clamping |
| `QTY_MAX` | `1000.0` | Maximum quantity per order |

---

## Running the Generator

```bash
q /home/developer/Documents/Claude/cpp/orderbook/data/gen_orders.q
```

Output is written to `/home/developer/Documents/Claude/cpp/orderbook/data/orders.csv`.
