#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include "json.hpp"
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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
        if (hi <= lo)
            return lo;
        return lo + static_cast<int>(next() * (hi - lo + 1));
    }

    double d(double lo, double hi)
    {
        if (hi <= lo)
            return lo;
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
            threads = max(1, p["threads"].get<int>());
        if (p.contains("simulate_delay"))
            slow = p["simulate_delay"];
        if (p.contains("delay_ms"))
            delay_ms = max(0, p["delay_ms"].get<int>());
        if (p.contains("use_gpu"))
            gpu = p["use_gpu"];
    }
};

struct RunStats
{
    int data_lines = 0;
    int selected_lines = 0;
    int out_rows = 0;
    int inject_hits = 0;
    int mutate_hits = 0;
    int flip_hits = 0;
    string mode = "mix";
    bool custom_loaded = false;
};

using MutFn = vector<string> (*)(const string &, const vector<string> &, const json &);

struct DynLib
{
#ifdef _WIN32
    HINSTANCE h = nullptr;
#else
    void *h = nullptr;
#endif
};

DynLib g_lib;

MutFn load_custom(const string &lib_path)
{
#ifdef _WIN32
    g_lib.h = LoadLibraryA(lib_path.c_str());
    if (!g_lib.h)
        return nullptr;
    return reinterpret_cast<MutFn>(GetProcAddress(g_lib.h, "custom_mutate"));
#else
    g_lib.h = dlopen(lib_path.c_str(), RTLD_LAZY);
    if (!g_lib.h)
        return nullptr;
    return reinterpret_cast<MutFn>(dlsym(g_lib.h, "custom_mutate"));
#endif
}

