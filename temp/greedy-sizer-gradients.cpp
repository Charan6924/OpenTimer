// Same initial circuit loading as main/sizer/greedy_sizer.cpp.
// Run from repository root; optional argument selects the output prefix.
#include <ot/timer/timer.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

static std::string quoted(const std::string& s) {
  std::string result = "\"";
  for(char c : s) { if(c == '"') result += '"'; result += c; }
  return result + '"';
}

int main(int argc, char** argv) {
  try {
    const std::string prefix = argc > 1 ? argv[1] : "temp/map9v3";
    const std::string circuit = "example/map9v3/";
    ot::Timer timer;
    timer.read_celllib(circuit + "osu018_stdcells", std::nullopt)
         .read_verilog(circuit + "map9v3.v")
         .read_spef(circuit + "map9v3.spef")
         .read_sdc(circuit + "map9v3.sdc");
    std::ofstream summary(prefix + "-gradient-summary.txt");
    summary.exceptions(std::ios::failbit | std::ios::badbit);
    summary << std::setprecision(17);
    summary << "Circuit: example/map9v3, original netlist, same library for MIN/MAX\n"
            << "Hard late TNS: " << timer.report_tns(ot::MAX).value_or(0)
            << "\nHard late WNS: " << timer.report_wns(ot::MAX).value_or(0) << '\n';
    for(auto objective : {ot::TimingObjective::TNS, ot::TimingObjective::WNS}) {
      const std::string name = objective == ot::TimingObjective::TNS ? "tns" : "wns";
      ot::GradientOptions options;
      options.objective = objective;
      options.split = ot::MAX;
      options.arrival_temperature = 0.001;
      options.objective_temperature = 0.001;
      auto report = timer.report_gradients(options);
      if(!report.objective_value || !std::isfinite(*report.objective_value))
        throw std::runtime_error("Missing/nonfinite objective");
      std::ofstream out(prefix + "-" + name + "-gradients.csv");
      out.exceptions(std::ios::failbit | std::ios::badbit);
      out << std::setprecision(17);
      out << "kind,id,name,from,to,split,input_transition,output_transition,entry,value\n";
      size_t count = 0, nonzero = 0;
      auto row = [&](const std::string& kind, const std::string& id,
                     const std::string& label, const std::string& from,
                     const std::string& to, const std::string& split,
                     const std::string& input, const std::string& output,
                     const std::string& entry, double value) {
        if(!std::isfinite(value)) throw std::runtime_error("Nonfinite gradient");
        out << quoted(kind) << ',' << quoted(id) << ',' << quoted(label) << ','
            << quoted(from) << ',' << quoted(to) << ',' << quoted(split) << ','
            << quoted(input) << ',' << quoted(output) << ',' << quoted(entry)
            << ',' << value << '\n';
        ++count; nonzero += value != 0;
      };
      auto split_name = [](ot::Split s) { return s == ot::MIN ? "MIN" : "MAX"; };
      auto tran_name = [](ot::Tran t) { return t == ot::RISE ? "RISE" : "FALL"; };
      for(const auto& arc : report.arcs)
        for(auto s : {ot::MIN, ot::MAX})
          for(auto i : {ot::RISE, ot::FALL})
            for(auto o : {ot::RISE, ot::FALL})
              if(auto v = arc.value[s][i][o])
                row(arc.kind == ot::TimingArcKind::CELL ? "cell_delay" : "net_delay",
                    std::to_string(arc.arc_id), "", arc.from_pin, arc.to_pin,
                    split_name(s), tran_name(i), tran_name(o), "", *v);
      for(const auto& pin : report.pins)
        for(auto s : {ot::MIN, ot::MAX})
          for(auto t : {ot::RISE, ot::FALL}) {
            if(pin.primary_input) {
              row("input_arrival", "", pin.pin, "", "", split_name(s), "", tran_name(t), "", pin.arrival[s][t]);
              row("input_slew", "", pin.pin, "", "", split_name(s), "", tran_name(t), "", pin.slew[s][t]);
            }
            row(pin.primary_output ? "output_load" : "pin_capacitance", "", pin.pin,
                "", "", split_name(s), "", tran_name(t), "", pin.capacitance[s][t]);
          }
      for(const auto& r : report.rc_resistances)
        row("rc_resistance", "", r.net, r.from_node, r.to_node, "", "", "", "", r.value);
      for(const auto& c : report.rc_capacitances)
        row("rc_capacitance", "", c.net, c.node, "", "", "", "", "", c.value);
      std::ofstream uses(prefix + "-" + name + "-lut-uses.csv");
      uses.exceptions(std::ios::failbit | std::ios::badbit);
      uses << "table_id,table_name,arc_id,split,kind,output_transition\n";
      for(const auto& lut : report.luts) {
        for(size_t i = 0; i < lut.values.size(); ++i)
          row("lut_entry", std::to_string(lut.table_id), lut.name, "", "", "", "", "", std::to_string(i), lut.values[i]);
        for(const auto& use : lut.uses)
          uses << lut.table_id << ',' << quoted(lut.name) << ',' << use.arc_id << ','
               << split_name(use.split) << ','
               << (use.kind == ot::LutTimingKind::DELAY ? "DELAY" : use.kind == ot::LutTimingKind::SLEW ? "SLEW" : "CONSTRAINT")
               << ',' << tran_name(use.transition) << '\n';
      }
      summary << name << " objective: " << *report.objective_value
              << "\n  split: MAX; transitions: RISE and FALL; temperatures: 0.001\n"
              << "  arcs: " << report.arcs.size() << "; pins: " << report.pins.size()
              << "; RC resistances: " << report.rc_resistances.size()
              << "; RC capacitances: " << report.rc_capacitances.size()
              << "; LUTs: " << report.luts.size()
              << "\n  finite gradient values: " << count << "; nonzero: " << nonzero << '\n';
      std::cout << name << " objective " << std::setprecision(17) << *report.objective_value
                << ", " << count << " finite gradients, " << nonzero << " nonzero\n";
    }
  } catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
