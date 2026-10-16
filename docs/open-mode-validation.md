# Device open-mode translation validation

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
- Full native ctest: 7/9 targets passed. `test_dmdevmon` and `test_order`
  terminate with SIGSEGV in repeated mount/configuration operations. Both
  reproduce with the unchanged `origin/main` dmdevfs source and the same test
  environment (exit -11). A GDB capture of test_order places the fault in the
  hotplug thread's Dmod_GetDifFunction/Dmod_Context_IsValid with an invalid
  module-context pointer. Those failures are not covered by the passing
  access-mode regression and remain unresolved.

The test CMake scope clears inherited DMOD_MAL_IMPLS: the filesystem, rather
than its consumers, implements dmdrvi notification callbacks. Dependencies are
unversioned in CMake; the packager derives required API versions.
