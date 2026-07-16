// src/core/command/Command.hpp
#pragma once
#include <string>

namespace cad {

class DrawingManager;

// Padrão Command: encapsula uma ação reversível. Cada comando guarda o estado
// necessário para se desfazer (Memento embutido), permitindo undo/redo infinito.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute(DrawingManager& doc) = 0;
    virtual void undo(DrawingManager& doc) = 0;
    virtual std::string label() const = 0;
};

} // namespace cad
