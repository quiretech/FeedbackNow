# Production Readiness Report - FlexBox v1.2

**Date:** 2026-02-17  
**Status:** ⚠️ **CRITICAL ISSUES FOUND** - Must fix before production

---

## 🔴 CRITICAL ISSUES (Must Fix)

### 1. **Uninitialized Mutexes in `smf_system_mode.c`**
**Location:** Lines 53, 58  
**Severity:** CRITICAL - Undefined behavior, potential deadlock/crash

**Problem:**
```c
static struct k_mutex smf_dl_mutex;  // ❌ Never initialized
static struct k_mutex smf_nfc_mutex; // ❌ Never initialized
```

These mutexes are used in `smf_post_downlink()`, `smf_post_nfc_result()`, and the SMF handler, but are never initialized. In Zephyr, static mutexes must be initialized with `k_mutex_init()` or `K_MUTEX_DEFINE()`.

**Impact:** Undefined behavior on first lock attempt. May cause:
- Immediate crash/hang
- Silent corruption
- Deadlock

**Fix:**
```c
// Option 1: Initialize in smf_thread_fn before use
static void smf_thread_fn(void *a, void *b, void *c) {
  k_mutex_init(&smf_dl_mutex);
  k_mutex_init(&smf_nfc_mutex);
  // ... rest of function
}

// Option 2: Use K_MUTEX_DEFINE (preferred)
K_MUTEX_DEFINE(smf_dl_mutex);
K_MUTEX_DEFINE(smf_nfc_mutex);
```

---

### 2. **Double Release of 3.3A Rail**
**Location:** `smf_system_mode.c` lines 541-542, 562-563  
**Severity:** CRITICAL - Power rail ref-count corruption

**Problem:**
```c
// MODE_NFC_SCAN timeout handler:
rail_manager_release_3v6();
rail_manager_release_3v3a(); /* NFC ref */
rail_manager_release_3v3a(); /* Staff ref (we entered NFC from Staff) */ // ❌ DOUBLE RELEASE

// MODE_NFC_SCAN result handler:
rail_manager_release_3v6();
rail_manager_release_3v3a(); /* NFC ref */
rail_manager_release_3v3a(); /* Staff ref (we entered NFC from Staff) */ // ❌ DOUBLE RELEASE
```

**Root Cause:** When entering `MODE_NFC_SCAN` from `MODE_STAFF`:
- Line 508: `rail_manager_request_3v3a()` (Staff already held it, so ref=2)
- Line 509: `rail_manager_request_3v6()` (NFC needs 3.6V)
- But Staff mode **already released** 3.3A when transitioning to NFC (line 485)

**Impact:**
- Ref-count goes negative → rail powers off unexpectedly
- Subsequent requests may fail
- EEPROM flush/NFC operations may fail mid-operation

**Fix:**
Remove the duplicate release. The Staff mode already released 3.3A when entering NFC (line 485), so only release once:
```c
rail_manager_release_3v6();
rail_manager_release_3v3a(); /* NFC ref only */
// Remove: rail_manager_release_3v3a(); /* Staff ref */
```

---

### 3. **Race Condition in `nfc_service.c`**
**Location:** Lines 31-33, 54, 76, 139  
**Severity:** CRITICAL - Data corruption, undefined behavior

**Problem:**
```c
static volatile uint8_t scan_intent;      // ❌ Volatile but not atomic
static volatile uint8_t scan_button_id;   // ❌ Volatile but not atomic
static volatile bool cancel_requested;     // ❌ Volatile but not atomic

// Accessed from multiple threads:
// - nfc_worker_thread (reads scan_intent, scan_button_id, cancel_requested)
// - nfc_scan_start (writes scan_intent, scan_button_id)
// - nfc_scan_cancel (writes cancel_requested)
```

**Impact:**
- Torn reads/writes (uint8_t is usually safe, but bool can be problematic)
- `cancel_requested` may be missed if written during read
- No memory barriers → CPU reordering issues

**Fix:**
Use atomic operations or proper mutex protection:
```c
#include <zephyr/sys/atomic.h>

static atomic_t scan_intent_atomic = ATOMIC_INIT(0);
static atomic_t scan_button_id_atomic = ATOMIC_INIT(0);
static atomic_t cancel_requested_atomic = ATOMIC_INIT(0);

// In nfc_scan_start:
atomic_set(&scan_intent_atomic, intent);
atomic_set(&scan_button_id_atomic, button_id);

// In nfc_scan_cancel:
atomic_set(&cancel_requested_atomic, 1);

// In worker thread:
if (atomic_get(&cancel_requested_atomic)) { ... }
```

---

### 4. **RTC Init Failure Causes Reboot Loop**
**Location:** `main.c` lines 99-104  
**Severity:** CRITICAL - Production device will reboot loop if RTC fails

**Problem:**
```c
ret = rtc_app_init();
if (ret != 0) {
  LOG_WRN("RTC init not available (%d); button timestamps may fall back", ret);
  sys_reboot(SYS_REBOOT_COLD); // ❌ REBOOTS ON RTC FAILURE
}
```

**Impact:**
- If RTC hardware fails or I2C bus issue → infinite reboot loop
- Device becomes completely unusable
- No recovery path

**Fix:**
Allow system to continue with degraded functionality:
```c
ret = rtc_app_init();
if (ret != 0) {
  LOG_WRN("RTC init not available (%d); button timestamps will use uptime", ret);
  // Continue without RTC - system can still function
}
```

---

## 🟡 HIGH PRIORITY ISSUES

### 5. **Production Config: `LORA_JOIN_BACKOFF_HOURS`**
**Location:** `sys_config.h` line 37  
**Severity:** HIGH - Non-standard production value

