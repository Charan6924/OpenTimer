// greedy_sizer.cpp
//
// Discrete, timing-driven gate sizing on top of OpenTimer's C++ API.
//
// (1): find gates on violating late paths, try every other
// drive strength of the same cell family, keep the moves that improve
// late TNS the most. Repeat until TNS = 0 or no move helps.
//
// (2): visit gates with the most late slack first and
// try one step smaller. Keep the change if late TNS and WNS do not get worse.
//
// Every trial is: repower_gate -> report_tns (incremental update) -> keep or
// revert. OpenTimer only re-times the cone touched by the change.
//
// Usage: greedy_sizer <lib> <verilog> <spef | -> <sdc> [max_iters=50] [top_k_paths=200]
//
// The same .lib is used for the early and late split. Cells are grouped into
// families by cell_footprint, or by the drive suffix in the name when the
// library has no footprints (NAND2_X1/NAND2_X2, or NAND2X1/NAND2X2).

#include <ot/timer/timer.hpp>
#include <ot/liberty/celllib.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <tuple>
#include <unordered_map>

namespace {

using Family = std::vector<std::string>;  // cell names, smallest area first

// Group library cells into drive-strength families with identical pin names.
std::unordered_map<std::string, Family> build_families(const ot::Celllib& lib) {

  static const std::regex drive_suffix(R"(^(.*?)_?X(\d+)$)");

  std::map<std::string, std::vector<const ot::Cell*>> groups;
  for(const auto& [name, cell] : lib.cells) {
    std::string key = cell.cell_footprint;
    if(std::smatch m; key.empty()) {
      key = std::regex_match(name, m, drive_suffix) ? m[1].str() : name;
    }
    groups[key].push_back(&cell);
  }

  auto pin_names = [] (const ot::Cell& c) {
    std::set<std::string> s;
    for(const auto& [pname, _] : c.cellpins) s.insert(pname);
    return s;
  };

  std::unordered_map<std::string, Family> cell_to_family;
  for(auto& [key, cells] : groups) {
    if(cells.size() < 2) continue;
    // repower_gate remaps pins by name, so every member must have the same pins
    const auto pins = pin_names(*cells.front());
    if(!std::all_of(cells.begin(), cells.end(),
                    [&] (const ot::Cell* c) { return pin_names(*c) == pins; })) {
      continue;
    }
    // Smallest first
    std::sort(cells.begin(), cells.end(), [] (const ot::Cell* a, const ot::Cell* b) {
      return std::make_tuple(a->area.value_or(0.0f), a->leakage_power.value_or(0.0f), a->name)
           < std::make_tuple(b->area.value_or(0.0f), b->leakage_power.value_or(0.0f), b->name);
    });
    Family family;
    for(const auto* c : cells) family.push_back(c->name);
    for(const auto& name : family) cell_to_family[name] = family;
  }
  return cell_to_family;
}

// Copy the input netlist and replace the cell type of every resized instance.
// (Timer::dump_verilog writes port connections without commas, which other
// tools reject, so the original file is edited instead.)
void write_sized_netlist(const ot::Timer& timer, const std::string& in, const std::string& out) {
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

}  // namespace

int main(int argc, char* argv[]) {

  if(argc < 5) {
    std::cerr << "usage: " << argv[0]
              << " <lib> <verilog> <spef|-> <sdc> [max_iters] [top_k_paths]\n";
    return 1;
  }

  const std::string lib_path = argv[1], v_path = argv[2], spef = argv[3], sdc = argv[4];
  const int    max_iters = argc > 5 ? std::stoi(argv[5]) : 50;
  const size_t top_k     = argc > 6 ? std::stoul(argv[6]) : 200;
  constexpr float eps = 1e-6f;

  // ---- read the design --------------------------------------------------------------------
  ot::Timer timer;
  timer.read_celllib(lib_path, std::nullopt).read_verilog(v_path);
  if(spef != "-") timer.read_spef(spef);
  timer.read_sdc(sdc);

  // Parse the library a second time: Timer does not expose its cell library.
  ot::Celllib lib;
  lib.read(lib_path);
  const auto families = build_families(lib);

  auto late_tns = [&] { return timer.report_tns(ot::MAX).value_or(0.0f); };
  auto late_wns = [&] { return timer.report_wns(ot::MAX).value_or(0.0f); };

  auto summary = [&] (const char* tag) {
    std::cout << tag
              << "  late TNS " << late_tns()
              << "  late WNS " << late_wns()
              << "  early TNS " << timer.report_tns(ot::MIN).value_or(0.0f)
              << "  area " << timer.report_area().value_or(0.0f)
              << "  leakage " << timer.report_leakage_power().value_or(0.0f) << '\n';
  };

  const auto t0 = std::chrono::steady_clock::now();
  summary("before   ");

  // ---- phase 1 -----------------------------------------------
  size_t trials = 0;

  for(int iter = 0; iter < max_iters; ++iter) {

    const float base = late_tns();
    if(base >= 0.0f) break;

    // Gates on the violating late paths
    std::set<std::string> critical;
    for(const auto& path : timer.report_timing(top_k, ot::MAX)) {
      if(!(path.slack < 0.0f)) continue;
      for(const auto& point : path) {
        if(const auto* g = point.pin.gate()) critical.insert(g->name());
      }
    }

    // Best alternative cell per gate, measured by the true incremental TNS
    struct Move { std::string gate, from, to; float gain; };
    std::vector<Move> moves;

    for(const auto& gname : critical) {
      const std::string cur = timer.gates().at(gname).cell_name();
      auto fam = families.find(cur);
      if(fam == families.end()) continue;

      Move best {gname, cur, cur, 0.0f};
      for(const auto& cand : fam->second) {
        if(cand == cur) continue;
        timer.repower_gate(gname, cand);
        const float gain = late_tns() - base;   // TNS <= 0, so gain > 0 is better
        ++trials;
        if(gain > best.gain + eps) best = {gname, cur, cand, gain};
      }
      timer.repower_gate(gname, cur);           // revert the trial
      if(best.to != cur) moves.push_back(best);
    }

    if(moves.empty()) break;

    // Commit in order of gain; moves interact, so re-check each one
    std::sort(moves.begin(), moves.end(),
              [] (const Move& a, const Move& b) { return a.gain > b.gain; });
    float now = base;
    int committed = 0;
    for(const auto& m : moves) {
      timer.repower_gate(m.gate, m.to);
      const float t = late_tns();
      ++trials;
      if(t > now + eps) { now = t; ++committed; }
      else timer.repower_gate(m.gate, m.from);
    }

    std::cout << "iter " << iter
              << "  critical gates " << critical.size()
              << "  committed " << committed
              << "  late TNS " << base << " -> " << late_tns() << '\n';

    if(committed == 0) break;
  }

  summary("fixed    ");

  // ---- phase 2 ---------------------------------------------------------------
  // Worst late slack at each gate's output pin; most slack is visited first.
  std::vector<std::pair<float, std::string>> order;
  for(const auto& [pname, pin] : timer.pins()) {
    const auto* g  = pin.gate();
    const auto* cp = pin.cellpin(ot::MAX);
    if(!g || !cp || cp->direction != ot::CellpinDirection::OUTPUT) continue;
    float s = std::numeric_limits<float>::max();
    for(auto rf : {ot::RISE, ot::FALL}) {
      if(auto v = timer.report_slack(pname, ot::MAX, rf)) s = std::min(s, *v);
    }
    order.emplace_back(s, g->name());
  }
  std::sort(order.rbegin(), order.rend());

  float tns_now = late_tns(), wns_now = late_wns();
  int downsized = 0;
  for(const auto& [slack, gname] : order) {
    const std::string cur = timer.gates().at(gname).cell_name();
    auto fam = families.find(cur);
    if(fam == families.end()) continue;
    const auto& f = fam->second;
    const auto idx = std::find(f.begin(), f.end(), cur) - f.begin();
    if(idx == 0) continue;

    timer.repower_gate(gname, f[idx - 1]);
    const float t = late_tns(), w = late_wns();
    ++trials;
    if(t >= tns_now - eps && w >= wns_now - eps) { tns_now = t; wns_now = w; ++downsized; }
    else timer.repower_gate(gname, cur);
  }

  std::cout << "area recovery: downsized " << downsized << " gates\n";
  summary("after    ");

  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "timing trials " << trials << ", runtime " << secs << " s\n";

  // ---- write the sized netlist -------------------------------------------------------------
  write_sized_netlist(timer, v_path, "sized.v");
  std::cout << "wrote sized.v\n";
  return 0;
}
