#pragma once

#include <cstdint>
#include <map>
#include <string>

// A title's param.sfo (sce_sys/param.sfo): APP_VER, CATEGORY, TITLE_ID and the
// USER_DEFINED_PARAM_n integers. Each entry is text or a 32-bit integer.
struct SfoValue {
    bool is_int = false;
    std::uint32_t num = 0;
    std::string text;
};

// Reads the file at path. False, with *error set, when it cannot be read or is
// not an SFO.
bool sfo_read(const std::string& path, std::map<std::string, SfoValue>* out, std::string* error);
