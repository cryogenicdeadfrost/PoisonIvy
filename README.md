# PoisonIvy
PoisonIvy is a modular, chaos-driven dataset poisoning engine built for machine learning research and anomaly detection testing. Designed with scalability and configurability in mind, it allows researchers and engineers to simulate realistic, randomized, and algorithmically-mutated attack patterns within structured datasets like CSV, JSON, and more

# PoisonIvy

> A chaotic, high-performance dataset poisoning engine for adversarial ML, anomaly detection testing, and synthetic dataset generation.

![PoisonIvy Logo](https://img.shields.io/badge/status-active-brightgreen?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)

---

## 🔥 Overview

**PoisonIvy** is a powerful, modular engine built in modern C++ that enables chaotic, randomized poisoning of structured datasets (CSV, JSON, and more). Designed for **red teaming, IDS benchmarking**, and **adversarial machine learning**, PoisonIvy allows you to simulate malicious data injection, mutate values unpredictably, and customize how anomalies are introduced.

---

## ✨ Features

* 🎚 **Anomaly Intensity Control**: Set the level of mutation from 1 (subtle) to 10 (chaotic).
* 🔄 **Streaming Mode**: Handles massive files line-by-line with minimal memory.
* 🎲 **Chaos-Based RNG**: Replaces PRNG with a nonlinear chaotic system for unpredictable mutations.
* 🧵 **Multithreaded Execution**: Parallel mutation with configurable thread counts.
* 🚀 **GPU Support**: Optional CUDA acceleration for large-scale operations.
* 🧠 **Custom Algorithm Support**: Inject custom mutation logic via DLLs or SOs.
* ⚙️ **Performance Profiles**: Configure execution behavior via a single JSON file.
* 💥 **Multi-Mode Poisoning**: Choose between raw injection, field mutation, or label flipping.
* 📁 **Cross-Format Future Support**: (Coming soon) JSON, Parquet, and binary support.

---

## 📦 Requirements

* **C++17** or higher compiler
* **Windows 10+** (or Linux with slight modifications)
* [nlohmann/json](https://github.com/nlohmann/json) header
* Optional: **CUDA Toolkit** (if `USE_CUDA` is defined)
* Optional: DLL/SO with `custom_mutate` symbol for custom logic

---

## 🛠 Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/yourname/PoisonIvy.git
   cd PoisonIvy
   ```

2. Get the JSON library:

   ```bash
   mkdir -p external/nlohmann
   curl -o external/nlohmann/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
   ```

3. Build the project (Windows - MSVC):

   ```bash
   cl /std:c++17 poisonivy.cpp /Iexternal /Fe:poisonivy.exe
   ```

4. Or build with CMake:

   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```

---

## 🚀 Usage

```bash
poisonivy.exe <main.csv> <malicious.csv> <output.csv> <profile.json> [custom.dll]
```

### Arguments:

* `main.csv`: Original benign dataset
* `malicious.csv`: Pre-crafted malicious entries
* `output.csv`: File to store poisoned dataset
* `profile.json`: Configuration file with mutation logic and parameters
* `custom.dll` *(optional)*: Your own algorithm compiled as DLL/SO

### Sample `profile.json`

```json
{
  "inject_count": 200,
  "anomaly_level": 7,
  "label": "malicious",
  "mutate_columns": ["pkt_size", "duration", "rate"],
  "anomaly_boost": {
    "pkt_size": 1.5,
    "rate": 2.0
  },
  "chaos_seed": 0.789,
  "chaos_param": 3.9999,
  "performance": {
    "threads": 4,
    "simulate_delay": false,
    "use_gpu": false
  }
}
```

---

## 📚 Use Cases

* ✅ Simulate attack traffic for **IDS testing**
* ✅ Stress-test ML classifiers for **robustness evaluation**
* ✅ Generate adversarial samples for **poisoning ML pipelines**
* ✅ Create synthetic anomalous datasets for **research papers**

---

## 🧩 Extending PoisonIvy

Want more chaos? Add your own randomization algorithm by:

1. Creating a new DLL/SO:

   ```cpp
   extern "C" __declspec(dllexport)
   std::vector<std::string> custom_mutate(const std::string &line, const std::vector<std::string> &headers, const nlohmann::json &profile) {
       // Your custom mutation logic here
   }
   ```

2. Compile it and provide the `.dll` path as a 5th argument.

---

## 📌 Future Roadmap

* [ ] GUI via [Clay](https://www.nicbarker.com/clay) or ImGui
* [ ] Native Linux/macOS support with `dlopen`
* [ ] Real-time metrics visualization
* [ ] JSON and Parquet format handling
* [ ] Chaos-mode adversarial validator


