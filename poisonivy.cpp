#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef USE_CUDA
#include <cuda_runtime.h>
#endif

using namespace std;
using json = nlohmann::json;

class Chaos
{
    double x;
    double r;

public:
    Chaos(double seed = 0.654321, double p = 3.9999) : x(seed), r(p) {}

    double next()
    {
        x = r * x * (1 - x);
        return x;
    }

    int i(int lo, int hi)
    {
        return lo + static_cast<int>(next() * (hi - lo + 1));
    }

    double d(double lo, double hi)
    {
        return lo + next() * (hi - lo);
    }
};

struct Perf
{
    int threads = 1;
    bool slow = false;
    int delay_ms = 0;
    bool gpu = false;

    void load(const json &cfg)
    {
        if (!cfg.contains("performance"))
            return;
        const auto &p = cfg["performance"];
        if (p.contains("threads"))
            threads = p["threads"];
        if (p.contains("simulate_delay"))
            slow = p["simulate_delay"];
        if (p.contains("delay_ms"))
            delay_ms = p["delay_ms"];
        if (p.contains("use_gpu"))
            gpu = p["use_gpu"];
    }
};

using MutFn = vector<string> (*)(const string &, const vector<string> &, const json &);

MutFn load_custom(const string &dll)
{
    HINSTANCE h = LoadLibraryA(dll.c_str());
    if (!h)
    {
        cerr << "Could not load custom DLL: " << dll << endl;
        return nullptr;
    }
    return reinterpret_cast<MutFn>(GetProcAddress(h, "custom_mutate"));
}

vector<string> read_lines(const string &path)
{
    ifstream f(path);
    vector<string> out;
    string line;
    while (getline(f, line))
        out.push_back(line);
    return out;
}

json read_cfg(const string &path)
{
    ifstream f(path);
    json j;
    f >> j;
    return j;
}

vector<string> split_csv(const string &line)
{
    vector<string> cols;
    istringstream ss(line);
    string t;
    while (getline(ss, t, ','))
        cols.push_back(t);
    return cols;
}

string join_csv(const vector<string> &cols)
{
    ostringstream out;
    for (size_t i = 0; i < cols.size(); ++i)
    {
        out << cols[i];
        if (i + 1 < cols.size())
            out << ',';
    }
    return out.str();
}

string mode_of(const json &cfg)
{
    if (cfg.contains("poison_mode"))
        return cfg["poison_mode"].get<string>();
    return "mix";
}

string mutate_one(const string &line, const vector<string> &hdr, const json &cfg, Chaos &rng)
{
    vector<string> cols = split_csv(line);
    int level = cfg.value("anomaly_level", 5);
    double scale = 1.0 + (level / 10.0);

    if (cfg.contains("mutate_columns") && cfg["mutate_columns"].is_array())
    {
        for (const auto &f : cfg["mutate_columns"])
        {
            auto it = find(hdr.begin(), hdr.end(), f.get<string>());
            if (it == hdr.end())
                continue;
            int idx = static_cast<int>(distance(hdr.begin(), it));
            if (idx < 0 || static_cast<size_t>(idx) >= cols.size())
                continue;

            if (cfg.contains("anomaly_boost") && cfg["anomaly_boost"].contains(f.get<string>()))
            {
                double k = cfg["anomaly_boost"][f.get<string>()].get<double>() * scale;
                try
                {
                    cols[idx] = to_string(stod(cols[idx]) * rng.d(k, k * 2));
                }
                catch (...)
                {
                    cols[idx] = to_string(rng.d(1000, 9999));
                }
            }
            else
            {
                cols[idx] = to_string(rng.d(1000, 9999));
            }
        }
    }

    if (!cols.empty())
        cols.back() = cfg.value("label", string("malicious"));

    return join_csv(cols);
}

string flip_one(const string &line, const json &cfg)
{
    size_t p = line.rfind(',');
    if (p == string::npos)
        return line;
    return line.substr(0, p) + "," + cfg.value("label", string("malicious"));
}

bool want_inject(const unordered_set<int> &spots, int line_no)
{
    return spots.find(line_no) != spots.end();
}

