// Self-test for the build-warnings gate (ADR-0083). Zero-dependency (node:test + node:assert):
//   node --test scripts/lint-build-warnings.test.mjs
// The vendored fixture lines below are real lines captured from a cardputer build on
// 2026-10-20 (nimconfig.h macro-redefinition, tusb_types.h -Wextra), so the test proves the
// gate ignores exactly the noise the real build emits.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scanLog, isOurSrcPath } from './lint-build-warnings.mjs';

// --- Real vendored warning lines from the build (must be ignored) --------------------------
const NIMCONFIG_WARNING =
  '.pio/libdeps/cardputer/NimBLE-Arduino/src/nimconfig.h:154:9: warning: "CONFIG_BT_NIMBLE_ROLE_CENTRAL" redefined';
const TUSB_WARNING =
  '/home/iker/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/include/arduino_tinyusb/tinyusb/src/common/tusb_types.h:557:47: warning: enumerated and non-enumerated type in conditional expression [-Wextra]';

test('ignores the real vendored warnings the build always emits', () => {
  assert.equal(scanLog(NIMCONFIG_WARNING).length, 0); // .pio/ -> not under src/
  assert.equal(scanLog(TUSB_WARNING).length, 0);       // absolute .platformio path -> not src/
});

test('flags a warning on an our-src path', () => {
  const log = "src/modules/foo/bar.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]";
  const findings = scanLog(log);
  assert.equal(findings.length, 1);
  assert.equal(findings[0].path, 'src/modules/foo/bar.cpp');
  assert.equal(findings[0].line, 12);
  assert.equal(findings[0].col, 5);
  assert.match(findings[0].message, /unused variable/);
});

test('flags a warning in an our-src header too', () => {
  const log = 'src/modules/ble/ble_scanner.h:7:1: warning: extra tokens at end of directive';
  assert.equal(scanLog(log).length, 1);
});

test('handles a warning line with no column', () => {
  const findings = scanLog('src/main.cpp:42: warning: something happened');
  assert.equal(findings.length, 1);
  assert.equal(findings[0].line, 42);
  assert.equal(findings[0].col, 0);
});

test('strips a leading ./ from the path', () => {
  const findings = scanLog('./src/utils/mac_utils.cpp:9:3: warning: comparison is always true');
  assert.equal(findings.length, 1);
  assert.equal(findings[0].path, 'src/utils/mac_utils.cpp');
});

test('ignores vendored trees that live inside src/', () => {
  assert.equal(scanLog('src/lgfx/v1/lgfx_fonts.cpp:3:1: warning: unused parameter').length, 0);
  assert.equal(scanLog('src/fl/bitset.h:88:5: warning: narrowing conversion').length, 0);
  assert.equal(scanLog('src/common/bits.h:10:1: warning: x').length, 0);
  assert.equal(scanLog('src/M5Unified.cpp:5:1: warning: deprecated conversion').length, 0);
});

test('does not match note or context lines', () => {
  // GCC pairs a redefinition warning with a "note:" line pointing at the previous definition,
  // and prints "In file included from <our file>" context lines. Neither is a warning we own.
  assert.equal(scanLog('src/main.cpp:42:5: note: this is the location of the previous definition').length, 0);
  assert.equal(scanLog('In file included from src/modules/ble/ble_scanner.cpp:5:').length, 0);
  assert.equal(scanLog('src/modules/ble/ble_scanner.cpp:5:1: error: something broke').length, 0);
});

test('reports every our-src warning in a mixed multi-line log', () => {
  const log = [
    'Compiling .pio/build/cardputer/src/modules/ble/ble_scanner.cpp.o',
    'In file included from src/modules/ble/ble_scanner.cpp:5:',
    NIMCONFIG_WARNING,                                              // vendored -> ignored
    'src/modules/ble/ble_scanner.cpp:30:9: warning: unused variable [-Wunused-variable]', // ours
    TUSB_WARNING,                                                  // vendored -> ignored
    'src/modules/wifi/wifi_scanner.cpp:88: warning: implicit fallthrough', // ours, no col
    'RAM:   [=  ]  10.2% (used 33404 bytes from 327680 bytes)',
  ].join('\n');
  const findings = scanLog(log);
  assert.equal(findings.length, 2);
  assert.deepEqual(findings.map((f) => f.path).sort(), [
    'src/modules/ble/ble_scanner.cpp',
    'src/modules/wifi/wifi_scanner.cpp',
  ]);
});

test('a warning-clean build (only vendored noise) yields no findings', () => {
  const log = [NIMCONFIG_WARNING, TUSB_WARNING, 'cardputer      SUCCESS   00:00:29.600'].join('\n');
  assert.equal(scanLog(log).length, 0);
});

test('isOurSrcPath classifies paths directly', () => {
  assert.equal(isOurSrcPath('src/modules/foo.cpp'), true);
  assert.equal(isOurSrcPath('src/main.cpp'), true);
  assert.equal(isOurSrcPath('src/lgfx/x.cpp'), false);
  assert.equal(isOurSrcPath('src/M5Unified.cpp'), false);
  assert.equal(isOurSrcPath('.pio/libdeps/NimBLE-Arduino/src/nimconfig.h'), false);
  assert.equal(isOurSrcPath('lib/vendor/x.cpp'), false);
  assert.equal(isOurSrcPath('/abs/.platformio/x.h'), false);
});
