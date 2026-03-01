# PoisonIvy

PoisonIvy is a C++ dataset poisoning tool for adversarial ML experiments, IDS benchmarking, and anomaly simulation.

## Core capabilities

- Streaming CSV poisoning path with low memory pressure.
- Chaos-based random source for mutation and mode sampling.
- Multi-mode poisoning with `poison_mode`:
  - `inject`
  - `mutate`
  - `flip`
  - `mix` (automatic blend)
- Optional custom mutation plugin (`custom_mutate`).
- Validation for key profile fields before execution.
- Run reports:
  - JSON report (`report_json`)
  - HTML dashboard (`dashboard_html`) for GUI-style run inspection
  - Optional auto-open (`open_dashboard`)

## Build

### Linux / macOS

```bash
g++ -std=c++17 poisonivy.cpp -I. -ldl -o poisonivy
```

### Windows (MSVC)

```bash
cl /std:c++17 poisonivy.cpp /Iexternal /Fe:poisonivy.exe
```

## Usage

```bash
./poisonivy <main.csv> <malicious.csv> <output.csv> <profile.json> [custom_lib]
```

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
