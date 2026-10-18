// SPDX-License-Identifier: GPL-2.0-or-later
#include "material_snapshot.h"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace Gpu::Metal::MaterialSnapshot {
namespace {
using Bytes=std::vector<std::uint8_t>;
std::uint64_t Hash(const Bytes& data) {
    std::uint64_t h=0xcbf29ce484222325ull;
    for (auto b:data) h=(h^b)*0x100000001b3ull;
    return h;
}
struct Writer {
    Bytes bytes;
    void U32(std::uint32_t n) { for(unsigned k=0;k<4;++k) bytes.push_back(n>>(k*8)); }
    void U64(std::uint64_t n) { for(unsigned k=0;k<8;++k) bytes.push_back(n>>(k*8)); }
    void Blob(const Bytes& b) {
        if(b.size()>MaxFileBytes || bytes.size()+b.size()+4>MaxFileBytes) throw std::runtime_error("Snapshot is too large.");
        U32(static_cast<std::uint32_t>(b.size()));bytes.insert(bytes.end(),b.begin(),b.end());
    }
};
struct Reader {
    const Bytes& bytes;std::size_t offset=0;
    void Need(std::size_t n) const { if(n>bytes.size()-offset) throw std::runtime_error("Truncated snapshot."); }
    std::uint32_t U32() { Need(4);std::uint32_t n=0;for(unsigned k=0;k<4;++k)n|=std::uint32_t(bytes[offset++])<<(k*8);return n; }
    std::uint64_t U64() { Need(8);std::uint64_t n=0;for(unsigned k=0;k<8;++k)n|=std::uint64_t(bytes[offset++])<<(k*8);return n; }
    Bytes Blob(std::size_t limit) {
        const auto n=U32();if(n>limit)throw std::runtime_error("Snapshot field exceeds its size limit.");Need(n);
        Bytes b(bytes.begin()+offset,bytes.begin()+offset+n);offset+=n;return b;
    }
};
void Validate(const Texture& t) {
    // Metal RGBA8Unorm / RGBA8Unorm_sRGB; stored bytes include alpha.
    if(!t.width || !t.height || t.width>1024 || t.height>1024 ||
       (t.format!=70 && t.format!=71) || t.mips.empty() ||
       t.mips.size()>std::size_t(std::bit_width(std::max(t.width,t.height))))
        throw std::runtime_error("Unsupported snapshot texture layout.");
    for(std::size_t k=0;k<t.mips.size();++k)
        if(t.mips[k].size()!=std::size_t(std::max(t.width>>k,1u))*std::max(t.height>>k,1u)*4)
            throw std::runtime_error("Invalid snapshot mip size.");
}
void TextureOut(Writer& w,const Texture& t) {
    Validate(t);w.U32(t.width);w.U32(t.height);w.U32(t.format);w.U32(t.mips.size());
    for(const auto& mip:t.mips)w.Blob(mip);
}
Texture TextureIn(Reader& r) {
    Texture t;t.width=r.U32();t.height=r.U32();t.format=r.U32();const auto levels=r.U32();
    if(levels>11)throw std::runtime_error("Invalid snapshot mip count.");
    for(unsigned k=0;k<levels;++k)t.mips.push_back(r.Blob(4u<<20));
    Validate(t);return t;
}
void Validate(const Snapshot& s) {
    if(s.game_hash.empty() || s.game_hash.size()>128 || s.materials.empty() || s.materials.size()>MaxMaterials)
        throw std::runtime_error("Invalid snapshot material count or game identity.");
    std::set<std::uint64_t> keys;
    for(const auto& m:s.materials) {
        if(!m.key || !m.shader_hash || !keys.insert(m.key).second || m.memory.size()>32)
            throw std::runtime_error("Invalid or duplicate snapshot material.");
        Validate(m.colour);Validate(m.surface);
        if(m.colour.width!=m.surface.width || m.colour.height!=m.surface.height)
            throw std::runtime_error("Mismatched stain textures.");
        for(const auto& b:m.memory)
            if(b.bytes.size()>(1u<<20) || b.address>std::numeric_limits<std::uint64_t>::max()-b.bytes.size())
                throw std::runtime_error("Invalid snapshot memory range.");
    }
}
void WriteTemporary(const std::filesystem::path& path,const Bytes& bytes,std::filesystem::path& temporary) {
    auto pattern=path.string()+".tmp-XXXXXX";
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    const int fd=mkstemp(name.data());
    if(fd<0)throw std::runtime_error("Could not create snapshot temporary file.");
    temporary=name.data();std::size_t offset=0;bool ok=true;
    while(offset<bytes.size()) {
        const auto n=write(fd,bytes.data()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){ok=false;break;}
        offset+=n;
    }
    if(ok && fsync(fd)!=0)ok=false;
    if(close(fd)!=0)ok=false;
    if(!ok)throw std::runtime_error("Could not finish writing snapshot.");
}
}
std::uint64_t Key(std::uint64_t shader,std::uint64_t normal,std::uint64_t diffuse) {
    if(!shader || !normal || !diffuse)return 0;
    const auto mix=[](std::uint64_t x) { x^=x>>30;x*=0xbf58476d1ce4e5b9ull;x^=x>>27;x*=0x94d049bb133111ebull;return x^(x>>31); };
    return mix(shader)^std::rotl(mix(normal),17)^std::rotl(mix(diffuse),39);
}
bool HasStains(const Texture& t) {
    if(t.mips.empty())return false;
    const auto& b=t.mips.front();
    for(std::size_t i=0;i+3<b.size();i+=4)
        if(b[i] || b[i+1] || b[i+2] || b[i+3]!=255)return true;
    return false;
}
bool Save(const std::filesystem::path& path,const Snapshot& snapshot,std::string& error) {
    std::filesystem::path temporary,backup_temporary;
    try {
        Validate(snapshot);Writer w;
        const std::string magic="BBMAT001";w.bytes.assign(magic.begin(),magic.end());
        w.U32(1);w.Blob(Bytes(snapshot.game_hash.begin(),snapshot.game_hash.end()));w.U32(snapshot.materials.size());
        for(const auto& m:snapshot.materials) {
            w.U64(m.key);w.U64(m.shader_hash);TextureOut(w,m.colour);TextureOut(w,m.surface);
            for(auto n:m.vertex_data)w.U32(n);for(auto n:m.pixel_data)w.U32(n);
            w.U32(m.memory.size());for(const auto& b:m.memory){w.U64(b.address);w.Blob(b.bytes);}
        }
        w.U64(Hash(w.bytes));
        if(w.bytes.size()>MaxFileBytes)throw std::runtime_error("Snapshot is too large.");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        // Exclusive temporary files and atomic renames also avoid following
        // an existing backup symlink when retaining the previous fixture.
        WriteTemporary(path,w.bytes,temporary);
        if(std::filesystem::exists(path)) {
            const auto old_size=std::filesystem::file_size(path);
            if(old_size>MaxFileBytes)throw std::runtime_error("Previous snapshot exceeds its size limit.");
            Bytes old(old_size);std::ifstream previous(path,std::ios::binary);
            if(!previous.read(reinterpret_cast<char*>(old.data()),old.size()))
                throw std::runtime_error("Could not retain previous snapshot.");
            const std::filesystem::path backup=path.string()+".previous";
            WriteTemporary(backup,old,backup_temporary);
            std::filesystem::rename(backup_temporary,backup);
        }
        std::filesystem::rename(temporary,path);error.clear();return true;
    } catch(const std::exception& e) {
        if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
        if(!backup_temporary.empty()){std::error_code ignored;std::filesystem::remove(backup_temporary,ignored);}
        error=e.what();return false;
    }
}
bool Load(const std::filesystem::path& path,const std::string& game_hash,Snapshot& out,std::string& error) {
    try {
        if(game_hash.empty())throw std::runtime_error("A prepared game build identity is required.");
        const auto size=std::filesystem::file_size(path);
        if(size<24 || size>MaxFileBytes)throw std::runtime_error("Invalid snapshot file size.");
        Bytes bytes(size);std::ifstream input(path,std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Could not read snapshot.");
        Reader checksum{bytes,bytes.size()-8};const auto expected=checksum.U64();bytes.resize(bytes.size()-8);
        if(Hash(bytes)!=expected)throw std::runtime_error("Snapshot checksum failed.");
        if(std::memcmp(bytes.data(),"BBMAT001",8))throw std::runtime_error("Not a material snapshot.");
        Reader r{bytes,8};if(r.U32()!=1)throw std::runtime_error("Unsupported snapshot version.");
        Snapshot s;const auto identity=r.Blob(128);s.game_hash.assign(identity.begin(),identity.end());
        if(s.game_hash!=game_hash)throw std::runtime_error("Snapshot belongs to a different prepared game build.");
        const auto count=r.U32();if(count>MaxMaterials)throw std::runtime_error("Too many snapshot materials.");
        for(unsigned k=0;k<count;++k) {
            Material m;m.key=r.U64();m.shader_hash=r.U64();m.colour=TextureIn(r);m.surface=TextureIn(r);
            for(auto& n:m.vertex_data)n=r.U32();for(auto& n:m.pixel_data)n=r.U32();
            const auto blocks=r.U32();if(blocks>32)throw std::runtime_error("Too many snapshot memory blocks.");
            for(unsigned b=0;b<blocks;++b){MemoryBlock block;block.address=r.U64();block.bytes=r.Blob(1u<<20);m.memory.push_back(std::move(block));}
            s.materials.push_back(std::move(m));
        }
        if(r.offset!=bytes.size())throw std::runtime_error("Unexpected data after snapshot.");
        Validate(s);out=std::move(s);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
