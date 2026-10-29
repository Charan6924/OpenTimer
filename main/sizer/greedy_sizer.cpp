// greedy_sizer.cpp  (ot-sizer)
//
// Trial-based gate sizing with OpenTimer's incremental timing. Every trial is
// repower_gate -> report_tns -> keep or undo.
//
//   Phase 1 (fix setup): rank gates on violating late paths by how much negative
//   slack runs through them, try every other cell of the family for the most
//   critical ones, keep the best change. Repeat until TNS = 0 or nothing helps.
//
//   Phase 2 (area recovery): downsize gates with spare slack one step, in
//   adaptive batches checked with one timing update each.
//
// One trial costs an incremental update of the gate's fanout and fanin cones,
// which on large designs is close to a full timing update (0.1-1 s). Earlier
// versions recovered area one gate at a time, which is one or two trials per
// gate in the design and takes days on superblue-size designs; phase 2 is now
// batched and both phases stop at --time-limit.

#include "sizer_common.hpp"

int main(int argc, char* argv[]) {

  sizer::Options opt;
  try { opt = sizer::parse(argc, argv); }
  catch(const std::exception& e) { std::cerr << e.what() << '\n'; sizer::usage(argv[0]); return 1; }

  ot::Timer timer;
  sizer::Log log;
  sizer::load(timer, opt);
  const auto families = sizer::build_families(timer, opt.families);
  constexpr float eps = 1e-6f;

  auto late_tns = [&] { return timer.report_tns(ot::MAX).value_or(0.0f); };
  auto late_wns = [&] { return timer.report_wns(ot::MAX).value_or(0.0f); };
  auto out_of_time = [&] { return opt.time_limit > 0 && log.elapsed() > opt.time_limit; };

  size_t sizable = 0;
  for(const auto& [name, gate] : timer.gates()) sizable += families.find(gate.cell_name()) != nullptr;
  std::cout << "gates " << timer.num_gates() << ", sizable " << sizable << std::endl;
  log.line("before", sizer::metrics(timer));

  size_t trials = 0;

  // ---- phase 1: fix late (setup) violations -----------------------------------------------
  for(int iter = 0; iter < opt.max_iters && !out_of_time(); ++iter) {

    float tns = late_tns();
    if(tns >= 0.0f) break;

    // Criticality: total negative slack of the violating paths through each gate.
    std::unordered_map<std::string, double> score;
    for(const auto& path : timer.report_timing(opt.top_k, ot::MAX)) {
      if(!(path.slack < 0.0f)) continue;
      std::set<std::string> seen;
      for(const auto& point : path) {
        if(const auto* g = point.pin.gate(); g && seen.insert(g->name()).second) score[g->name()] -= path.slack;
      }
    }
    std::vector<std::pair<double, std::string>> ranked;
    for(const auto& [g, s] : score) {
      if(families.find(timer.gates().at(g).cell_name())) ranked.emplace_back(s, g);
    }
    std::sort(ranked.rbegin(), ranked.rend());
    if(ranked.size() > opt.gates_per_iter) ranked.resize(opt.gates_per_iter);

    int changed = 0;
    const float start = tns;
    for(const auto& [s, gname] : ranked) {
      if(out_of_time()) break;
      const std::string cur = timer.gates().at(gname).cell_name();
      std::string best = cur;
      float best_tns = tns;
      for(const auto& cand : *families.find(cur)) {
        if(cand == cur) continue;
        timer.repower_gate(gname, cand);
        const float t = late_tns();
        ++trials;
        if(t > best_tns + eps) { best_tns = t; best = cand; }
      }
      timer.repower_gate(gname, best);   // keep the best (or restore the original)
      if(best != cur) { tns = best_tns; ++changed; }
    }

    log.line("fix iter " + std::to_string(iter), sizer::metrics(timer),
             "critical " + std::to_string(ranked.size()) + "  changed " + std::to_string(changed) +
             "  trials " + std::to_string(trials));
    if(changed == 0 || !(tns > start + eps)) break;
  }

  // ---- phase 2: area recovery in adaptive batches --------------------------------------------
  if(opt.recover_area && !out_of_time()) {
    const float period = sizer::clock_period(timer);
    const float margin = 0.05f * period;     // only touch gates with at least 5% of a period of slack
    auto slack = sizer::gate_output_slacks(timer);
    std::vector<std::pair<float, std::string>> order;
    for(const auto& [g, s] : slack) {
      if(s == std::numeric_limits<float>::max() || s < margin) continue;
      const auto* fam = families.find(timer.gates().at(g).cell_name());
      if(fam && fam->front() != timer.gates().at(g).cell_name()) order.emplace_back(s, g);
    }
    std::sort(order.rbegin(), order.rend());

    float tns = late_tns(), wns = late_wns();
    size_t batch = std::max<size_t>(16, order.size() / 20), pos = 0, downsized = 0, updates = 0;
    while(pos < order.size() && !out_of_time()) {
      const size_t end = std::min(order.size(), pos + batch);
      std::vector<std::pair<std::string, std::string>> applied;   // gate, previous cell
      for(size_t i = pos; i < end; ++i) {
        const auto& g = order[i].second;
        const std::string cur = timer.gates().at(g).cell_name();
        const auto& fam = *families.find(cur);
        const auto idx = std::find(fam.begin(), fam.end(), cur) - fam.begin();
        if(idx == 0) continue;
        timer.repower_gate(g, fam[idx - 1]);
        applied.emplace_back(g, cur);
      }
      const float t = late_tns(), w = late_wns();
      ++updates;
      if(t >= tns - eps && w >= wns - eps) {
        tns = t; wns = w; downsized += applied.size(); pos = end;
        batch = std::min<size_t>(batch * 2, order.size());
      }
      else {
        for(const auto& [g, prev] : applied) timer.repower_gate(g, prev);
        if(batch == 1) { ++pos; }               // a single gate that hurts timing: skip it
        else batch = std::max<size_t>(1, batch / 4);
      }
    }
    timer.update_timing();
    log.line("area recovery", sizer::metrics(timer),
             "downsized " + std::to_string(downsized) + " of " + std::to_string(order.size()) +
             " candidates, " + std::to_string(updates) + " batch updates");
  }

  log.line("after", sizer::metrics(timer), "trials " + std::to_string(trials));
  sizer::write_outputs(timer, opt);
  return 0;
}
