// src/core/document/LayerTable.hpp
#pragma once
#include "core/document/Layer.hpp"
#include <map>
#include <string>
#include <vector>

namespace cad {

// Tabela de camadas do documento. A camada "0" sempre existe (padrão AutoCAD).
class LayerTable {
public:
    void   ensureDefaultLayer();
    Layer& add(const Layer& layer);                 // insere ou atualiza
    const Layer* find(const std::string& name) const;
    Layer*       find(const std::string& name);
    bool         contains(const std::string& name) const;
    std::size_t  count() const { return m_layers.size(); }
    std::vector<Layer> all() const;          // todas as camadas (para a UI)

    // Renomeia preservando as propriedades. Recusa: "0", nome inexistente,
    // destino já existente. (O remap das entidades é do DrawingManager.)
    bool rename(const std::string& oldName, const std::string& newName);
    // Remove a camada. Recusa a "0". (Checar uso é de quem chama.)
    bool remove(const std::string& name);

private:
    std::map<std::string, Layer> m_layers;
};

} // namespace cad
