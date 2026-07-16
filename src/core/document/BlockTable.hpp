// src/core/document/BlockTable.hpp
// Biblioteca de blocos nomeados (à la AutoCAD BLOCK): cada definição guarda a
// geometria-mestre em coordenadas LOCAIS (ponto-base na origem). As inserções
// (BlockRef) são criadas clonando a definição e aplicando a transformação de
// inserção. Sem Qt: testável headless.
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace cad {

// Definição de ATRIBUTO dentro de um bloco (extraída dos AttDef ao criar):
// campo nomeado que cada inserção preenche com um valor próprio.
struct AttDefSpec {
    std::string tag;        // nome do campo (ex.: "AMBIENTE")
    std::string prompt;     // pergunta feita no INSERT (ex.: "Nome do ambiente?")
    std::string defValue;   // valor padrão
    Point3      pos{};      // posição em coords LOCAIS do bloco
    double      height{2.5};
};

// Definição de um bloco nomeado: geometria-mestre em coords locais.
struct BlockDefinition {
    std::string             name;
    Point3                  base{};      // ponto-base original (informativo)
    std::vector<EntityPtr>  members;     // geometria em coords LOCAIS (base na origem)
    std::vector<AttDefSpec> attdefs;     // atributos (tags preenchíveis por inserção)

    BlockDefinition() = default;
    BlockDefinition(BlockDefinition&&) noexcept = default;
    BlockDefinition& operator=(BlockDefinition&&) noexcept = default;

    // Cópia = clone profundo (unique_ptr não é copiável trivialmente).
    BlockDefinition(const BlockDefinition& o)
        : name(o.name), base(o.base), attdefs(o.attdefs) {
        members.reserve(o.members.size());
        for (const EntityPtr& m : o.members) members.push_back(m->clone());
    }
    BlockDefinition& operator=(const BlockDefinition& o) {
        if (this == &o) return *this;
        name = o.name; base = o.base; attdefs = o.attdefs;
        members.clear(); members.reserve(o.members.size());
        for (const EntityPtr& m : o.members) members.push_back(m->clone());
        return *this;
    }

    std::vector<EntityPtr> cloneMembers() const {
        std::vector<EntityPtr> out;
        out.reserve(members.size());
        for (const EntityPtr& m : members) out.push_back(m->clone());
        return out;
    }
};

// Tabela de blocos nomeados (biblioteca do documento).
class BlockTable {
public:
    // Registra/atualiza uma definição por nome (redefinição sobrescreve).
    void add(BlockDefinition def) { m_defs[def.name] = std::move(def); }

    const BlockDefinition* find(const std::string& name) const {
        auto it = m_defs.find(name);
        return it == m_defs.end() ? nullptr : &it->second;
    }
    BlockDefinition* find(const std::string& name) {
        auto it = m_defs.find(name);
        return it == m_defs.end() ? nullptr : &it->second;
    }
    bool contains(const std::string& name) const { return m_defs.count(name) != 0; }
    bool remove(const std::string& name) { return m_defs.erase(name) != 0; }

    std::vector<std::string> names() const {
        std::vector<std::string> v;
        v.reserve(m_defs.size());
        for (const auto& kv : m_defs) v.push_back(kv.first);
        return v;
    }
    std::size_t size()  const { return m_defs.size(); }
    bool        empty() const { return m_defs.empty(); }

private:
    std::map<std::string, BlockDefinition> m_defs;
};

} // namespace cad
