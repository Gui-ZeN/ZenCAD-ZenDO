// src/core/command/CommandStack.cpp
#include "core/command/CommandStack.hpp"

namespace cad {

void CommandStack::push(std::unique_ptr<Command> cmd) {
    m_undo.push_back(std::move(cmd));
    m_redo.clear();  // executar uma nova ação invalida o ramo de redo
    if (m_undo.size() > kMaxHistory) {                     // teto do histórico:
        m_undo.erase(m_undo.begin());                      // descarta o mais antigo
        // As marcas apontam profundidades: o descarte desloca tudo 1 p/ trás.
        for (std::size_t& d : m_marks) d = (d > 0) ? d - 1 : 0;
    }
}

Command* CommandStack::popForUndo() {
    if (m_undo.empty()) return nullptr;
    std::unique_ptr<Command> cmd = std::move(m_undo.back());
    m_undo.pop_back();
    Command* raw = cmd.get();
    m_redo.push_back(std::move(cmd));
    return raw;
}

Command* CommandStack::popForRedo() {
    if (m_redo.empty()) return nullptr;
    std::unique_ptr<Command> cmd = std::move(m_redo.back());
    m_redo.pop_back();
    Command* raw = cmd.get();
    m_undo.push_back(std::move(cmd));
    return raw;
}

void CommandStack::clear() {
    m_undo.clear();
    m_redo.clear();
    m_marks.clear();
}

} // namespace cad
