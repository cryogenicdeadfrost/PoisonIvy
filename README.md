# PoisonIvy

PoisonIvy is a lightweight C++ dataset poisoning utility for adversarial ML experiments, IDS benchmarking, and anomaly simulation.

## Overview

PoisonIvy mutates or injects rows in CSV datasets with configurable behavior driven by a JSON profile. It is designed to keep the core flow simple and stream input/output without loading full files into memory.

## Features

- Streaming CSV processing for low memory overhead.
- Chaos-based number generation for non-linear mutation behavior.
- Multi-mode poisoning through `poison_mode`:
  - `inject`: replace selected rows with rows from malicious CSV.
  - `mutate`: mutate configured fields and relabel rows.
  - `flip`: keep values and only flip label.
  - `mix` (default): random blend of inject/mutate/flip per selected row.
- Profile-based performance settings (`threads`, delay simulation, GPU flag for build integrations).
- Optional custom mutation plugin loading (`custom_mutate` symbol from DLL).

## Requirements

- C++17 compiler
- Windows (current implementation uses `LoadLibraryA`)
- `nlohmann/json` single-header library
- Optional CUDA toolkit for builds using `USE_CUDA`

## Build

### MSVC (Windows)

```bash
cl /std:c++17 poisonivy.cpp /Iexternal /Fe:poisonivy.exe
```

### CMake (optional)

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

```bash
./poisonivy <main.csv> <malicious.csv> <output.csv> <profile.json> [custom_lib]
```

Arguments:

- `main.csv`: source dataset (first row must be header).
- `malicious.csv`: candidate rows for direct injection.
- `output.csv`: destination poisoned dataset.
- `profile.json`: poisoning profile.
- `custom.dll`: optional plugin exporting `custom_mutate`.

## Profile example

```json
{
  "inject_count": 200,
  "anomaly_level": 7,
  "label": "malicious",
  "poison_mode": "mix",
  "mutate_columns": ["pkt_size", "duration", "rate"],
  "anomaly_boost": {
    "pkt_size": 1.5,
    "rate": 2.0
  },
  "chaos_seed": 0.789,
  "chaos_param": 3.9999,
  "report_json": "run_report.json",
  "dashboard_html": "run_dashboard.html",
  "open_dashboard": false,
  "performance": {
    "threads": 4,
    "simulate_delay": false,
    "delay_ms": 0,
    "use_gpu": false
  }
}
```

## Dashboard output

The HTML dashboard contains:

- run summary (mode, selected rows, output rows)
- mutation mix bar with inject/mutate/flip counts

This is intended as a fast local GUI artifact for production runs and regression checks.

## Plugin contract

```cpp
extern "C"
std::vector<std::string> custom_mutate(
    const std::string& line,
    const std::vector<std::string>& headers,
    const nlohmann::json& profile);
```

If plugin load fails, PoisonIvy falls back to built-in mutation.
## Custom mutation plugin contract

```cpp
extern "C" __declspec(dllexport)
std::vector<std::string> custom_mutate(
    const std::string& line,
    const std::vector<std::string>& headers,
    const nlohmann::json& profile);
```

PoisonIvy expects at least one output row and uses index `0`.

## Notes

- Injection positions are selected within the available data line range.
- Header row is always preserved.
- If plugin loading fails, PoisonIvy automatically falls back to built-in mutation logic.
