# dmdevmon

Generic monitor service: one instance per device node whose driver implements
the dmdrvi monitor contract (`DMDRVI_IOCTL_MONITOR_GET_POLICY` / `_EVENT` /
`_REFRESH`, dmdrvi >= 2.1 - see dmdrvi's `docs/dmdrvi.md`, "Monitor Ioctl
Commands").

Drivers never create threads for work that happens over time - card
insertion and removal, USB port changes, media or link polling. They declare
what should trigger them and dmdevmon does the waiting.

## How it runs

1. dmdevfs reports every node that answers `MONITOR_GET_POLICY` to libsystemd
   as class `monitor`, e.g. `("monitor", "dmsdio0", "/dev/dmsdio0")`.
2. The `[class=monitor]` rule in [`configs/dmdevmon.rules`](configs/dmdevmon.rules)
   starts `dmdevmon@dmsdio0` from [`configs/dmdevmon@.ini`](configs/dmdevmon@.ini)
   with the node path as its only argument.
3. dmdevmon reads the policy (`event_handler`, `settle_ms`,
   `poll_interval_ms`), registers `event_handler` with dmhaman - the handler
   runs in interrupt context and only posts a semaphore - and calls
   `MONITOR_REFRESH` once.
4. Then it sleeps on that semaphore:
   - **event** -> `MONITOR_EVENT` at once, then again once per settle round
     while events keep coming, until a whole `settle_ms` passes without one;
     then `MONITOR_REFRESH`. A bouncing contact yields one refresh.
   - **`poll_interval_ms` elapsed** -> `MONITOR_REFRESH`.
5. With neither an event handler nor a poll interval there is nothing to wait
   for: dmdevmon exits with 0 after the first refresh.

When the node disappears, dmdevfs reports its removal and libsystemd stops
the unit.

## Stopping

dmdevmon registers its semaphore with `libsystemd_set_stop_semaphore()`: a
stop (`service stop dmdevmon@dmsdio0`, node removal) wakes it, it finishes
the refresh it may be in, unregisters the event handler and returns from
`main()` - the driver's lock is never left held by a killed thread. The unit
sets `stop_timeout_ms=5000`, enough for a slow refresh such as an SD card
identification; only a monitor stuck longer than that is killed, in which
case a process exit callback still unregisters the event handler.

## Exit status and restarts

dmdevmon exits with 0 when there is nothing to monitor, when the node is not
a monitored node or does not exist, and after a stop. It only fails
(`-ENOMEM`) on resource shortage, so the unit's `restart=on-failure` never
spins on a node that cannot be monitored.

## Installing

Copy [`configs/dmdevmon.rules`](configs/dmdevmon.rules) into libsystemd's
rules directory and [`configs/dmdevmon@.ini`](configs/dmdevmon@.ini) into its
units directory. Both ship in the dmdevmon package under `configs/`.

## Usage

```
dmdevmon <node>
```

Not meant to be started by hand; `<node>` is the absolute path of the
monitored node (e.g. `/dev/dmsdio0`). Each request opens the node, issues one
ioctl and closes it, so a stopped or killed instance never leaves a handle
open.

Use a distinct `event_handler` name per node: dmhaman keeps one registration
per monitor, and two monitors sharing a name would both be woken by it.
