// src/core/document/DrawingManager.hpp
#pragma once
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/geometry/Entity.hpp"
#include "core/spatial/ISpatialIndex.hpp"
#include "core/document/LayerTable.hpp"
#include "core/document/BlockTable.hpp"
#include "core/command/CommandStack.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Ray.hpp"

namespace cad {

class Command; // fwd

// ============================================================================
//  DrawingManager — O DOCUMENTO. Fonte única da verdade.
//  Responsabilidades:
//    * possuir as entidades (ownership via unique_ptr);
//    * manter o índice espacial sincronizado;
//    * tabela de camadas (Layers);
//    * executar Commands e expor undo/redo.
//  NÃO depende de Qt nem de OpenGL — testável headless.
// ============================================================================
class DrawingManager {
public:
    explicit DrawingManager(std::unique_ptr<ISpatialIndex> index);
    ~DrawingManager();

    DrawingManager(const DrawingManager&)            = delete;
    DrawingManager& operator=(const DrawingManager&) = delete;
    DrawingManager(DrawingManager&&) noexcept            = default;
    DrawingManager& operator=(DrawingManager&&) noexcept = default;

    // --- Mutação da base (prefira via Commands para ter undo) -------------
    // Zera o DOCUMENTO inteiro (Novo/Abrir projeto): entidades, camadas (volta
    // à "0"), biblioteca de blocos e histórico. NÃO tem undo.
    void      clearAll();
    EntityId  addEntity(EntityPtr entity);     // atribui id, indexa, assume posse
    EntityPtr removeEntity(EntityId id);       // tira da base/índice, devolve posse
    void      reinsert(EntityPtr entity);      // reinsere preservando o id (undo)
    EntityPtr replaceEntity(EntityId id, EntityPtr entity);  // troca a geometria, mantém o id; devolve a antiga

    // --- Consulta ---------------------------------------------------------
    Entity*       getEntity(EntityId id);
    const Entity* getEntity(EntityId id) const;
    std::size_t   count() const noexcept { return m_entities.size(); }

    std::vector<EntityId> query(const AABB& region) const;     // janela/frustum
    EntityId pick(const Ray& pickRay, double tol) const;       // picking exato
    void     markDirty(EntityId id);                           // reindexa após transform

    // --- Camadas ----------------------------------------------------------
    LayerTable&       layers() noexcept       { return m_layers; }
    const LayerTable& layers() const noexcept { return m_layers; }

    // ESTADOS DE CAMADA nomeados (snapshot completo da tabela; aplicar
    // restaura on/frozen/locked/cor/tipo/espessura/transparência de todas).
    const std::map<std::string, std::vector<Layer>>& layerStates() const { return m_layerStates; }
    void saveLayerState(const std::string& name) {
        if (!name.empty()) m_layerStates[name] = m_layers.all();
    }
    void addLayerState(const std::string& name, std::vector<Layer> ls) {   // load
        if (!name.empty() && !ls.empty()) m_layerStates[name] = std::move(ls);
    }
    bool applyLayerState(const std::string& name) {
        const auto it = m_layerStates.find(name);
        if (it == m_layerStates.end()) return false;
        for (const Layer& l : it->second)
            if (m_layers.contains(l.name)) m_layers.add(l);   // atualiza existentes
        m_fullDirty = true;
        return true;
    }
    bool removeLayerState(const std::string& name) { return m_layerStates.erase(name) > 0; }

    // Gestão de ciclo de vida (sem undo — operações de organização):
    std::size_t layerUsage(const std::string& name) const;   // nº de entidades na camada
    // Renomeia a camada e REMAPEIA as entidades (documento + membros de blocos).
    bool renameLayer(const std::string& oldName, const std::string& newName);
    // Remove a camada; se em uso e moveToDefault, move as entidades para a "0",
    // senão recusa. Nunca remove a "0".
    bool removeLayer(const std::string& name, bool moveToDefault);
    // Remove todas as camadas sem entidades (exceto "0" e `keep`, ex.: a
    // corrente). Devolve os nomes removidos.
    std::vector<std::string> purgeLayers(const std::string& keep);

