// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal bbnet symbol resolver ABI for the macOS network adapter.
#pragma once
#include "common/types.h"
namespace Core::Loader {
enum class SymbolType { Function,Object };
class SymbolsResolver {
public:
    void AddSymbol(const char*,const char*,const char*,SymbolType,u64);
};
}
#define LIB_FUNCTION(nid,lib,version,module,function) sym->AddSymbol(nid,lib,module,Core::Loader::SymbolType::Function,reinterpret_cast<u64>(function))
