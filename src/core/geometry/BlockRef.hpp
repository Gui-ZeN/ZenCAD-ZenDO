// src/core/geometry/BlockRef.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Matrix4.hpp"
#include <set>
#include <vector>
#include <string>

namespace cad {

struct BlockDefinition;   // core/document/BlockTable.hpp

// Inserção de bloco (INSERT): agrupa várias entidades (a "definição", em
// coordenadas locais) e as posiciona no desenho por uma transformação de
// inserção `m_xform`. Comporta-se como uma única entidade (seleção, snap,
// transformação) — emitindo/testando seus membros já transformados, sem mutá-los.
class BlockRef final : public Entity {
public:
    BlockRef() = default;
    BlockRef(std::vector<EntityPtr> members, const Matrix4& xform)
        : m_members(std::move(members)), m_xform(xform) {}

    // Monta um bloco a partir de entidades existentes: clona cada uma, leva o
    // ponto-base para a origem local, e define a inserção de volta em `base`.
    static std::unique_ptr<BlockRef> fromEntities(const std::vector<const Entity*>& ents,
                                                  const Point3& base);

    // Instancia um bloco a partir de uma definição nomeada: clona a geometria
    // (já em coords locais) e aplica a transformação de inserção.
    static std::unique_ptr<BlockRef> fromDefinition(const BlockDefinition& def,
                                                    const Matrix4& insertXform);

    const std::vector<EntityPtr>& members() const { return m_members; }
    const Matrix4& xform() const { return m_xform; }
    const std::string& blockName() const { return m_blockName; }
    void setBlockName(std::string n) { m_blockName = std::move(n); }

    // Valores de ATRIBUTO desta inserção (tag -> valor + posição local).
    // Preenchidos a partir dos AttDefSpec da definição; renderizados no emitTo.
    struct AttValue { std::string tag, value; Point3 pos{}; double height{2.5}; };
    const std::vector<AttValue>& attValues() const { return m_attValues; }
    void setAttValues(std::vector<AttValue> v) { m_attValues = std::move(v); }
    void setAttValue(const std::string& tag, const std::string& value) {
        for (AttValue& a : m_attValues) if (a.tag == tag) { a.value = value; return; }
    }

    // ESTADOS DE VISIBILIDADE por inserção: camadas INTERNAS do bloco ocultas
    // nesta inserção (membros nessas camadas não são emitidos/testados). É o
    // "bloco dinâmico" v1: desenhe as variações em camadas e ligue/desligue
    // por inserção (porta esq/dir, janela em 3 larguras...).
    const std::set<std::string>& hiddenLayers() const { return m_hidden; }
    void setHiddenLayers(std::set<std::string> h) { m_hidden = std::move(h); }
    bool memberVisible(const Entity& m) const {
        return m_hidden.find(m.layer()) == m_hidden.end();
    }

    // Clones dos membros JÁ transformados pela inserção (para explodir o bloco).
    std::vector<EntityPtr> explodedClones() const;

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "INSERT"; }

private:
    std::vector<EntityPtr> m_members;             // geometria em coords LOCAIS
    Matrix4                m_xform{Matrix4::identity()};  // transformação de inserção
    std::string            m_blockName;            // nome da definição (vazio = anônimo)
    std::vector<AttValue>  m_attValues;            // valores de atributo desta inserção
    std::set<std::string>  m_hidden;               // camadas internas ocultas (estados)
};

} // namespace cad
