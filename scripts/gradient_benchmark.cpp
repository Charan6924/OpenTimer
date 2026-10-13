#include <ot/timer/timer.hpp>
#include <sys/resource.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ctime>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

using Clock = std::chrono::steady_clock;

static double seconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

static double peak_memory() {
  rusage usage {};
  if(getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("getrusage failed");
#ifdef __APPLE__
  return usage.ru_maxrss / (1024.0 * 1024.0);
#else
  return usage.ru_maxrss / 1024.0;
#endif
}

// Peak footprint includes memory that RSS does not capture on macOS.
static std::optional<double> peak_footprint() {
#ifdef __APPLE__
  task_vm_info_data_t info {};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS
      && count >= TASK_VM_INFO_REV3_COUNT)
    return info.ledger_phys_footprint_peak / (1024.0 * 1024.0);
#endif
  return std::nullopt;
}

static std::string quote(const std::string& text) {
  std::ostringstream out;
  out << '"';
  for(unsigned char c : text) {
    if(c == '"' || c == '\\') out << '\\' << c;
    else if(c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
    else out << c;
  }
  return out.str() + '"';
}

static std::string csv_quote(const std::string& text) {
  std::string result = "\"";
  for(char c : text) { if(c == '"') result += '"'; result += c; }
  return result + '"';
}

static const std::string csv_header =
  "timestamp_unix,benchmark,objective,split,threads,arrival_temperature,objective_temperature,"
  "time_unit_seconds,status,stage,error,load_and_initial_timing_seconds,timing_report_seconds,"
  "gradient_seconds,total_seconds,peak_rss_mib,pre_gradient_peak_rss_mib,objective_value,"
  "hard_tns,hard_wns,gates,nets,pins,arcs,rc_resistances,rc_capacitances,luts,lut_entries,"
  "gradient_values,nonzero_gradients,actual_threads,peak_footprint_mib";

struct BenchmarkSettings {
  std::string benchmark {"superblue4"}; // benchmark name
  std::filesystem::path directory = std::filesystem::path("iccad2015.ot") / benchmark;
  std::string objective {"TNS"};                  // TNS or WNS
  std::string split {"MAX"};                      // MIN, MAX, or both
  double arrival_temperature {0.001};
  double objective_temperature {0.001};
  unsigned threads {4};
  bool save_nonzero_gradients {true};

  std::filesystem::path early_lib = directory / (benchmark + "_Early.lib");
  std::filesystem::path late_lib = directory / (benchmark + "_Late.lib");
  std::filesystem::path verilog = directory / (benchmark + ".v");
  std::filesystem::path sdc = directory / (benchmark + ".sdc");
  std::filesystem::path spef = std::filesystem::is_regular_file(directory / "rc.spef") ? directory / "rc.spef" : std::filesystem::path("-");
  std::filesystem::path result_file = std::filesystem::path("results") / (benchmark + "-" + objective + ".json");
  std::filesystem::path comparison_file {"results/optimized-gradient-benchmarks.csv"};
  std::filesystem::path nonzero_file = result_file.string() + ".nonzero.txt";
};


int main() {
  BenchmarkSettings settings;
  try {
    if(settings.objective != "TNS" && settings.objective != "WNS")
      throw std::invalid_argument("objective must be TNS or WNS");
    if(settings.split != "MIN" && settings.split != "MAX" && settings.split != "both")
      throw std::invalid_argument("split must be MIN, MAX, or both");
    if(settings.threads == 0) throw std::invalid_argument("threads must be positive");
    if(!std::isfinite(settings.arrival_temperature) || settings.arrival_temperature <= 0 ||
       !std::isfinite(settings.objective_temperature) || settings.objective_temperature <= 0)
      throw std::invalid_argument("temperatures must be positive and finite");
    if(!settings.result_file.parent_path().empty())
      std::filesystem::create_directories(settings.result_file.parent_path());
    if(!settings.comparison_file.parent_path().empty())
      std::filesystem::create_directories(settings.comparison_file.parent_path());
    if(std::filesystem::exists(settings.comparison_file) && std::filesystem::file_size(settings.comparison_file)) {
      std::ifstream existing(settings.comparison_file);
      std::string header;
      if(!std::getline(existing, header) || header != csv_header)
        throw std::runtime_error("Comparison CSV has a different schema; choose a new comparison_file");
    }
  } catch(const std::exception& e) {
    std::cerr << e.what() << '\n'; return 2;
  }

  const auto total_start = Clock::now();

  size_t actual_threads = 0;
  double load_seconds = 0, timing_seconds = 0, gradient_seconds = 0;
  double pre_gradient_peak = 0, time_unit_seconds = 0;
  size_t gates = 0, nets = 0, pins = 0, arcs = 0, rc_resistances = 0, rc_capacitances = 0;
  size_t luts = 0, lut_entries = 0, gradient_values = 0, nonzero = 0;
  std::optional<double> objective_value, hard_tns, hard_wns;
  std::string stage = "loading", status = "ok", error;
  auto stage_start = Clock::now();

  try {
    ot::Timer timer;
    timer.set_num_threads(settings.threads);
    actual_threads = timer.num_threads();
    timer.read_celllib(settings.early_lib, ot::MIN)
        .read_celllib(settings.late_lib, ot::MAX)
        .read_verilog(settings.verilog);

    if(settings.spef != "-") timer.read_spef(settings.spef);
    timer.read_sdc(settings.sdc);

    // Reading is deferred: update_timing includes actual parsing and model construction.
    timer.update_timing();
    load_seconds = seconds(stage_start);

    if(timer.time_unit()) time_unit_seconds = timer.time_unit()->value();
    gates = timer.gates().size();
    nets = timer.nets().size();
    pins = timer.pins().size();

    stage = "timing"; stage_start = Clock::now();
    const std::string& split = settings.split;
    std::optional<ot::Split> filter;

    if(split == "MIN") {
      filter = ot::MIN;
    }
    else if(split == "MAX") {
      filter = ot::MAX;
    }

    hard_tns = timer.report_tns(filter);
    hard_wns = timer.report_wns(filter);

    timing_seconds = seconds(stage_start);
    pre_gradient_peak = peak_memory();

    ot::GradientOptions options;
    options.objective = settings.objective == "TNS" ? ot::TimingObjective::TNS : ot::TimingObjective::WNS;
    options.split = filter;
    options.arrival_temperature = settings.arrival_temperature;
    options.objective_temperature = settings.objective_temperature;
    stage = "gradient";

    stage_start = Clock::now();
    const auto report = timer.report_gradients(options);
    gradient_seconds = seconds(stage_start);

    stage = "validation";
    objective_value = report.objective_value;
    if(objective_value && !std::isfinite(*objective_value)) throw std::runtime_error("Nonfinite objective");
    if(!objective_value) status = "no_endpoints";
    arcs = report.arcs.size();
    rc_resistances = report.rc_resistances.size();
    rc_capacitances = report.rc_capacitances.size();
    luts = report.luts.size();

    // check if infinite gradients
    auto check = [&](double value) {
      if(!std::isfinite(value)) throw std::runtime_error("Nonfinite gradient");
      ++gradient_values; nonzero += value != 0;
    };

    std::ofstream sparse;
    if(settings.save_nonzero_gradients) {
      sparse.open(settings.nonzero_file);
      if(!sparse) throw std::runtime_error("Cannot write gradient comparison file");
      sparse << std::setprecision(17);
    }
    for(const auto& arc : report.arcs)
      for(auto el : ot::SPLIT) for(auto frf : ot::TRAN) for(auto trf : ot::TRAN)
        if(auto value = arc.value[el][frf][trf]) {
          check(*value);
          if(settings.save_nonzero_gradients && *value != 0)
            sparse << "arc " << arc.arc_id << ' ' << arc.from_pin << ' ' << arc.to_pin << ' '
              << el << ' ' << frf << ' ' << trf << ' ' << *value << '\n';
        }
    for(const auto& pin : report.pins)
      for(auto el : ot::SPLIT) for(auto rf : ot::TRAN) {
        const double values[] = {pin.arrival[el][rf], pin.slew[el][rf], pin.capacitance[el][rf]};
        for(size_t kind=0; kind<3; ++kind) {
          check(values[kind]);
          if(settings.save_nonzero_gradients && values[kind] != 0)
            sparse << "pin " << pin.pin << ' ' << kind << ' ' << el << ' ' << rf << ' ' << values[kind] << '\n';
        }
      }
    for(const auto& r : report.rc_resistances) {
      check(r.value);
      if(settings.save_nonzero_gradients && r.value != 0)
        sparse << "res " << r.net << ' ' << r.from_node << ' ' << r.to_node << ' ' << r.value << '\n';
    }
    for(const auto& c : report.rc_capacitances) {
      check(c.value);
      if(settings.save_nonzero_gradients && c.value != 0)
        sparse << "cap " << c.net << ' ' << c.node << ' ' << c.value << '\n';
    }
    for(const auto& lut : report.luts) {
      lut_entries += lut.values.size();
      for(size_t i=0; i<lut.values.size(); ++i) {
        check(lut.values[i]);
        if(settings.save_nonzero_gradients && lut.values[i] != 0)
          sparse << "lut " << lut.table_id << ' ' << i << ' ' << lut.values[i] << '\n';
      }
    }
    if(settings.save_nonzero_gradients) {
      sparse.close();
      if(!sparse) throw std::runtime_error("Cannot finish gradient comparison file");
    }
    stage = "done";

  } catch(const std::exception& e) {
    if(stage == "loading") load_seconds = seconds(stage_start);
    if(stage == "timing") timing_seconds = seconds(stage_start);
    if(stage == "gradient") gradient_seconds = seconds(stage_start);
    status = "error"; error = e.what();
  }

  const double total_seconds = seconds(total_start);
  const double peak_rss_mib = peak_memory();
  const auto footprint_mib = peak_footprint();
  std::ofstream out(settings.result_file);
  if(!out) { std::cerr << "Cannot write result file\n"; return 2; }
  out << std::setprecision(17) << "{\"benchmark\":" << quote(settings.benchmark)
      << ",\"objective\":" << quote(settings.objective) << ",\"split\":" << quote(settings.split)
      << ",\"actual_threads\":" << actual_threads
      << ",\"status\":" << quote(status)
      << ",\"stage\":" << quote(stage) << ",\"error\":" << quote(error)
      << ",\"load_and_initial_timing_seconds\":" << load_seconds
      << ",\"timing_report_seconds\":" << timing_seconds
      << ",\"gradient_seconds\":" << gradient_seconds
      << ",\"total_seconds\":" << total_seconds
      << ",\"peak_rss_mib\":" << peak_rss_mib
      << ",\"pre_gradient_peak_rss_mib\":" << pre_gradient_peak
      << ",\"time_unit_seconds\":" << time_unit_seconds
      << ",\"gates\":" << gates << ",\"nets\":" << nets << ",\"pins\":" << pins
      << ",\"arcs\":" << arcs << ",\"rc_resistances\":" << rc_resistances
      << ",\"rc_capacitances\":" << rc_capacitances << ",\"luts\":" << luts
      << ",\"lut_entries\":" << lut_entries << ",\"gradient_values\":" << gradient_values
      << ",\"nonzero_gradients\":" << nonzero;


  for(auto entry : {std::make_pair("objective_value", objective_value),
                    std::make_pair("peak_footprint_mib", footprint_mib),
                    std::make_pair("hard_tns", hard_tns), std::make_pair("hard_wns", hard_wns)}) {
    out << ',' << quote(entry.first) << ':';
    if(entry.second && std::isfinite(*entry.second)) out << *entry.second; else out << "null";
  }

  out << "}\n"; out.close();
  if(!out) return 2;
  const bool needs_header = !std::filesystem::exists(settings.comparison_file) ||
    std::filesystem::file_size(settings.comparison_file) == 0;
  std::ofstream comparison(settings.comparison_file, std::ios::app);
  if(!comparison) { std::cerr << "Cannot write comparison CSV\n"; return 2; }
  if(needs_header) comparison << csv_header << '\n';
  comparison << std::setprecision(17) << std::time(nullptr) << ','
    << csv_quote(settings.benchmark) << ',' << csv_quote(settings.objective) << ','
    << csv_quote(settings.split) << ',' << settings.threads << ','
    << settings.arrival_temperature << ',' << settings.objective_temperature << ','
    << time_unit_seconds << ',' << csv_quote(status) << ',' << csv_quote(stage) << ','
    << csv_quote(error) << ',' << load_seconds << ',' << timing_seconds << ','
    << gradient_seconds << ',' << total_seconds << ',' << peak_rss_mib << ',' << pre_gradient_peak;
  for(auto value : {objective_value, hard_tns, hard_wns}) {
    comparison << ',';
    if(value && std::isfinite(*value)) comparison << *value;
  }
  comparison << ',' << gates << ',' << nets << ',' << pins << ',' << arcs << ','
    << rc_resistances << ',' << rc_capacitances << ',' << luts << ',' << lut_entries
    << ',' << gradient_values << ',' << nonzero << ',' << actual_threads << ',';
  if(footprint_mib) comparison << *footprint_mib;
  comparison << '\n';
  comparison.close();
  if(!comparison) return 2;
  std::cout << "Results: " << settings.result_file << "\nComparison: " << settings.comparison_file << '\n';
  return status == "error" ? 1 : 0;
}
