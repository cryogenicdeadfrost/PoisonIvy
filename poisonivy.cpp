#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <random>
#include <algorithm>
#include <thread>
#include <chrono>
#include <future>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp> // External library: https://github.com/nlohmann/json
#include <filesystem>

#ifdef USE_CUDA
#include <cuda_runtime.h>
#endif

using namespace std;
using json = nlohmann::json;

//-----------------------------------
// CHAOS-BASED RNG
//-----------------------------------
class ChaosRNG
{
    double x;
    double r;

public:
    ChaosRNG(double seed = 0.654321, double chaos_param = 3.9999) : x(seed), r(chaos_param) {}

    double next()
    {
        x = r * x * (1 - x);
        return x;
    }

    int next_int(int min, int max)
    {
        return min + static_cast<int>(next() * (max - min + 1));
    }

    double next_double(double min, double max)
    {
        return min + next() * (max - min);
    }
};

//-----------------------------------
// PERFORMANCE CONFIGURATION
//-----------------------------------
struct PerformanceConfig
{
    int threads = 1;
    bool simulate_delay = false;
    int delay_ms = 0;
    bool use_gpu = false;

    void load_from_json(const json &profile)
    {
        if (profile.contains("performance"))
        {
            const auto &perf = profile["performance"];
            if (perf.contains("threads"))
                threads = perf["threads"];
            if (perf.contains("simulate_delay"))
                simulate_delay = perf["simulate_delay"];
            if (perf.contains("delay_ms"))
                delay_ms = perf["delay_ms"];
            if (perf.contains("use_gpu"))
                use_gpu = perf["use_gpu"];
        }
    }
};

//-----------------------------------
// CUSTOM ALGORITHM SUPPORT (Windows)
//-----------------------------------
using CustomMutationFunc = vector<string> (*)(const string &, const vector<string> &, const json &);

CustomMutationFunc load_custom_algorithm(const string &dll_path)
{
    HINSTANCE hDll = LoadLibraryA(dll_path.c_str());
    if (!hDll)
    {
        cerr << "Could not load custom DLL: " << dll_path << endl;
        return nullptr;
    }
    return reinterpret_cast<CustomMutationFunc>(GetProcAddress(hDll, "custom_mutate"));
}

//-----------------------------------
// READ HELPERS
//-----------------------------------
vector<string> read_lines(const string &file_path)
{
    ifstream file(file_path);
    vector<string> lines;
    string line;
    while (getline(file, line))
        lines.push_back(line);
    return lines;
}

json read_json(const string &file_path)
{
    ifstream file(file_path);
    json j;
    file >> j;
    return j;
}

//-----------------------------------
// DEFAULT MUTATION LOGIC
//-----------------------------------
vector<string> mutate_line(const string &line, const vector<string> &header_cols, const json &profile, ChaosRNG &rng)
{
    vector<string> mutated_lines;
    istringstream ss(line);
    string token;
    vector<string> cols;
    while (getline(ss, token, ','))
        cols.push_back(token);

    int intensity = profile.contains("anomaly_level") ? profile["anomaly_level"].get<int>() : 5;
    double scale_factor = 1.0 + (intensity / 10.0);

    for (const auto &field : profile["mutate_columns"])
    {
        auto it = find(header_cols.begin(), header_cols.end(), field);
        if (it != header_cols.end())
        {
            int idx = distance(header_cols.begin(), it);
            if (profile["anomaly_boost"].contains(field))
            {
                double factor = profile["anomaly_boost"][field] * scale_factor;
                try
                {
                    cols[idx] = to_string(stod(cols[idx]) * rng.next_double(factor, factor * 2));
                }
                catch (...)
                {
                    cols[idx] = to_string(rng.next_double(1000, 9999));
                }
            }
            else
            {
                cols[idx] = to_string(rng.next_double(1000, 9999));
            }
        }
    }

    cols.back() = profile["label"];

    ostringstream oss;
    for (size_t i = 0; i < cols.size(); ++i)
    {
        oss << cols[i];
        if (i != cols.size() - 1)
            oss << ",";
    }

    mutated_lines.push_back(oss.str());
    return mutated_lines;
}

