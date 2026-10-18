// From Software shader bundles: DCX (zlib) wrapped BND4 archives holding
// Sony shader containers (.vpo/.ppo/.cpo/.hpo/.dpo/.gpo).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gcn {

struct BundleEntry {
    std::string name;   // Windows-style path from the archive
    std::uint32_t id = 0;
    std::vector<std::uint8_t> data;
};

// Inflate a DCX/DFLT file (returns the input unchanged when not DCX).
std::vector<std::uint8_t> dcx_decompress(const std::vector<std::uint8_t>& data, std::string* error = nullptr);
// The reverse: a DFLT DCX as the game's files are (they are zlib level 9; the
// game reads any level); empty on failure.
std::vector<std::uint8_t> dcx_compress(const std::vector<std::uint8_t>& raw, int level = 9);
// Parse a BND4 archive (already decompressed) into entries.
bool bnd4_entries(const std::vector<std::uint8_t>& b, std::vector<BundleEntry>& out, std::string* error = nullptr);

// A BND4 archive kept whole for writing back: the header, and per file its
// flags, id, name as stored (UTF-16 or bytes, without the terminator) and data.
struct Bnd4File {
    std::uint64_t flags = 0;
    std::uint32_t id = 0;
    std::vector<std::uint8_t> name;
    std::vector<std::uint8_t> data;
};
struct Bnd4Archive {
    std::vector<std::uint8_t> head;  // the first 0x40 bytes
    std::uint64_t entry_size = 0;
    bool unicode = false;
    std::vector<Bnd4File> files;
};
bool bnd4_read(const std::vector<std::uint8_t>& b, Bnd4Archive& out, std::string* error = nullptr);
// Each file's data at a 16-byte aligned offset, as the game's archives are laid
// out: the dump's talk-script archives pack back byte for byte.
std::vector<std::uint8_t> bnd4_pack(const Bnd4Archive& a);

// Sony shader container: 0x24-byte file header, 0x50-byte "Shdr" header, GCN
// code, "OrbShdr" footer. Returns the code slice (dword aligned) or false.
struct ShaderCode {
    std::vector<std::uint32_t> words;
    std::size_t offset = 0;  // byte offset of the code in the container
    std::size_t footer = 0;  // byte offset of the 'OrbShdr' ShaderBinaryInfo
    std::uint8_t type = 0;   // ShaderFileHeader type byte
};
bool shader_code(const std::vector<std::uint8_t>& container, ShaderCode& out);

// Load a file into memory.
bool read_file(const std::string& path, std::vector<std::uint8_t>& out);

}  // namespace gcn
