// src/core/document/LayerTable.cpp
#include "core/document/LayerTable.hpp"

namespace cad {

void LayerTable::ensureDefaultLayer() {
    if (m_layers.find("0") == m_layers.end()) {
        Layer l;
        l.name = "0";
        m_layers.emplace("0", l);
    }
}

Layer& LayerTable::add(const Layer& layer) {
    auto res = m_layers.insert_or_assign(layer.name, layer);
    return res.first->second;
}

const Layer* LayerTable::find(const std::string& name) const {
    auto it = m_layers.find(name);
    return it == m_layers.end() ? nullptr : &it->second;
}

Layer* LayerTable::find(const std::string& name) {
    auto it = m_layers.find(name);
    return it == m_layers.end() ? nullptr : &it->second;
}

bool LayerTable::contains(const std::string& name) const {
    return m_layers.find(name) != m_layers.end();
}

bool LayerTable::rename(const std::string& oldName, const std::string& newName) {
    if (oldName == "0" || newName.empty() || oldName == newName) return false;
    auto it = m_layers.find(oldName);
    if (it == m_layers.end() || m_layers.count(newName)) return false;
    Layer l = it->second;
    l.name = newName;
    m_layers.erase(it);
    m_layers.emplace(newName, std::move(l));
    return true;
}

bool LayerTable::remove(const std::string& name) {
    if (name == "0") return false;
    return m_layers.erase(name) > 0;
}

std::vector<Layer> LayerTable::all() const {
    std::vector<Layer> out;
    out.reserve(m_layers.size());
    for (const auto& kv : m_layers) out.push_back(kv.second);
    return out;
}

} // namespace cad