**Problem:**
```c
#define LORA_JOIN_BACKOFF_HOURS 6 // changed to 6
// Comment says: "prod: 24"
```

**Impact:** Devices will retry join every 6 hours instead of 24 hours, increasing:
- Battery drain
- Network congestion
- Unnecessary gateway load

**Fix:**
```c
#define LORA_JOIN_BACKOFF_HOURS 24 // prod: 24
```

---

### 6. **Missing Error Handling in Rail Manager**
**Location:** `rail_manager.c` lines 76-97  
**Severity:** HIGH - Silent failures

**Problem:**
```c
void rail_manager_release_3v3(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v3 > 0) {
    ref_3v3--;  // ❌ No check if ref goes negative
  }
  // ...
}
```

**Impact:** If double-release occurs (see issue #2), ref-count can go negative silently.

**Fix:** Add assertions or bounds checking:
```c
if (ref_3v3 > 0) {
  ref_3v3--;
} else {
  LOG_ERR("rail_manager_release_3v3: ref already 0!");
  // Optionally: set to 0 to prevent negative
  ref_3v3 = 0;
}
```

---

### 7. **Stack Overflow Risk in SMF Thread**
**Location:** `smf_system_mode.c` line 617, `sys_config.h` line 243  
**Severity:** HIGH - Potential stack overflow

**Problem:**
- `SMF_THREAD_STACK_SIZE = 1536` bytes
- Recent addition: drain-wait loop (lines 351-355) adds stack frames
- Counter sync loop (lines 185-204) with payload buffers
- Multiple nested function calls

**Impact:** Stack overflow → undefined behavior, crash

**Recommendation:**
- Verify with `CONFIG_STACK_SENTINEL=y` and stack usage reports
- Consider increasing to 2048 bytes if needed

---

## 🟢 MEDIUM PRIORITY ISSUES

### 8. **Missing Input Validation in Downlink Handler**
**Location:** `smf_system_mode.c` lines 209-297  
**Severity:** MEDIUM - Potential buffer overflow

**Problem:**
```c
static void smf_handle_downlink(uint8_t port, uint8_t len, const uint8_t *data) {
  // ...
  uint8_t cmd = data[0]; // ❌ No bounds check if len == 0
  // ...
  case DL_CMD_EPD_UPDATE:
    if (len >= 5) {
      uint32_t epoch = ((uint32_t)data[1] << 24) | ...; // ❌ No bounds check
```

**Impact:** If `len` is incorrect, may read out-of-bounds.

**Fix:** Already has `if (len == 0 || data == NULL)` check at start, but add explicit bounds checks for each command.

---

### 9. **EEPROM Flush Failure Recovery**
**Location:** `button_counter_store.c` lines 196-232  
**Severity:** MEDIUM - Data loss risk

**Problem:**
```c
if (ret != 0) {
  LOG_ERR("EEPROM counter flush failed (slot=%u off=0x%x): %d", ...);
  /* Keep dirty=true so we retry on next schedule */
  k_mutex_unlock(&ctx.lock);
  return; // ❌ No retry limit, no alert
}
```

**Impact:** If EEPROM fails permanently, counters are lost on power cycle.

**Recommendation:** Add retry counter and alert mechanism (e.g., LED pattern or uplink).

---

### 10. **DeviceInfo Timeout vs Display Delay Mismatch**
**Location:** `sys_config.h` lines 82, 232  
**Severity:** MEDIUM - UX issue

**Problem:**
- `DEVICE_INFO_TIMEOUT_MS = 4000` (4 seconds)
- `DISPLAY_WORK_DELAY_MS = 5000` (5 seconds)

Timeout fires **before** DeviceInfo screen renders, causing confusing UX.

**Recommendation:** Increase `DEVICE_INFO_TIMEOUT_MS` to 15000 (15 seconds) for better UX.

---

## ✅ POSITIVES

1. **Good error handling** in most critical paths (LoRa join, EEPROM init)
2. **Proper mutex usage** in rail_manager, button_counter_store, devnonce_store
3. **Atomic operations** used correctly for `lora_joined_flag` and `time_sync_inflight`
4. **Power management** is well-designed with ref-counting
5. **EPD cold start recovery** now handles power-cycling correctly

---

## 📋 FIX CHECKLIST

- [ ] **CRITICAL:** Initialize `smf_dl_mutex` and `smf_nfc_mutex`
- [ ] **CRITICAL:** Fix double release of 3.3A rail in NFC handlers
- [ ] **CRITICAL:** Convert volatile variables to atomic in `nfc_service.c`
- [ ] **CRITICAL:** Remove reboot on RTC init failure
- [ ] **HIGH:** Set `LORA_JOIN_BACKOFF_HOURS` to 24 for production
- [ ] **HIGH:** Add bounds checking in rail_manager release functions
- [ ] **HIGH:** Verify SMF thread stack size with stack usage reports
- [ ] **MEDIUM:** Add retry limit for EEPROM flush failures
- [ ] **MEDIUM:** Increase `DEVICE_INFO_TIMEOUT_MS` to 15000

---

## 🧪 TESTING RECOMMENDATIONS

1. **Stress test:** Rapid button presses, NFC scans, mode transitions
2. **Power cycling:** Test EPD recovery after multiple power cycles
3. **RTC failure:** Test with RTC disconnected (should not reboot loop)
4. **Stack usage:** Enable `CONFIG_STACK_SENTINEL=y` and monitor
5. **Long-term:** 24+ hour test with periodic joins/heartbeats

---

**Report Generated:** 2026-02-17  
**Next Review:** After critical fixes applied
