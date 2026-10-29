// sizer_common.hpp
//
// Shared pieces of the OpenTimer gate sizers (ot-sizer, ot-gsizer): command-line
// options, design loading, cell families, metrics, and output files.

#ifndef OT_MAIN_SIZER_COMMON_HPP_
#define OT_MAIN_SIZER_COMMON_HPP_

#include <ot/timer/timer.hpp>
#include <ot/liberty/celllib.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace sizer {

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------------------------------------------
// Options

struct Options {
  std::vector<std::string> libs;            // read for both early and late
  std::string verilog, spef, sdc;
  std::string families;                     // optional "<cell> <family>" file
  std::string instmap;                      // optional "<opentimer name> <original name>" file
  std::string out {"sized"};                // writes <out>.v and <out>.size
  bool preset_clear_arcs {true};            // false matches OpenSTA's default
  double time_limit {0.0};                  // seconds, 0 = none
  unsigned threads {0};                     // 0 = OpenTimer default
  int max_iters {50};
  size_t top_k {200};                       // greedy: violating paths searched per iteration
  size_t gates_per_iter {100};              // greedy: most critical gates tried per iteration
  double tau {0.0};                         // gradient: temperature, 0 = 0.1 x median output slew
  int max_step {2};                         // gradient: largest drive-strength step per move
  bool recover_area {true};
};

inline void usage(const char* prog) {
  std::cerr <<
    "usage: " << prog << " <lib> <verilog> <spef|-> <sdc> [max_iters] [top_k_paths]\n"
    "   or: " << prog << " --lib <file> [--lib <file> ...] --verilog <file> --sdc <file> [options]\n"
    "options:\n"
    "  --spef <file>            parasitics\n"
    "  --families <file>        lines \"<cell> <family>\" defining interchangeable cells\n"
    "  --instmap <file>         lines \"<instance> <original name>\" for the .size output\n"
    "  --no-preset-clear        do not time asynchronous preset/clear arcs (OpenSTA default)\n"
    "  --time-limit <seconds>   stop optimizing after this long\n"
    "  --threads <n>            OpenTimer worker threads\n"
    "  --max-iters <n>          setup-fixing iterations (default 50)\n"
    "  --top-k <n>              greedy: violating paths searched per iteration (default 200)\n"
    "  --gates-per-iter <n>     greedy: critical gates tried per iteration (default 100)\n"
    "  --tau <t>                gradient: smoothing temperature in library time units\n"
    "  --max-step <n>           gradient: largest drive-strength step per move (default 2)\n"
    "  --no-area-recovery       skip the area-recovery phase\n"
    "  --out <prefix>           output prefix (default \"sized\")\n";
}

inline Options parse(int argc, char* argv[]) {
  Options o;
  std::vector<std::string> positional;
  for(int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&] () -> std::string {
      if(i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
      return argv[++i];
    };
    if(a == "--lib") o.libs.push_back(next());
    else if(a == "--verilog") o.verilog = next();
    else if(a == "--spef") o.spef = next();
    else if(a == "--sdc") o.sdc = next();
    else if(a == "--families") o.families = next();
    else if(a == "--instmap") o.instmap = next();
    else if(a == "--out") o.out = next();
    else if(a == "--no-preset-clear") o.preset_clear_arcs = false;
    else if(a == "--time-limit") o.time_limit = std::stod(next());
    else if(a == "--threads") o.threads = std::stoul(next());
    else if(a == "--max-iters") o.max_iters = std::stoi(next());
    else if(a == "--top-k") o.top_k = std::stoul(next());
    else if(a == "--gates-per-iter") o.gates_per_iter = std::stoul(next());
    else if(a == "--tau") o.tau = std::stod(next());
    else if(a == "--max-step") o.max_step = std::stoi(next());
    else if(a == "--no-area-recovery") o.recover_area = false;
    else if(a == "-h" || a == "--help") { usage(argv[0]); std::exit(0); }
    else if(!a.empty() && a[0] == '-' && a != "-") throw std::invalid_argument("unknown option " + a);
    else positional.push_back(a);
  }
  // Legacy form: <lib> <verilog> <spef|-> <sdc> [max_iters] [top_k]
  if(!positional.empty()) {
    if(positional.size() < 4) throw std::invalid_argument("expected <lib> <verilog> <spef|-> <sdc>");
    o.libs.push_back(positional[0]);
    o.verilog = positional[1];
    if(positional[2] != "-") o.spef = positional[2];
    o.sdc = positional[3];
    if(positional.size() > 4) o.max_iters = std::stoi(positional[4]);
    if(positional.size() > 5) o.top_k = std::stoul(positional[5]);
  }
  if(o.libs.empty() || o.verilog.empty() || o.sdc.empty()) throw std::invalid_argument("need --lib, --verilog and --sdc");
  return o;
}