//-----------------------------------
// THREADING STREAM INJECTION FUNCTION
//-----------------------------------
void stream_inject(const string &main_path,
                   const vector<string> &malicious_data,
                   const vector<string> &header_cols,
                   const json &profile,
                   const string &output_path,
                   const PerformanceConfig &perf,
                   CustomMutationFunc custom_algo = nullptr)
{

    ChaosRNG rng(profile["chaos_seed"], profile["chaos_param"]);
    int inject_count = profile["inject_count"];
    unordered_set<int> inject_positions;
    mutex file_mutex;

    int line_count = 0;
    ifstream infile(main_path);
    ofstream outfile(output_path);
    string line;

    auto process_line = [&](const string &line, int line_number)
    {
        string result;
        if (inject_positions.count(line_number))
        {
            int mode = rng.next_int(0, 2);
            if (mode == 0)
            {
                result = malicious_data[rng.next_int(0, malicious_data.size() - 1)];
            }
            else if (mode == 1)
            {
                vector<string> mutated = custom_algo ? custom_algo(line, header_cols, profile) : mutate_line(line, header_cols, profile, rng);
                result = mutated[0];
            }
            else
            {
                size_t last_comma = line.rfind(',');
                result = (last_comma != string::npos) ? line.substr(0, last_comma) + "," + (string)profile["label"] : line;
            }
        }
        else
        {
            result = line;
        }

        lock_guard<mutex> lock(file_mutex);
        outfile << result << "\n";
    };

    while (getline(infile, line))
    {
        if (line_count == 0)
        {
            outfile << line << "\n";
            ++line_count;
            continue;
        }

        if (inject_positions.size() < inject_count)
        {
            inject_positions.insert(rng.next_int(1, 1000000));
        }

        if (perf.threads > 1)
        {
            async(launch::async, process_line, line, line_count);
        }
        else
        {
            process_line(line, line_count);
        }

        if (perf.simulate_delay && perf.delay_ms > 0)
            this_thread::sleep_for(chrono::milliseconds(perf.delay_ms));

        ++line_count;
    }

    infile.close();
    outfile.close();
}

//-----------------------------------
// MAIN
//-----------------------------------
int main(int argc, char *argv[])
{
    if (argc < 5 || argc > 6)
    {
        cerr << "Usage: " << argv[0] << " <main.csv> <malicious.csv> <output.csv> <profile.json> [custom_alg.dll]\n";
        return 1;
    }

    string main_csv = argv[1];
    string mal_csv = argv[2];
    string out_csv = argv[3];
    string json_config = argv[4];

    vector<string> malicious_data = read_lines(mal_csv);
    json profile = read_json(json_config);

    PerformanceConfig perf;
    perf.load_from_json(profile);

    ifstream test(main_csv);
    string header_line;
    getline(test, header_line);
    test.close();

    vector<string> header_cols;
    istringstream ss(header_line);
    string col;
    while (getline(ss, col, ','))
        header_cols.push_back(col);

    CustomMutationFunc custom_algo = nullptr;
    if (argc == 6)
    {
        custom_algo = load_custom_algorithm(argv[5]);
        if (!custom_algo)
        {
            cerr << "⚠️ Warning: Falling back to built-in mutation logic.\n";
        }
    }

    cout << "🔁 Running in streaming mode with " << perf.threads << " thread(s)...";
    if (perf.use_gpu)
        cout << " (GPU enabled)";
    cout << endl;

    stream_inject(main_csv, malicious_data, header_cols, profile, out_csv, perf, custom_algo);

    cout << "✅ Injection complete. " << profile["inject_count"] << " entries injected with anomaly level "
         << (profile.contains("anomaly_level") ? (int)profile["anomaly_level"] : 5) << ".\nOutput saved to: " << out_csv << endl;
    return 0;
}
