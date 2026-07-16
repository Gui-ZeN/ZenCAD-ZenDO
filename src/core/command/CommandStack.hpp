// src/core/command/CommandStack.hpp
#pragma once
#include "core/command/Command.hpp"
#include <memory>
#include <vector>

namespace cad {

// Duas pilhas (undo/redo). push() limpa o redo (novo ramo de histórico).
// O dono de cada comando migra entre as pilhas — nunca há cópia nem leak.
// O histórico é LIMITADO (kMaxHistory): ao estourar, o comando mais antigo é
// descartado — evita crescimento sem teto da memória (mementos guardam clones).
class CommandStack {
public:
    static constexpr std::size_t kMaxHistory = 256;

    void push(std::unique_ptr<Command> cmd);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    // Move o topo de undo->redo e devolve o ponteiro (dono passa a ser o redo).
    Command* popForUndo();
    // Move o topo de redo->undo e devolve o ponteiro (dono passa a ser o undo).
    Command* popForRedo();

    void clear();

    // --- Marcas de undo (UNDO Mark / Back, à la AutoCAD) --------------------
    // mark(): marca a PROFUNDIDADE atual da pilha de undo. hasMark(): há marca
    // acima de zero comandos? undoDepth(): nº de comandos desfazíveis.
    // popMarkDepth(): consome a última marca e devolve a profundidade marcada
    // (o chamador desfaz até undoDepth() == valor devolvido).
    void        mark() { m_marks.push_back(m_undo.size()); }
    bool        hasMark() const { return !m_marks.empty(); }
    std::size_t undoDepth() const { return m_undo.size(); }
    std::size_t popMarkDepth() {
        if (m_marks.empty()) return 0;
        const std::size_t d = m_marks.back();
        m_marks.pop_back();
        return d;
    }

private:
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    std::vector<std::size_t>              m_marks;   // profundidades marcadas
};

} // namespace cad