// ------------------------------------------------------------------------------------------------
// Loading

inline void load(ot::Timer& timer, const Options& o) {
  if(o.threads) timer.set_num_threads(o.threads);
  timer.set_preset_clear_arcs(o.preset_clear_arcs);
  for(const auto& l : o.libs) timer.read_celllib(l, std::nullopt);
  timer.read_verilog(o.verilog);
  if(!o.spef.empty()) timer.read_spef(o.spef);
  timer.read_sdc(o.sdc);
  timer.update_timing();
}

// ------------------------------------------------------------------------------------------------
// Cell families: interchangeable cells (same function and pin names), smallest first.

using Family = std::vector<std::string>;

struct Families {
  std::unordered_map<std::string, std::shared_ptr<const Family>> of;   // cell -> its family
  const Family* find(const std::string& cell) const {
    auto it = of.find(cell);
    return it == of.end() ? nullptr : it->second.get();
  }
};

inline Families build_families(const ot::Timer& timer, const std::string& family_file) {

  const auto* lib = timer.celllib(ot::MAX);
  const auto* early = timer.celllib(ot::MIN);
  if(!lib || !early) throw std::runtime_error("cell library not loaded");

  std::unordered_map<std::string, std::string> given;
  if(!family_file.empty()) {
    std::ifstream ifs(family_file);
    if(!ifs) throw std::runtime_error("cannot open " + family_file);
    std::string cell, fam;
    while(ifs >> cell >> fam) given[cell] = fam;
  }

  // Drive-strength suffixes: NAND2_X2, NAND2X2 (Nangate, OSU) and NAND2x1p5_ASAP7_75t_R (ASAP7).
  static const std::regex asap7(R"(^(.*?)(?:xp\d+|x\d+(?:p\d+)?)(_ASAP7_\w+)$)");
  static const std::regex drive(R"(^(.*?)_?X(\d+)$)");

  std::map<std::string, std::vector<const ot::Cell*>> groups;
  for(const auto& [name, cell] : lib->cells) {
    if(!early->cells.count(name)) continue;
    std::string key;
    std::smatch m;
    if(!given.empty()) {
      auto it = given.find(name);
      if(it == given.end()) continue;
      key = it->second;
    }
    else if(!cell.cell_footprint.empty()) key = cell.cell_footprint;
    else if(std::regex_match(name, m, asap7)) key = m[1].str() + m[2].str();
    else if(std::regex_match(name, m, drive)) key = m[1].str();
    else key = name;
    groups[key].push_back(&cell);
  }

  auto signature = [] (const ot::Cell& c) {
    std::set<std::pair<std::string, int>> s;
    for(const auto& [pname, pin] : c.cellpins) s.insert({pname, pin.direction ? int(*pin.direction) : -1});
    return s;
  };

  Families families;
  for(auto& [key, cells] : groups) {
    // repower_gate remaps pins by name: keep the largest subset with identical pins.
    std::map<std::set<std::pair<std::string, int>>, std::vector<const ot::Cell*>> by_pins;
    for(auto* c : cells) by_pins[signature(*c)].push_back(c);
    for(auto& [sig, members] : by_pins) {
      if(members.size() < 2) continue;
      // Smallest first; break equal areas by leakage (grows with drive strength), then name.
      std::sort(members.begin(), members.end(), [] (const ot::Cell* a, const ot::Cell* b) {
        return std::make_tuple(a->area.value_or(0.0f), a->leakage_power.value_or(0.0f), a->name)
             < std::make_tuple(b->area.value_or(0.0f), b->leakage_power.value_or(0.0f), b->name);
      });
      auto fam = std::make_shared<Family>();
      for(auto* c : members) fam->push_back(c->name);
      for(const auto& n : *fam) families.of[n] = fam;
    }
  }
  return families;
}

inline float cell_area(const ot::Timer& timer, const std::string& cell) {
  const auto& cells = timer.celllib(ot::MAX)->cells;
  auto it = cells.find(cell);
  return it == cells.end() ? 0.0f : it->second.area.value_or(0.0f);
}

// ------------------------------------------------------------------------------------------------
// Metrics and logging

struct Metrics {
  float late_tns {0}, late_wns {0}, early_tns {0}, area {0};
};

