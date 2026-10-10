# Shape of the output gradient report

GradientReport
├── objective                 TNS or WNS
├── split / transition        optional endpoint filters
├── arrival_temperature       smoothing setting
├── objective_temperature     smoothing setting
├── objective_value           TNS/WNS value
├── arcs[]                    arc delay sensitivities
├── pins[]                    pin parameter sensitivities
├── rc_resistances[]           wire resistance sensitivities
├── rc_capacitances[]          wire capacitance sensitivities
└── luts[]                    shared table-entry sensitivities
