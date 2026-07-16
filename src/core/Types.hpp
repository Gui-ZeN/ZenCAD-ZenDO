// src/core/Types.hpp
#pragma once
#include <cstdint>

namespace cad {

// Identificador estável de entidade dentro de um documento.
using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidId = 0;

} // namespace cad