inline Metrics metrics(ot::Timer& timer) {
  Metrics m;
  m.late_tns  = timer.report_tns(ot::MAX).value_or(0.0f);
  m.late_wns  = timer.report_wns(ot::MAX).value_or(0.0f);
  m.early_tns = timer.report_tns(ot::MIN).value_or(0.0f);
  m.area      = timer.report_area().value_or(0.0f);
  return m;
}

struct Log {
  Clock::time_point start {Clock::now()};
  double elapsed() const { return std::chrono::duration<double>(Clock::now() - start).count(); }
  void line(const std::string& tag, const Metrics& m, const std::string& extra = "") const {
    std::cout << '[' << std::fixed << std::setprecision(1) << std::setw(7) << elapsed() << " s] "
              << std::left << std::setw(14) << tag << std::right << std::defaultfloat << std::setprecision(6)
              << "  late TNS " << m.late_tns << "  late WNS " << m.late_wns
              << "  early TNS " << m.early_tns << "  area " << m.area;
    if(!extra.empty()) std::cout << "  " << extra;
    std::cout << std::endl;
  }
};

// ------------------------------------------------------------------------------------------------
// Per-gate output slack (late, worst over transitions); lowest() when unknown.

inline std::unordered_map<std::string, float> gate_output_slacks(ot::Timer& timer) {
  timer.update_timing();
  std::unordered_map<std::string, float> slack;
  for(const auto& [pname, pin] : timer.pins()) {
    const auto* g = pin.gate();
    const auto* cp = pin.cellpin(ot::MAX);
    if(!g || !cp || cp->direction != ot::CellpinDirection::OUTPUT) continue;
    float s = std::numeric_limits<float>::max();
    bool any = false;
    for(auto rf : {ot::RISE, ot::FALL}) {
      if(auto v = pin.slack(ot::MAX, rf)) { s = std::min(s, *v); any = true; }
    }
    auto& entry = slack.try_emplace(g->name(), std::numeric_limits<float>::max()).first->second;
    if(any) entry = std::min(entry, s);
  }
  return slack;
}

inline float clock_period(const ot::Timer& timer) {
  for(const auto& [name, clk] : timer.clocks()) return clk.period();
  return 0.0f;
}

// ------------------------------------------------------------------------------------------------
// Outputs

// Copy the input netlist and replace the cell type of every instance with its
// current cell (Timer::dump_verilog drops the commas between port connections).
inline void write_netlist(const ot::Timer& timer, const std::string& in, const std::string& out) {
  static const std::regex inst(R"(^(\s*)([A-Za-z_]\w*)(\s+)(\S+)(\s*\(.*)$)");
  std::ifstream ifs(in);
  std::ofstream ofs(out);
  std::string line;
  std::smatch m;
  while(std::getline(ifs, line)) {
    if(std::regex_match(line, m, inst)) {
      if(auto g = timer.gates().find(m[4].str()); g != timer.gates().end()) {
        line = m[1].str() + g->second.cell_name() + m[3].str() + m[4].str() + m[5].str();
      }
    }
    ofs << line << '\n';
  }
}

// "<instance> <cell>" per line, with original instance names when a map is given
// (the ICCAD 2024 contest .size format). Brackets in names are escaped with a
// backslash, as OpenROAD prints them (reg[0] -> reg\[0\]).
inline void write_size_file(const ot::Timer& timer, const std::string& instmap, const std::string& out) {
  auto escape = [] (const std::string& n) {
    std::string e;
    for(char c : n) { if(c == '[' || c == ']') e += '\\'; e += c; }
    return e;
  };
  std::unordered_map<std::string, std::string> original;
  if(!instmap.empty()) {
    std::ifstream ifs(instmap);
    std::string a, b;
    while(ifs >> a >> b) original[a] = escape(b);
  }
  std::vector<std::pair<std::string, std::string>> rows;
  for(const auto& [name, gate] : timer.gates()) {
    auto it = original.find(name);
    rows.emplace_back(it == original.end() ? name : it->second, gate.cell_name());
  }
  std::sort(rows.begin(), rows.end());
  std::ofstream ofs(out);
  for(const auto& [n, c] : rows) ofs << n << ' ' << c << '\n';
}

inline void write_outputs(const ot::Timer& timer, const Options& o) {
  write_netlist(timer, o.verilog, o.out + ".v");
  write_size_file(timer, o.instmap, o.out + ".size");
  std::cout << "wrote " << o.out << ".v and " << o.out << ".size" << std::endl;
}

}  // namespace sizer

#endif
