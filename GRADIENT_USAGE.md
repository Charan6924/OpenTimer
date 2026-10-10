# Calling `report_gradients`

After loading the circuit and timing constraints into `timer`:

```cpp
ot::GradientOptions options;
options.objective = ot::TimingObjective::TNS; // Or WNS
options.split = ot::MAX;                     // Late timing; MIN for early
options.arrival_temperature = 0.001;         // Must be positive and finite
options.objective_temperature = 0.001;       // Must be positive and finite

ot::GradientReport report = timer.report_gradients(options);
if(report.objective_value) {
  std::cout << "Objective: " << *report.objective_value << '\n';
}
```

Gradients are in `report.arcs`, `pins`, `rc_resistances`,
`rc_capacitances`, and `luts`. Omit `split` to include both analyses;
set `transition` to `ot::RISE` or `ot::FALL` to filter endpoints.
Temperatures use the timer's internal time units. CPPR must be disabled.
