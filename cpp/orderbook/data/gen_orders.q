/ gen_orders.q
/ Generates N rows of synthetic limit order data and saves to orders.csv.
/ The CSV is consumed by the C++ orderbook (cpp/orderbook/).
/ See schema.md in this directory for the full contract.

/ ---------------------------------------------------------------------------
/ Configuration — edit these values to change the generated data set
/ ---------------------------------------------------------------------------

N:1000000        / number of orders to generate
MID:100.0        / starting mid-price and OU reversion target
STEP:0.05        / per-tick noise amplitude (tick size)
THETA:0.05       / OU reversion strength — higher = tighter range around MID
PRICE_BAND:2.50  / hard price band around MID (50 ticks * 0.05)
QTY_SCALE:50.0   / mean quantity for the exponential draw (before clamping)
QTY_MAX:1000.0   / hard cap on any single order quantity

/ ---------------------------------------------------------------------------
/ Price generation
/ ---------------------------------------------------------------------------
/ Ornstein-Uhlenbeck mean-reverting price walk.
/ Each step: new_price = prev + (-THETA * (prev - MID)) + (STEP * noise)
/ where noise ~ Uniform[-1, +1].
/
/ ouStep is a 2-arg function suitable for seeded scan (\):
/   ouStep[prev; n] returns the next price given previous price and noise n.
/ f\[seed; list] applies f left-to-right, seeded with MID, producing N prices.
/
/ After the walk, clamp to [MID-PRICE_BAND, MID+PRICE_BAND] as a hard safety
/ net against rare extreme excursions.
/ Round to 2 decimal places via the integer trick: floor(x*100+0.5)/100.

noise:(2.0 * N?1.0) - 1.0                          / N uniform draws on [-1, +1]
rawPrices:{x + (neg[THETA] * x - MID) + STEP * y}\[MID; noise]  / OU walk
rawPrices:(MID-PRICE_BAND) | (MID+PRICE_BAND) & rawPrices        / clamp to band
prices:"f"$(`long$rawPrices * 100.0) % 100.0   / round to 2dp

/ ---------------------------------------------------------------------------
/ Quantity generation
/ ---------------------------------------------------------------------------
/ Exponential distribution: qty = floor(1 + (-QTY_SCALE * log(1-u)))
/ where u ~ Uniform(0,1).
/ This produces a right-skewed distribution: most orders are small (1-100),
/ but occasional large orders up to QTY_MAX appear naturally.
/ After flooring, clamp to [1, QTY_MAX].

u:N?1.0                                  / N uniform draws on [0,1)
rawQtys:1.0 + neg[QTY_SCALE] * log 1.0 - u   / exponential-like, shifted up by 1
qtys:"f"$1.0 | QTY_MAX & floor rawQtys  / floor then clamp to [1, QTY_MAX]

/ ---------------------------------------------------------------------------
/ Side generation
/ ---------------------------------------------------------------------------
/ Each order is independently assigned Buy or Sell with equal probability.
/ sideIdx is 0 or 1; indexing into `B`S maps 0->`B and 1->`S.
/ Stored as sym so the CSV writer produces "B" or "S" (no backtick).

sideIdx:N?2                              / N random ints: 0 or 1
sides:`B`S[sideIdx]                      / map to sym `B or `S

/ ---------------------------------------------------------------------------
/ Assemble table
/ ---------------------------------------------------------------------------

orders:([]
    price:prices;
    qty:qtys;
    side:sides)

/ ---------------------------------------------------------------------------
/ Save to CSV
/ ---------------------------------------------------------------------------

outFile:hsym `$"/home/developer/Documents/Claude/cpp/orderbook/data/orders.csv"

save outFile

/ ---------------------------------------------------------------------------
/ Strip trailing 'f' suffix from float values
/ ---------------------------------------------------------------------------
/ q's save writes floats with a trailing 'f' (e.g. "99.85f", "47f").
/ Read the file back as lines, replace every 'f' character with nothing,
/ then write the lines back out.
/ Safety check: none of the column names (price, qty, side) or the side
/ values (B, S) contain the letter 'f', so a blanket replace is correct
/ for this specific schema.

lines:read0 outFile                          / list of strings, one per line
cleaned:{ssr[x;enlist"f";""]} each lines    / strip all 'f' from each line
outFile 0: cleaned                           / write lines back (no newline added by 0:)

-1 "Generated ", string[N], " orders -> ", string outFile;