void close_custom()
{
#ifdef _WIN32
    if (g_lib.h)
        FreeLibrary(g_lib.h);
#else
    if (g_lib.h)
        dlclose(g_lib.h);
#endif
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

vector<string> read_data_lines(const string &path)
{
    vector<string> rows = read_lines(path);
    if (!rows.empty())
        rows.erase(rows.begin());
    return rows;
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
    string m = cfg.value("poison_mode", string("mix"));
    if (m == "inject" || m == "mutate" || m == "flip" || m == "mix")
        return m;
    return "mix";
}

bool cfg_ok(const json &cfg, string &err)
{
    if (!cfg.contains("inject_count") || !cfg["inject_count"].is_number_integer())
    {
        err = "profile.inject_count must be integer";
        return false;
    }
    if (cfg["inject_count"].get<int>() < 0)
    {
        err = "profile.inject_count must be >= 0";
        return false;
    }
    if (cfg.contains("anomaly_level"))
    {
        int lv = cfg["anomaly_level"].get<int>();
        if (lv < 1 || lv > 10)
        {
            err = "profile.anomaly_level must be between 1 and 10";
            return false;
        }
    }
    if (cfg.contains("mutate_columns") && !cfg["mutate_columns"].is_array())
    {
        err = "profile.mutate_columns must be array";
        return false;
    }
    return true;
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
            string key = f.get<string>();
            auto it = find(hdr.begin(), hdr.end(), key);
            if (it == hdr.end())
                continue;
            int idx = static_cast<int>(distance(hdr.begin(), it));
            if (idx < 0 || static_cast<size_t>(idx) >= cols.size())
                continue;

            if (cfg.contains("anomaly_boost") && cfg["anomaly_boost"].contains(key))
            {
                double k = cfg["anomaly_boost"][key].get<double>() * scale;
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

int count_data_lines(const string &path)
{
    ifstream f(path);
    string line;
    int n = -1;
    while (getline(f, line))
        ++n;
    return max(0, n);
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

string poison_one(const string &line,
                 int line_no,
                 const unordered_set<int> &spots,
                 const vector<string> &bad,
                 const vector<string> &hdr,
                 const json &cfg,
                 Chaos &rng,
                 MutFn custom,
                 RunStats &stats)
{
    if (spots.find(line_no) == spots.end())
        return line;

    string mode = stats.mode;
    string chosen = mode;
    if (mode == "mix")
    {
        int pick = rng.i(0, 2);
        chosen = (pick == 0 ? "inject" : pick == 1 ? "mutate"
                                                   : "flip");
    }

    if (chosen == "inject")
    {
        ++stats.inject_hits;
        return bad[rng.i(0, static_cast<int>(bad.size()) - 1)];
    }
    if (chosen == "mutate")
    {
        ++stats.mutate_hits;
        if (custom)
        {
            vector<string> r = custom(line, hdr, cfg);
            if (!r.empty())
                return r[0];
        }
        return mutate_one(line, hdr, cfg, rng);
    }

    ++stats.flip_hits;
    return flip_one(line, cfg);
}

RunStats run_stream(const string &main_path,
                    const vector<string> &bad,
                    const vector<string> &hdr,
                    const json &cfg,
                    const string &out_path,
                    const Perf &perf,
                    MutFn custom = nullptr)
{
    RunStats stats;
    stats.mode = mode_of(cfg);
    stats.custom_loaded = (custom != nullptr);

    Chaos rng(cfg.value("chaos_seed", 0.654321), cfg.value("chaos_param", 3.9999));
    int inject_n = cfg.value("inject_count", 0);
    int data_n = count_data_lines(main_path);
    auto spots = pick_spots(data_n, inject_n, rng);

    stats.data_lines = data_n;
    stats.selected_lines = static_cast<int>(spots.size());

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

        string p = poison_one(line, line_no, spots, bad, hdr, cfg, rng, custom, stats);
        out << p << "\n";
        ++stats.out_rows;

        if (perf.slow && perf.delay_ms > 0)
            this_thread::sleep_for(chrono::milliseconds(perf.delay_ms));
        ++line_no;
    }

    return stats;
}

vector<string> parse_header(const string &main_csv)
{
    ifstream f(main_csv);
    string header;
    getline(f, header);
    return split_csv(header);
}

void write_json_report(const string &path, const RunStats &stats, const json &cfg, const Perf &perf)
{
    if (path.empty())
        return;
    ofstream out(path);
    json rep = {
        {"mode", stats.mode},
        {"data_lines", stats.data_lines},
        {"selected_lines", stats.selected_lines},
        {"output_rows", stats.out_rows},
        {"inject_hits", stats.inject_hits},
        {"mutate_hits", stats.mutate_hits},
        {"flip_hits", stats.flip_hits},
        {"custom_loaded", stats.custom_loaded},
        {"threads", perf.threads},
        {"gpu", perf.gpu},
        {"inject_count", cfg.value("inject_count", 0)}};
    out << rep.dump(2) << "\n";
}

void write_html_dashboard(const string &path, const RunStats &stats)
{
    if (path.empty())
        return;
    ofstream out(path);
    out << "<!doctype html><html><head><meta charset='utf-8'><title>PoisonIvy Dashboard</title>"
        << "<style>body{font-family:Arial;background:#0f172a;color:#e2e8f0;margin:24px}.card{background:#1e293b;padding:16px;border-radius:10px;margin-bottom:12px}"
        << ".bar{height:20px;background:#334155;border-radius:6px;overflow:hidden}.fill{height:100%}.i{background:#ef4444}.m{background:#f59e0b}.f{background:#22c55e}</style></head><body>";
    out << "<h1>PoisonIvy Run Dashboard</h1>";
    out << "<div class='card'><b>Mode:</b> " << stats.mode << "<br><b>Rows:</b> " << stats.data_lines
        << "<br><b>Selected:</b> " << stats.selected_lines << "<br><b>Output Rows:</b> " << stats.out_rows << "</div>";

    int total = max(1, stats.inject_hits + stats.mutate_hits + stats.flip_hits);
    int iw = stats.inject_hits * 100 / total;
    int mw = stats.mutate_hits * 100 / total;
    int fw = 100 - iw - mw;

    out << "<div class='card'><b>Mutation Mix</b><div class='bar'>"
        << "<div class='fill i' style='width:" << iw << "%;float:left'></div>"
        << "<div class='fill m' style='width:" << mw << "%;float:left'></div>"
        << "<div class='fill f' style='width:" << fw << "%;float:left'></div>"
        << "</div><p>inject=" << stats.inject_hits << ", mutate=" << stats.mutate_hits << ", flip=" << stats.flip_hits << "</p></div>";
    out << "</body></html>\n";
}

void maybe_open_gui(const string &path, bool open)
{
    if (!open || path.empty())
        return;
#ifdef _WIN32
    string cmd = "start \"\" \"" + path + "\"";
#elif __APPLE__
    string cmd = "open \"" + path + "\"";
#else
    string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1";
#endif
    system(cmd.c_str());
}

int main(int argc, char *argv[])
{
    if (argc < 5 || argc > 6)
    {
        cerr << "Usage: " << argv[0] << " <main.csv> <malicious.csv> <output.csv> <profile.json> [custom_lib]\n";
        return 1;
    }

    string main_csv = argv[1];
    string bad_csv = argv[2];
    string out_csv = argv[3];
    string cfg_file = argv[4];

    json cfg = read_cfg(cfg_file);
    string err;
    if (!cfg_ok(cfg, err))
    {
        cerr << "Invalid profile: " << err << "\n";
        return 1;
    }

    Perf perf;
    perf.load(cfg);
    vector<string> hdr = parse_header(main_csv);
    vector<string> bad = read_data_lines(bad_csv);
    if (bad.empty())
    {
        cerr << "Malicious dataset is empty.\n";
        return 1;
    }

    MutFn custom = nullptr;
    if (argc == 6)
    {
        custom = load_custom(argv[5]);
        if (!custom)
            cerr << "Warning: custom plugin not loaded, built-in mutation will be used.\n";
    }

    cout << "Running with mode=" << mode_of(cfg) << ", threads=" << perf.threads;
    if (perf.gpu)
        cout << ", gpu=on";
    cout << "\n";

    RunStats stats = run_stream(main_csv, bad, hdr, cfg, out_csv, perf, custom);

    string json_report = cfg.value("report_json", string(""));
    string html_gui = cfg.value("dashboard_html", string(""));
    bool open_gui = cfg.value("open_dashboard", false);
    write_json_report(json_report, stats, cfg, perf);
    write_html_dashboard(html_gui, stats);
    maybe_open_gui(html_gui, open_gui);

    close_custom();

    cout << "Done. requested=" << cfg.value("inject_count", 0)
         << " selected=" << stats.selected_lines
         << " inject=" << stats.inject_hits
         << " mutate=" << stats.mutate_hits
         << " flip=" << stats.flip_hits
         << " output=" << out_csv << "\n";
    return 0;
}
