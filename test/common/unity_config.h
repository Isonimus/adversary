#pragma once

// Unity configuration for the native test environment.
//
// PlatformIO's test runner compiles Unity with -DUNITY_INCLUDE_CONFIG_H, which
// makes unity_internals.h #include "unity_config.h". This file satisfies that
// include (found via the `-I test/common` flag in the [env:native] build).
//
// The native build runs on the host, so Unity's defaults (stdio output via
// putchar/printf) are correct — no overrides are required. Keep this minimal;
// add UNITY_* overrides here only if a specific test needs them.
