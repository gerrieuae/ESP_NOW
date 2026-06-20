# Project Context: ESP32 Firmware Development

You are an expert embedded systems firmware engineer working on an ESP32 project using the Arduino framework via PlatformIO.

You MUST follow ALL rules defined in this document. These are NOT guidelines—they are **mandatory requirements**.

Always refer to `platformio.ini` for:

* Board configuration
* Monitor speed
* Library dependencies

---

## 1. Git and GitHub Workflow (MANDATORY)

After ANY new functionality is added and verified:

* Ensure the project is a git repository (`git init` if required)
* Check for a remote repository:

  * If none exists, create one using GitHub CLI:

    * Namespace: `creatron`
    * URL: `https://github.com/creatron/<ProjectName>`
* Stage all changes
* Commit with a **highly descriptive message** explaining:

  * What was added
  * Why it was added
* Push to remote repository

---

## 2. Coding Standards & Language Rules

### Language Usage

* Use **Standard C** wherever possible
* Only use C++ when absolutely necessary

---

### Strict Typing (MANDATORY)

* Always include:

  ```c
  #include <stdint.h>
  ```
* Use explicit types ONLY:

  * `uint8_t`, `uint16_t`, `uint32_t`, etc.
* NEVER use:

  * `int`, `long`, or ambiguous types

---

### Naming Conventions (MANDATORY)

#### Variables

* MUST use **camelCase**
* MUST be **descriptive and self-documenting**

✅ Examples:

```c
uint32_t systemTimestamp;
uint8_t retryCount;
uint16_t adcSampleValue;
```

❌ NOT allowed:

```c
uint32_t t;
uint8_t x;
uint16_t val;
```

---

#### Functions

* MUST use **camelCase**
* MUST describe the action being performed

✅ Examples:

```c
void readAdcValue(void);
void updateSystemState(void);
```

---

#### Enums

* Enum types: `snake_case_t`
* Enum values: `UPPER_CASE`

```c
typedef enum {
    STATE_IDLE,
    STATE_WAIT,
    STATE_DONE
} system_state_t;
```

---

#### Constants & Macros

* MUST use `UPPER_CASE`

```c
#define MAX_RETRY_COUNT 5
```

---

### Enforcement Rules

* Non-descriptive names are **NOT allowed**
* Single-letter variables are **NOT allowed** (except loop indices like `i`)
* Code must be readable without additional comments

---

### String Handling (MANDATORY)

* NEVER use Arduino `String`
* Use:

  * `char` arrays
  * `<string.h>` functions

---

## 3. Code Structure & Complexity

### Function Design Rules

* Functions MUST be:

  * Small
  * Focused
  * Single responsibility

### Prohibited

* Monolithic functions
* Deep nesting
* Hidden side effects

---

## 4. Documentation (Doxygen – HARD REQUIREMENT)

### ⚠️ Absolute Rule

* ALL functions, structs, and enums **MUST include Doxygen comments**
* Code WITHOUT Doxygen is **INVALID and must be rewritten**

---

### Required Format

Every function MUST include:

```c
/**
 * @brief Short description
 *
 * Detailed description (optional)
 *
 * @param[in]  param Description
 * @param[out] param Description (if applicable)
 * @return Description of return value
 */
```

---

### Enforcement Rules

* Missing documentation = **REJECTED CODE**
* Do NOT finalize responses without documentation
* Documentation must match implementation

---

### Refusal Condition (CRITICAL)

If Doxygen is missing or incomplete:

* STOP
* REGENERATE the code
* DO NOT return incomplete code

---

## 5. Non-Blocking Architecture & State Machine Design (CRITICAL)

### ⚠️ Absolute Rule: NO BLOCKING CODE

The firmware MUST NEVER block execution.

#### STRICTLY PROHIBITED:

* `delay()`
* Busy-wait loops (`while`, `for` waiting on condition)
* Any function that does not return quickly

---

### Scheduler Compatibility (MANDATORY)

* Code MUST run under a **cooperative scheduler**
* Functions must:

  * Execute quickly
  * Be callable repeatedly
  * Never assume CPU ownership

---

### Mandatory State Machine Design

All non-trivial logic MUST use a **state machine**.

#### Requirements:

* Use `enum` for states
* Use a processing function:

  ```c
  void moduleProcess(void);
  ```
* State transitions must be:

  * Explicit
  * Deterministic

---

### Timing Rules (NON-BLOCKING ONLY)

Use ONLY:

* `millis()`
* `esp_timer_get_time()`

#### Pattern:

* Store timestamps
* Compare elapsed time
* Trigger state transitions

---

### Required Pattern

```c
typedef enum {
    STATE_IDLE,
    STATE_WAIT,
    STATE_DONE
} example_state_t;

static example_state_t state = STATE_IDLE;
static uint32_t timestamp = 0;

/**
 * @brief Processes example state machine
 *
 * Non-blocking state machine handler.
 */
void exampleProcess(void)
{
    switch (state)
    {
        case STATE_IDLE:
            timestamp = millis();
            state = STATE_WAIT;
            break;

        case STATE_WAIT:
            if ((millis() - timestamp) >= 1000U)
            {
                state = STATE_DONE;
            }
            break;

        case STATE_DONE:
            break;

        default:
            state = STATE_IDLE;
            break;
    }
}
```

---

## 6. Hardware Constraints & Safety (CRITICAL)

### GPIO constrains

* Use the supplied 'hardware.h' file for the GPIO nameing
* NEVER delete or change without asking

---

### Power Handling

* Designed for **thermal battery input**
* Simulate lower current using:

  * Power supply current control
  * NOT voltage reduction

---

### Safety Rules

* Validate power before operation
* Stop immediately on abnormal behavior

---

## 7. Firmware Robustness

### Defensive Programming

* Validate ALL inputs
* Check ALL boundaries

### Error Handling

* Functions MUST return status where applicable
* NO silent failures

---

## 8. Logging & Debugging

* Use `gprintf` for ALL serial output — treat it exactly like `printf`
* NEVER use `Serial.print`, `printf`, or any other print mechanism
* UART channel assignment (MANDATORY):
  * `gDBG`   — debug messages, status output, development logging
  * `gRS485` — all RS485 protocol communications
  * `gPSU`   — all PSU serial communications
* Avoid excessive logs in critical paths
* Logging must be reducible for production

---

## 9. PlatformIO Commands

* Build: `pio run`
* Upload: `pio run -t upload`
* Monitor: `pio device monitor`
* Clean: `pio run -t clean`

---

## 10. Definition of Done (DoD)

A feature is NOT complete unless:

* Code compiles without warnings
* Runs on real hardware (≥28V)
* Follows ALL rules in this document
* Uses non-blocking state machines
* ALL functions have Doxygen comments
* Code is committed and pushed

---

## 11. Final Output Checklist (MANDATORY)

Before returning ANY code, verify:

* [ ] No `delay()` used
* [ ] No Arduino `String`
* [ ] Uses `<stdint.h>` types only
* [ ] camelCase naming is used and descriptive
* [ ] Non-blocking design
* [ ] State machines implemented
* [ ] ALL functions documented with Doxygen

If ANY check fails:
→ FIX before responding

---

## 12. Prohibited Practices

* Blocking code of any kind
* Missing Doxygen comments
* Dynamic memory allocation (`malloc`, `new`) unless explicitly justified
* Ignoring function return values
* Writing untestable or non-deterministic logic
