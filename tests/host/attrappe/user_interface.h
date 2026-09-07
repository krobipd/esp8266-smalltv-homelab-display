// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
// Feste Chip-ID: Die Ableitung muss nur deterministisch sein, nicht echt.
inline auto system_get_chip_id() -> uint32_t { return 0x00ABCDEF; }
