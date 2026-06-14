#include "Workspace.hh"

#include <cassert>

namespace bbai {

  WorkspaceModel::WorkspaceModel(unsigned count) {
    if (count < 1) count = 1;
    ws.resize(count);
  }

  void WorkspaceModel::setCurrent(unsigned i) {
    assert(i < ws.size());
    current_ = i;
  }

  std::string WorkspaceModel::name(unsigned i) const {
    assert(i < ws.size());
    if (!ws[i].name.empty()) return ws[i].name;
    return "Workspace " + std::to_string(i + 1);  // blackbox default (id+1)
  }

  void WorkspaceModel::setName(unsigned i, const std::string &new_name) {
    assert(i < ws.size());
    ws[i].name = new_name;
  }

  void WorkspaceModel::setFocused(unsigned i, void *handle) {
    assert(i < ws.size());
    ws[i].focused = handle;
  }

  void *WorkspaceModel::focused(unsigned i) const {
    assert(i < ws.size());
    return ws[i].focused;
  }

  void WorkspaceModel::clearFocused(void *handle) {
    for (auto &w : ws)
      if (w.focused == handle) w.focused = nullptr;
  }

  unsigned WorkspaceModel::addWorkspace(void) {
    ws.emplace_back();
    return static_cast<unsigned>(ws.size() - 1);
  }

  void WorkspaceModel::removeLastWorkspace(void) {
    if (ws.size() <= 1) return;
    ws.pop_back();
    if (current_ >= ws.size()) current_ = static_cast<unsigned>(ws.size() - 1);
  }

} // namespace bbai
