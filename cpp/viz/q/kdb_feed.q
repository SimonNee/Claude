/ kdb_feed.q — Simulated exchange publisher for the orderbook visualiser
/ Usage:
/   q kdb_feed.q                        (default port 7890, rate 100ms)
/   q kdb_feed.q -port 7891 -rate 50    (custom port and rate)
/ Prerequisites:
/   Start the C++ visualiser first: ./viz --kdb [port]
/   The C++ process must be in listen() state before this script connects.
/ What it does:
/   Connects outbound to the C++ TCP listener on the configured port.
/   Each timer tick sends one L2 MBP event (side, tick, qty) encoded as a
/   32-byte KDB+ byte vector matching the ReplayEvent struct wire layout.
/   Mid price follows an Ornstein-Uhlenbeck (OU) process:
/     dP = theta*(mu - P)*dt + sigma*dW
/   Defaults simulate ETH/USDT: mu=2000.0, sigma=10.0, tick_size=0.01.
/ Wire format (little-endian, Linux x86-64):
/   KDB+ prepends a 16-byte IPC envelope automatically when using neg[h].
/   The C++ side reads 48 bytes total and memcpys bytes [16..47] into
/   a ReplayEvent struct. This script sends only the 32-byte payload.
/ Byte layout of the 32-byte payload (matches ReplayEvent):
/   offset  0, 8 bytes: timestamp_ns  (uint64 LE) virtual clock nanoseconds
/   offset  8, 4 bytes: tick          (uint32 LE) absolute price tick
/   offset 12, 4 bytes: _pad          (uint32 LE) zeros
/   offset 16, 8 bytes: qty           (uint64 LE) scaled qty x 10^8
/   offset 24, 1 byte:  side          (uint8)     0=BID, 1=ASK
/   offset 25, 7 bytes: _pad2         (uint8[7])  zeros

/ --- Configuration ---
PORT:7890
PUSH_RATE_MS:100
MU:2000.0
THETA:0.005
SIGMA:10.0
TICK_SIZE:0.01
TICKS_PER_DOLLAR:100
BASE_PRICE:0.0
MAX_TICK:300000
SEED:42

/ --- State ---
/ .feed.mid    current mid price (initialised to MU at startup)
/ .feed.vclock virtual clock nanoseconds (monotonically increasing)
/ .feed.h      connection handle to C++ listener (null until connected)
/ .feed.running flag: 1b while active, set to 0b on disconnect
.feed.h:0N
.feed.running:0b

/ --- Box-Muller normal random variable ---
/ Returns one standard-normal float.
/ The 1e-15 floor on u1 prevents log[0] (which gives -0w in q).
.feed.randn:{[]
    u1:1e-15 | rand 1.0;
    u2:rand 1.0;
    sqrt[-2*log u1] * cos 2*acos[-1]*u2
 }

/ --- Build a 32-byte ReplayEvent byte vector ---
/ Arguments:
/   side  long  0=BID 1=ASK
/   tick  long  absolute price tick (1..MAX_TICK)
/   qty   long  scaled quantity x 10^8
/ Returns a 32-element byte vector matching the ReplayEvent wire layout.
/ Encoding: 0x0 vs x produces big-endian bytes; reverse gives little-endian.
/ Verification: count .feed.makeEvent[0;200000;100000000] must equal 32.
/ Note: tick 200000 is valid — ReplayEvent.tick is uint32, C++ book auto-rebases.
.feed.makeEvent:{[side;tick;qty]
    ts_bytes  : reverse 0x0 vs `long$.feed.vclock;     / 8 bytes LE  offset  0
    tick_bytes: reverse 0x0 vs `int$tick;              / 4 bytes LE  offset  8
    pad_bytes : 4#0x00;                                / 4 bytes     offset 12
    qty_bytes : reverse 0x0 vs `long$qty;              / 8 bytes LE  offset 16
    side_byte : enlist `byte$side;                     / 1 byte      offset 24
    pad2_bytes: 7#0x00;                                / 7 bytes     offset 25
    ts_bytes,tick_bytes,pad_bytes,qty_bytes,side_byte,pad2_bytes
 }

/ --- Generate one OU step and push one event to C++ ---
/ Called each timer tick while .feed.running is 1b.
/ OU update: P(t+dt) = P(t) + theta*(mu - P(t)) + sigma*N(0,1)
/ Mid price is clamped to [BASE_PRICE+TICK_SIZE, BASE_PRICE+MAX_TICK*TICK_SIZE].
/ Tick is clamped to [1, MAX_TICK] as a belt-and-suspenders guard.
.feed.push:{[]
    / Advance virtual clock by one interval (nanoseconds)
    .feed.vclock+:PUSH_RATE_MS * 1000000j;

    / OU mean reversion step
    .feed.mid+:(MU - .feed.mid) * THETA;

    / OU diffusion step
    .feed.mid+:SIGMA * .feed.randn[];

    / Clamp mid price to valid tick range
    min_price:BASE_PRICE + TICK_SIZE;
    max_price:BASE_PRICE + MAX_TICK * TICK_SIZE;
    .feed.mid:min_price | max_price & .feed.mid;

    / Convert mid price to integer tick
    tick:`long$floor (.feed.mid - BASE_PRICE) * TICKS_PER_DOLLAR + 0.5;
    tick:1 | MAX_TICK & tick;

    / Random side: 0=BID, 1=ASK with equal probability
    side:`long$0.5 < rand 1.0;

    / Random quantity 1..9 lots (scaled by 10^8)
    qty:`long$1e8 * 1 + `long$9 * rand 1.0;

    / Encode, frame, and push to C++
    neg[.feed.h] .feed.makeEvent[side;tick;qty];
 }

/ --- Timer callback ---
/ Fires every PUSH_RATE_MS milliseconds while the timer is running.
/ Guard on .feed.running protects against a tick firing during shutdown.
.z.ts:{[] if[.feed.running; .feed.push[]]}

/ --- Disconnect callback ---
/ Called by KDB+ when the remote process closes the connection.
/ Stops the timer (\t 0) and marks the session as inactive.
.z.pc:{[h] if[h=.feed.h; .feed.running:0b; system "t 0"; .feed.h:0N]}

/ --- Startup sequence ---

/ Override defaults from CLI args if provided
/ .Q.opt .z.x parses -key value pairs into a symbol->string dict
if[`port in key .Q.opt .z.x; PORT:`long$.Q.opt[.z.x]`port]
if[`rate in key .Q.opt .z.x; PUSH_RATE_MS:`long$.Q.opt[.z.x]`rate]

/ Seed the RNG for reproducible sequences (override with -seed N if needed)
system "S ",string SEED

/ Initialise state
.feed.mid:MU
.feed.vclock:0j
.feed.running:0b

/ Connect outbound to the C++ listener
/ hopen blocks until the TCP handshake completes.
/ If C++ is not yet in listen() state this will throw a connection-refused error.
-1 "Connecting to C++ listener on port ",string PORT;
.feed.h:hopen `$"::",string PORT
-1 "Connected. Pushing at ",string[PUSH_RATE_MS],"ms intervals.";

/ Arm the timer and mark as running
.feed.running:1b
system "t ",string PUSH_RATE_MS
