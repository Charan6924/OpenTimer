#include <ot/timer/timer.hpp>

namespace ot {

// Function: _is_redundant_timing
bool Timer::_is_redundant_timing(const Timing& timing, Split el) const {

  switch(el) {
    case MIN:
      if(timing.is_max_constraint()) {
        return true; 
      }
    break;

    case MAX:
      if(timing.is_min_constraint()) {
        return true;
      }
    break;
  }

  return false;
}

// Function: read_celllib
Timer& Timer::read_celllib(std::filesystem::path path, std::optional<Split> el) {
  
  auto lib = std::make_shared<Celllib>();
  
  std::scoped_lock lock(_mutex);
  
  // Library parser
  auto parser = _taskflow.emplace([path=std::move(path), lib] () {
    OT_LOGI("loading celllib ", path);
    lib->read(path);
  });

  // Placeholder to add_lineage
  auto reader = _taskflow.emplace([this, lib, el] () {
    if(el) {
      _merge_celllib(*lib, *el);
    }
    else {
      auto cpy = *lib;
      _merge_celllib(cpy, MIN);
      _merge_celllib(*lib, MAX);
    }
  });

  // Reader -> reader
  parser.precede(reader);

  _add_to_lineage(reader);

  return *this;
}

// Each Lut points at a LutTemplate owned by the library it was parsed from.
// Point it at the template of the same name in the library that keeps the cell,
// so the pointer stays valid after the source library is destroyed.
static void _rebind_lut_templates(Cell& cell, const Celllib& owner) {
  const auto rebind = [&owner] (std::optional<Lut>& lut) {
    if(!lut || !lut->lut_template) return;
    if(auto itr = owner.lut_templates.find(lut->lut_template->name); itr != owner.lut_templates.end()) {
      lut->lut_template = &itr->second;
    }
    else {
      OT_LOGE("lut template ", std::quoted(lut->lut_template->name), " not found after merge");
      lut->lut_template = nullptr;
    }
  };
  for(auto& [pname, cellpin] : cell.cellpins) {
    for(auto& timing : cellpin.timings) {
      rebind(timing.cell_rise);
      rebind(timing.cell_fall);
      rebind(timing.rise_transition);
      rebind(timing.fall_transition);
      rebind(timing.rise_constraint);
      rebind(timing.fall_constraint);
      rebind(timing.internal_power.rise_power);
      rebind(timing.internal_power.fall_power);
    }
  }
}

// Procedure: _merge_celllib
void Timer::_merge_celllib(Celllib& lib, Split el) {

  _rebase_unit(lib);

  // initialize a library
  if(!_celllib[el]) {
    _celllib[el] = std::move(lib);
    // A copied library (read_celllib without a split) still points at the
    // original's templates; rebind to this library's own copies.
    for(auto& [cname, cell] : _celllib[el]->cells) {
      _rebind_lut_templates(cell, *_celllib[el]);
    }
    OT_LOGI(
      "added ", to_string(el), " celllib ", std::quoted(_celllib[el]->name),
      " [cells:", _celllib[el]->cells.size(), ']'
    );
  }
  // merge the library
  else {
    // Merge the lut template. unordered_map::merge leaves templates whose
    // names already exist in the source library, which is destroyed later.
    _celllib[el]->lut_templates.merge(std::move(lib.lut_templates));

    // Rebind the incoming cells to the surviving templates before merging.
    for(auto& [cname, cell] : lib.cells) {
      _rebind_lut_templates(cell, *_celllib[el]);
    }

    // Merge the cell
    _celllib[el]->cells.merge(std::move(lib.cells));
    
    OT_LOGI(
      "merged with library ", std::quoted(lib.name), 
      " [cells:", _celllib[el]->cells.size(), ']'
    );
  }
}

};  // end of namespace ot. -----------------------------------------------------------------------