string poison_one(const string &line,
                 int line_no,
                 const unordered_set<int> &spots,
                 const vector<string> &bad,
                 const vector<string> &hdr,
                 const json &cfg,
                 Chaos &rng,
                 MutFn custom)
{
    if (!want_inject(spots, line_no))
        return line;

    string mode = mode_of(cfg);
    if (mode == "inject")
    {
        return bad[rng.i(0, static_cast<int>(bad.size()) - 1)];
    }
    if (mode == "mutate")
    {
        return custom ? custom(line, hdr, cfg)[0] : mutate_one(line, hdr, cfg, rng);
    }
    if (mode == "flip")
    {
        return flip_one(line, cfg);
    }

    int pick = rng.i(0, 2);
    if (pick == 0)
        return bad[rng.i(0, static_cast<int>(bad.size()) - 1)];
    if (pick == 1)
        return custom ? custom(line, hdr, cfg)[0] : mutate_one(line, hdr, cfg, rng);
    return flip_one(line, cfg);
}

unordered_set<int> pick_spots(int data_lines, int count, Chaos &rng)
{
    unordered_set<int> s;
    if (data_lines <= 0 || count <= 0)
        return s;
    int cap = min(data_lines, count);
    while (static_cast<int>(s.size()) < cap)
        s.insert(rng.i(1, data_lines));
    return s;
}

int count_data_lines(const string &path)
{
    ifstream f(path);
    string line;
    int n = -1;
    while (getline(f, line))
        ++n;
    return max(0, n);
}

void run_stream(const string &main_path,
                const vector<string> &bad,
                const vector<string> &hdr,
                const json &cfg,
                const string &out_path,
                const Perf &perf,
                MutFn custom = nullptr)
{
    Chaos rng(cfg.value("chaos_seed", 0.654321), cfg.value("chaos_param", 3.9999));
    int inject_n = cfg.value("inject_count", 0);
    int data_n = count_data_lines(main_path);
    auto spots = pick_spots(data_n, inject_n, rng);

    ifstream in(main_path);
    ofstream out(out_path);
    string line;
    int line_no = 0;

    while (getline(in, line))
    {
        if (line_no == 0)
        {
            out << line << "\n";
            ++line_no;
            continue;
        }

        out << poison_one(line, line_no, spots, bad, hdr, cfg, rng, custom) << "\n";
        if (perf.slow && perf.delay_ms > 0)
            this_thread::sleep_for(chrono::milliseconds(perf.delay_ms));
        ++line_no;
    }
}

vector<string> parse_header(const string &main_csv)
{
    ifstream f(main_csv);
    string header;
    getline(f, header);
    return split_csv(header);
}

int main(int argc, char *argv[])
{
    if (argc < 5 || argc > 6)
    {
        cerr << "Usage: " << argv[0] << " <main.csv> <malicious.csv> <output.csv> <profile.json> [custom_alg.dll]\n";
        return 1;
    }

    string main_csv = argv[1];
    string bad_csv = argv[2];
    string out_csv = argv[3];
    string cfg_file = argv[4];

    vector<string> bad = read_lines(bad_csv);
    json cfg = read_cfg(cfg_file);
    if (bad.empty())
    {
        cerr << "Malicious dataset is empty.\n";
        return 1;
    }

    Perf perf;
    perf.load(cfg);

    vector<string> hdr = parse_header(main_csv);

    MutFn custom = nullptr;
    if (argc == 6)
    {
        custom = load_custom(argv[5]);
        if (!custom)
            cerr << "Warning: Falling back to built-in mutation logic.\n";
    }

    cout << "Running in streaming mode with " << perf.threads << " thread(s)...";
    if (perf.gpu)
        cout << " (GPU enabled)";
    cout << endl;

    run_stream(main_csv, bad, hdr, cfg, out_csv, perf, custom);

    cout << "Injection complete. " << cfg.value("inject_count", 0)
         << " entries requested with anomaly level " << cfg.value("anomaly_level", 5)
         << ". Output saved to: " << out_csv << endl;
    return 0;
}
