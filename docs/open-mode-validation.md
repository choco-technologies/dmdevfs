# Device access and mount lifetime validation

DMFSI represents read/write access as 3, whereas DMDRVI uses 4. dmdevfs now
maps the access bits at its filesystem/driver boundary and preserves the
existing non-access flags. Invalid access mode zero is rejected before calling
the driver. Each test handle records the actual flags received by the driver.

Validation on 2026-10-16:

- Native `test_dmdevfs`: 10/10 steps, including the new read, write, read/write,
  create/truncate, append and invalid-mode regression.
- Native `test_partitions` passed; STM32F7 build passed.
- STM32F746G-DISCO booted with the changed dmdevfs: RTC 11/11 and RNG 6/6.
  The generic rtc CLI sets time through an `r+b` VFS handle successfully.
- Before the lifetime fix below, full native ctest: 7/9 targets passed. `test_dmdevmon` and `test_order`
  terminate with SIGSEGV in repeated mount/configuration operations. Both
  reproduce with the unchanged `origin/main` dmdevfs source and the same test
  environment (exit -11). A GDB capture of test_order places the fault in the
  hotplug thread's Dmod_GetDifFunction/Dmod_Context_IsValid with an invalid
  module-context pointer. Those failures are not covered by the passing
  access-mode regression; the lifetime fix below resolves them.

The test CMake scope clears inherited DMOD_MAL_IMPLS: the filesystem, rather
than its consumers, implements dmdrvi notification callbacks. Dependencies are
unversioned in CMake; the packager derives required API versions.

## Mount/worker lifetime fix

The class-probing worker released g_devfs_mutex after its live-mount lookup,
then accessed the mount's driver nodes and module context without retaining
them. Concurrent filesystem deinit removed the mount from the registry and
freed those objects. Repeated mount/unmount tests therefore raced a real
use-after-free, independently of filesystem open-mode translation.

Worker operations now retain a mount reference under the registry mutex.
Unmount unregisters it to block new worker references, then sleeps on a
per-mount semaphore until active operations finish before destroying drivers.
No global mutex is held while waiting or invoking driver callbacks. Pending
mount notifications carry a generation so reuse of a freed mount address
cannot make a stale notification operate on a newer mount. Failed mount
initialization follows the same teardown ordering.

Validation on 2026-10-16:

- Full native ctest: 10/10, including test_order and test_dmdevmon.
- Both previous failures plus the lifetime regression: 20 consecutive passes
  per target (60 executions).
- The new regression pauses a real mock-driver class probe with semaphores,
  starts unmount concurrently and verifies the driver is not freed and
  unmount cannot complete before releasing the probe. A second mount with a
  queued notification can be unmounted while the worker is blocked elsewhere.
- The regression fails against the exact pre-fix dmdevfs source: driver free
  and unmount complete prematurely (0/1 steps, exit 1).
- STM32F7 build passed.

## RTC device reporting

RTC nodes are probed with GET_INFO, not GET_TIME, so an unset calendar is still
reported to libsystemd. The new rtc report mask participates in the existing
all/none/single-class filtering and device removal notifies the matching rule.
Native ctest passes 10/10 targets, including 11/11 node-reporting steps covering
RTC startup, unset calendars, report filtering and service stop on unmount.
STM32F7 compilation passed; the integrated board image starts dmutc@rtc as a
library unit through the class=rtc rule.
