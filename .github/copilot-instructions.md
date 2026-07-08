# Copilot instructions

## Project overview

This project reads Itron/Actaris EverBlu Cyble water/gas meters via a CC1101 433 MHz radio transceiver on an ESP8266 or ESP32, and publishes readings to Home Assistant (via MQTT or ESPHome). It supports two independent deployment targets that share the same core radio/protocol logic.

## Repository layout

| Path                                | Purpose                                                                                                                                     |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/`                              | PlatformIO/Arduino firmware (standalone deployment via MQTT)                                                                                |
| `src/core/`                         | CC1101 driver, utils, logging, version - shared low-level code                                                                              |
| `src/services/`                     | Business logic: meter reading, scheduling, history, frequency calibration                                                                   |
| `src/adapters/`                     | Interfaces (`config_provider.h`, `data_publisher.h`, `time_provider.h`) and their concrete implementations (MQTT, ESPHome, NTP)             |
| `ESPHOME/components/everblu_meter/` | **Source** for the ESPHome external component (C++ + Python)                                                                                |
| `ESPHOME/`                          | ESPHome YAML examples and the release-preparation scripts                                                                                   |
| `ESPHOME-release/`                  | **Generated output - do not edit.** Produced by `ESPHOME/prepare-component-release.ps1` / `.sh`. Contents are overwritten on every release. |
| `include/private.h`                 | Device-specific credentials (Wi-Fi, MQTT, meter serial). Not committed; `private.example.h` is the template.                                |
| `test/`                             | PlatformIO unit tests (native environment)                                                                                                  |
| `docs/`                             | Design notes, datasheets, and historical analysis. Not shipped.                                                                             |

## Key architectural decisions

- **Adapter pattern**: `src/adapters/` abstracts platform differences so the same service layer compiles for both MQTT-standalone and ESPHome builds.
- **ESPHome component** (`ESPHOME/components/everblu_meter/`) wraps the same CC1101 + protocol logic in an ESPHome `Component`/`Sensor` hierarchy; it does **not** duplicate `src/`.
- **Frequency calibration**: The `FrequencyManager` service tunes the CC1101 carrier frequency at runtime to compensate for crystal drift.
- **Schedule manager**: Meters broadcast on a fixed daily schedule; `ScheduleManager` aligns wakeups to that window.

## Review guidance

- `ESPHOME-release/` is generated - never suggest changes there; raise them against the source in `ESPHOME/components/everblu_meter/` instead.
- `include/private.h` is gitignored and contains real credentials; do not flag its absence as an error.
- The project targets both ESP8266 (Arduino framework, no `std::` threading) and ESP32 + ESPHome; avoid suggestions that break 8266 compatibility unless the change is explicitly scoped to ESP32.
- PlatformIO environments are defined in `platformio.ini`; the default build target is `huzzah` (Adafruit HUZZAH ESP8266).

## Generated output policy

- Treat `ESPHOME-release/` as read-only generated output.
- Never edit files in `ESPHOME-release/` directly, even if a user asks for behavior changes.
- Make changes only in `ESPHOME/components/everblu_meter/` (and related source), then regenerate `ESPHOME-release/` via `ESPHOME/prepare-component-release.ps1` or `.sh`.

## ESPHome component development

When working on `ESPHOME/components/everblu_meter/`, follow the upstream ESPHome conventions documented in the [ESPHome AI Collaboration Guide (AGENTS.md)](https://github.com/esphome/esphome/blob/dev/AGENTS.md). It covers C++ naming and style, Python schema patterns, component structure, automation helpers, heap allocation guidelines, and the PR contribution workflow.

## General coding-behavior guidelines

See [SKILL.md](../SKILL.md) for general LLM coding-behavior guidelines (surgical changes, simplicity-first, verify-before-done).

## Writing style (docs and comments)

- Use UK English spelling and conventions (for example: colour, behaviour, optimise, centre).
- Do not use em dashes (—). Rewrite the sentence, or use a colon, comma, parentheses, or separate sentences instead.
- Avoid patterns that read as AI-generated text. See [Wikipedia: Signs of AI writing](https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing) for the tells to avoid (for example: puffery, hollow "not only... but also" constructions, overuse of "moreover"/"furthermore", vague attributions, and needless summary sections).
- Prefer plain, direct language that matches the surrounding text.