    // --- Blocos (biblioteca de definições nomeadas) -----------------------
    BlockTable&       blocks() noexcept       { return m_blocks; }
    const BlockTable& blocks() const noexcept { return m_blocks; }

    // --- UCS 2D (origem + rotação de TRABALHO) ------------------------------
    // "Lente" de entrada/leitura: coordenadas digitadas, ortho, polar, grade e
    // o X/Y da status bar operam no frame do UCS; a geometria segue no mundo.
    const Point3& ucsOrigin()  const noexcept { return m_ucsOrigin; }
    double        ucsAngleRad() const noexcept { return m_ucsAngle; }
    bool ucsActive() const noexcept {
        return std::abs(m_ucsAngle) > 1e-12 ||
               std::abs(m_ucsOrigin.x) > 1e-12 || std::abs(m_ucsOrigin.y) > 1e-12;
    }
    void setUcs(const Point3& origin, double angleRad) {
        m_ucsOrigin = origin;
        m_ucsAngle  = angleRad;
    }
    Point3 ucsToWorld(const Point3& p) const {
        const double c = std::cos(m_ucsAngle), s = std::sin(m_ucsAngle);
        return {m_ucsOrigin.x + p.x * c - p.y * s,
                m_ucsOrigin.y + p.x * s + p.y * c, 0.0};
    }
    Point3 worldToUcs(const Point3& p) const {
        const double c = std::cos(m_ucsAngle), s = std::sin(m_ucsAngle);
        const double dx = p.x - m_ucsOrigin.x, dy = p.y - m_ucsOrigin.y;
        return {dx * c + dy * s, -dx * s + dy * c, 0.0};
    }
    Point3 ucsDirToWorld(const Point3& d) const {   // só rotação (deltas @dx,dy)
        const double c = std::cos(m_ucsAngle), s = std::sin(m_ucsAngle);
        return {d.x * c - d.y * s, d.x * s + d.y * c, 0.0};
    }

    // --- XREF (referências externas): bloco -> caminho do .zencad ----------
    // O kernel só guarda o VÍNCULO (nome da definição -> caminho, de
    // preferência relativo ao projeto); quem lê o arquivo e reconstrói a
    // definição/inserções é a camada de I/O do app (reload ao abrir/comando).
    const std::map<std::string, std::string>& xrefs() const { return m_xrefs; }
    void addXref(const std::string& name, const std::string& path) {
        if (!name.empty() && !path.empty()) m_xrefs[name] = path;
    }
    bool removeXref(const std::string& name) { return m_xrefs.erase(name) > 0; }

    // --- Grupos nomeados (GROUP): selecionar um membro seleciona o grupo ---
    const std::map<std::string, std::vector<EntityId>>& groups() const { return m_groups; }
    void addGroup(const std::string& name, std::vector<EntityId> ids) {
        if (!name.empty() && !ids.empty()) m_groups[name] = std::move(ids);
    }
    bool removeGroup(const std::string& name) { return m_groups.erase(name) > 0; }
    // Grupo que contém `id` (nullptr = nenhum). Primeiro que casar.
    const std::vector<EntityId>* groupOf(EntityId id) const {
        for (const auto& kv : m_groups)
            for (const EntityId g : kv.second)
                if (g == id) return &kv.second;
        return nullptr;
    }
    const std::string* groupNameOf(EntityId id) const {
        for (const auto& kv : m_groups)
            for (const EntityId g : kv.second)
                if (g == id) return &kv.first;
        return nullptr;
    }

    // Último id atribuído por addEntity (seleção "Last"; kInvalidId = nenhum).
    EntityId lastAssignedId() const noexcept {
        return m_nextId > 1 ? m_nextId - 1 : kInvalidId;
    }

