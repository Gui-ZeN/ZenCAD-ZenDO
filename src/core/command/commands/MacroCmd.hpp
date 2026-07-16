// src/core/command/commands/MacroCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cad {

// Agrupa vários comandos num único passo de undo/redo. Os sub-comandos são
// executados na ordem de inserção e desfeitos na ordem inversa. NÃO chame
// doc.execute() nos sub-comandos — adicione-os aqui e execute SÓ o MacroCmd.
class MacroCmd final : public Command {
public:
    explicit MacroCmd(std::string label = "MACRO") : m_label(std::move(label)) {}

    void add(std::unique_ptr<Command> c) { if (c) m_cmds.push_back(std::move(c)); }
    bool empty() const { return m_cmds.empty(); }
    std::size_t size() const { return m_cmds.size(); }

    void execute(DrawingManager& doc) override {
        for (auto& c : m_cmds) c->execute(doc);
    }
    void undo(DrawingManager& doc) override {
        for (auto it = m_cmds.rbegin(); it != m_cmds.rend(); ++it) (*it)->undo(doc);
    }
    std::string label() const override { return m_label; }

private:
    std::vector<std::unique_ptr<Command>> m_cmds;
    std::string m_label;
};

} // namespace cad