    // --- Histórico (Command + Memento) ------------------------------------
    void execute(std::unique_ptr<Command> cmd);
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    void undo();
    void redo();

    // Marcas de undo (UNDO Mark/Back): undoMark() marca o estado atual;
    // undoBack() desfaz TUDO até a última marca (devolve o nº de comandos
    // desfeitos; 0 = sem marca ou nada a desfazer).
    void        undoMark() { m_history.mark(); }
    std::size_t undoBack();

    // OOPS: restaura o ÚLTIMO conjunto apagado (clones guardados pelo
    // EraseCmd) sem mexer no restante do histórico. Devolve o nº restaurado.
    void        setLastErased(std::vector<EntityPtr> clones) { m_lastErased = std::move(clones); }
    std::size_t oops();

    // Iteração estável e barata (render/serialização).
    template <class Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [id, ptr] : m_entities) fn(*ptr);
    }

    // --- Regen incremental (dirty-tracking p/ o render) --------------------
    // Ids cuja geometria mudou desde o último consumo (add/remove/replace/
    // reinsert/markDirty anotam aqui). O viewport usa isso para invalidar só
    // as entidades alteradas no cache de tesselação, em vez de re-emitir o
    // documento inteiro a cada edição.
    const std::unordered_set<EntityId>& dirtyIds() const noexcept { return m_dirty; }
    bool fullDirty()     const noexcept { return m_fullDirty; }
    void invalidateAll()       noexcept { m_fullDirty = true; }   // mudança em massa
    void clearDirty() { m_dirty.clear(); m_fullDirty = false; }   // consumido pelo render
    // Pós-LOAD: com os vínculos recém-religados (âncoras de cota/fontes de
    // hachura), força um regen completo — arquivo consistente é no-op; arquivo
    // com geometria defasada se auto-corrige.
    void regenAssociativeNow() { m_fullDirty = true; regenAssociativeDims(); }

    // --- ESCALA DE ANOTAÇÃO (CANNOSCALE) -----------------------------------
    // mm de papel por unidade de modelo. Texto/cota ANOTATIVOS têm a altura de
    // modelo derivada dela (altura_modelo = mm_no_papel / escala); trocar a
    // escala re-deriva as alturas (a altura em mm de papel é preservada).
    double annoMmPerUnit() const noexcept { return m_annoMmPerUnit; }
    void   setAnnoMmPerUnit(double mm) { if (mm > 1e-12) m_annoMmPerUnit = mm; }  // load
    std::size_t applyAnnotationScale(double newMmPerUnit);   // regen; retorna nº afetadas

private:
    void noteDirty(EntityId id) { m_dirty.insert(id); }
    // Cotas ASSOCIATIVAS: recalcula os pontos de definição das cotas cuja
    // entidade-fonte está no conjunto sujo (rodado após execute/undo/redo).
    void regenAssociativeDims();

    EntityId m_nextId{1};
    std::unordered_map<EntityId, EntityPtr> m_entities;
    std::unique_ptr<ISpatialIndex>          m_spatialIndex;
    LayerTable                              m_layers;
    BlockTable                              m_blocks;
    std::map<std::string, std::vector<EntityId>> m_groups;   // grupos nomeados
    std::map<std::string, std::string>           m_xrefs;    // XREF: bloco -> caminho
    std::map<std::string, std::vector<Layer>>    m_layerStates;   // estados nomeados
    std::vector<EntityPtr>                  m_lastErased;    // clones p/ OOPS
    CommandStack                            m_history;
    std::unordered_set<EntityId>            m_dirty;
    bool                                    m_fullDirty{true};   // 1º upload emite tudo
    double                                  m_annoMmPerUnit{1.0};   // CANNOSCALE
    Point3                                  m_ucsOrigin{};          // UCS 2D
    double                                  m_ucsAngle{0.0};        // rad, CCW
};

} // namespace cad
