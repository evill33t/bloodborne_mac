// SPDX-License-Identifier: GPL-2.0-or-later
#include "archive.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

namespace Bb::Checkpoint {
namespace {
using Bytes=std::vector<std::uint8_t>;
constexpr std::size_t HeaderBytes=176, EntryBytes=48;
constexpr std::array<std::uint8_t,8> Magic{'B','B','S','T','A','T','E','1'};
struct Fd {
    int value=-1;
    explicit Fd(int n):value(n) {}
    ~Fd() { if(value>=0)close(value); }
    Fd(const Fd&)=delete;
};
void Fail(const char* what) { throw std::runtime_error(what); }
std::uint64_t Hash(std::span<const std::uint8_t> b) {
    std::uint64_t h=0xcbf29ce484222325ull;
    for(auto byte:b)h=(h^byte)*0x100000001b3ull;
    return h;
}
void HashMore(std::uint64_t& h,std::span<const std::uint8_t> b) {for(auto byte:b)h=(h^byte)*0x100000001b3ull;}
void Put(Bytes& b,std::size_t at,std::uint64_t n,unsigned count=8) {
    for(unsigned k=0;k<count;++k)b.at(at+k)=std::uint8_t(n>>(k*8));
}
std::uint64_t Get(std::span<const std::uint8_t> b,std::size_t at,unsigned count=8) {
    if(at>b.size() || count>b.size()-at)Fail("Truncated checkpoint field.");
    std::uint64_t n=0;
    for(unsigned k=0;k<count;++k)n|=std::uint64_t(b[at+k])<<(k*8);
    return n;
}
bool Known(SectionKind kind) {
    const auto n=static_cast<std::uint32_t>(kind);
    return (n>=1 && n<=10) || kind==SectionKind::Fixture;
}
void CheckIdentity(const std::string& identity) {
    if(identity.empty() || identity.size()>128 || identity.find('\0')!=std::string::npos)
        Fail("Invalid checkpoint build identity.");
}
void CheckLimits(Limits limits) {
    if(limits.file_bytes<HeaderBytes || limits.file_bytes>std::uint64_t(std::numeric_limits<off_t>::max()) ||
       !limits.section_bytes || limits.section_bytes>limits.file_bytes || !limits.sections || limits.sections>65536)
        Fail("Invalid checkpoint size limits.");
}
void Read(int fd,std::uint64_t offset,std::span<std::uint8_t> bytes) {
    while(!bytes.empty()) {
        const auto n=pread(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset));
        if(n<0 && errno==EINTR)continue;
        if(n<=0)Fail("Could not read complete checkpoint.");
        offset+=std::uint64_t(n);bytes=bytes.subspan(static_cast<std::size_t>(n));
    }
}
void Write(int fd,std::span<const std::uint8_t> bytes) {
    while(!bytes.empty()) {
        const auto n=write(fd,bytes.data(),bytes.size());
        if(n<0 && errno==EINTR)continue;
        if(n<=0)Fail("Could not write complete checkpoint.");
        bytes=bytes.subspan(static_cast<std::size_t>(n));
    }
}
}
bool SaveStream(const std::filesystem::path& path,const std::string& identity,std::span<const StreamSection> sections,std::string& error,Limits limits) {
    std::filesystem::path temporary;
    try {
        CheckLimits(limits);CheckIdentity(identity);
        if(sections.empty() || sections.size()>limits.sections)Fail("Invalid checkpoint section count.");
        Bytes table(sections.size()*EntryBytes,0),header(HeaderBytes,0);
        std::copy(Magic.begin(),Magic.end(),header.begin());
        Put(header,8,1,4);Put(header,12,HeaderBytes,4);Put(header,16,sections.size(),4);Put(header,20,identity.size(),4);
        std::copy(identity.begin(),identity.end(),header.begin()+48);
        std::uint64_t end=HeaderBytes+table.size();
        std::set<std::pair<SectionKind,std::uint64_t>> ids;
        for(std::size_t k=0;k<sections.size();++k) {
            const auto& s=sections[k];
            if(!Known(s.kind) || !s.version || (s.size && !s.read) || !ids.emplace(s.kind,s.id).second ||
               s.size>limits.section_bytes || end>limits.file_bytes || s.size>limits.file_bytes-end)Fail("Invalid checkpoint stream section.");
            const auto at=k*EntryBytes;Put(table,at,static_cast<std::uint32_t>(s.kind),4);Put(table,at+4,s.version,4);
            Put(table,at+8,s.id);Put(table,at+16,end);Put(table,at+24,s.size);end+=s.size;
        }
        if(end>limits.file_bytes)Fail("Checkpoint exceeds its size limit.");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        auto pattern=path.string()+".tmp-XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        Fd fd(mkstemp(name.data()));if(fd.value<0)Fail("Could not create checkpoint temporary file.");temporary=name.data();
        Write(fd.value,header);Write(fd.value,table);
        Bytes scratch(1<<20);
        for(std::size_t k=0;k<sections.size();++k) {
            const auto& s=sections[k];std::uint64_t hash=0xcbf29ce484222325ull;
            for(std::uint64_t at=0;at<s.size;) {
                auto chunk=std::span(scratch).first(static_cast<std::size_t>(std::min<std::uint64_t>(scratch.size(),s.size-at)));
                std::string why;if(!s.read(at,chunk,why))throw std::runtime_error(why.empty()?"Could not capture checkpoint section.":why);
                HashMore(hash,chunk);Write(fd.value,chunk);at+=chunk.size();
            }
            Put(table,k*EntryBytes+32,hash);
        }
        Put(header,24,end);Put(header,32,Hash(table));Put(header,40,0);Put(header,40,Hash(header));
        if(lseek(fd.value,0,SEEK_SET)<0)Fail("Could not finish checkpoint index.");
        Write(fd.value,header);Write(fd.value,table);
        if(fsync(fd.value))Fail("Could not flush checkpoint.");
        const int closed=close(fd.value);fd.value=-1;if(closed)Fail("Could not finish checkpoint file.");
        std::filesystem::rename(temporary,path);error.clear();return true;
    } catch(const std::exception& e) {
        if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
        error=e.what();return false;
    }
}

struct VerifiedArchive::Impl {
    Fd fd;
    struct stat stamp{};
    std::vector<SectionInfo> sections;
    explicit Impl(int value):fd(value) {}
    bool Unchanged() const {
        struct stat current{};if(fstat(fd.value,&current))return false;
        if(current.st_dev!=stamp.st_dev || current.st_ino!=stamp.st_ino || current.st_size!=stamp.st_size)return false;
        return current.st_mtimespec.tv_sec==stamp.st_mtimespec.tv_sec && current.st_mtimespec.tv_nsec==stamp.st_mtimespec.tv_nsec &&
               current.st_ctimespec.tv_sec==stamp.st_ctimespec.tv_sec && current.st_ctimespec.tv_nsec==stamp.st_ctimespec.tv_nsec;
    }
};
VerifiedArchive::VerifiedArchive()=default;
VerifiedArchive::~VerifiedArchive()=default;
VerifiedArchive::VerifiedArchive(VerifiedArchive&&) noexcept=default;
VerifiedArchive& VerifiedArchive::operator=(VerifiedArchive&&) noexcept=default;
std::span<const SectionInfo> VerifiedArchive::Sections() const {return impl?std::span<const SectionInfo>(impl->sections):std::span<const SectionInfo>{};}
bool VerifiedArchive::Read(std::size_t section,std::uint64_t offset,std::span<std::uint8_t> bytes,std::string& error) const {
    try {
        if(!impl || section>=impl->sections.size())Fail("Invalid checkpoint section index.");
        const auto& s=impl->sections[section];
        if(offset>s.size || bytes.size()>s.size-offset)Fail("Checkpoint read is outside its section.");
        if(!impl->Unchanged())Fail("Checkpoint file changed after verification.");
        Bb::Checkpoint::Read(impl->fd.value,s.offset+offset,bytes);
        if(!impl->Unchanged())Fail("Checkpoint file changed during restore.");
        error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool OpenVerified(const std::filesystem::path& path,const std::string& identity,VerifiedArchive& out,std::string& error,Limits limits) {
    try {
        CheckLimits(limits);CheckIdentity(identity);
        auto file=std::make_unique<VerifiedArchive::Impl>(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
        if(file->fd.value<0)Fail("Could not open checkpoint.");
        auto& st=file->stamp;
        if(fstat(file->fd.value,&st) || !S_ISREG(st.st_mode) || st.st_size<static_cast<off_t>(HeaderBytes) ||
           std::uint64_t(st.st_size)>limits.file_bytes || flock(file->fd.value,LOCK_SH|LOCK_NB))Fail("Invalid checkpoint file size, type or lock.");
        Bytes header(HeaderBytes);Read(file->fd.value,0,header);
        if(!std::equal(Magic.begin(),Magic.end(),header.begin()) || Get(header,8,4)!=1 || Get(header,12,4)!=HeaderBytes)Fail("Unsupported checkpoint format.");
        const auto saved=Get(header,40);Put(header,40,0);if(Hash(header)!=saved)Fail("Checkpoint header checksum failed.");
        const auto count=Get(header,16,4),length=Get(header,20,4);
        if(!count || count>limits.sections || !length || length>128 || Get(header,24)!=std::uint64_t(st.st_size))Fail("Invalid checkpoint header.");
        for(std::size_t k=48+length;k<header.size();++k)if(header[k])Fail("Invalid checkpoint identity padding.");
        if(std::string(header.begin()+48,header.begin()+48+length)!=identity)Fail("Checkpoint belongs to a different prepared build.");
        if(count*EntryBytes>std::uint64_t(st.st_size)-HeaderBytes)Fail("Truncated checkpoint section table.");
        Bytes table(static_cast<std::size_t>(count*EntryBytes));Read(file->fd.value,HeaderBytes,table);
        if(Hash(table)!=Get(header,32))Fail("Checkpoint section table checksum failed.");
        std::uint64_t end=HeaderBytes+table.size();std::set<std::pair<SectionKind,std::uint64_t>> ids;
        for(std::size_t k=0;k<count;++k) {
            const auto at=k*EntryBytes;
            SectionInfo s{static_cast<SectionKind>(Get(table,at,4)),static_cast<std::uint32_t>(Get(table,at+4,4)),
                          Get(table,at+8),Get(table,at+16),Get(table,at+24),Get(table,at+32)};
            if(!Known(s.kind) || !s.version || Get(table,at+40) || !ids.emplace(s.kind,s.id).second || s.offset!=end ||
               s.size>limits.section_bytes || end>std::uint64_t(st.st_size) || s.size>std::uint64_t(st.st_size)-end)Fail("Invalid checkpoint section layout.");
            end+=s.size;file->sections.push_back(s);
        }
        if(end!=std::uint64_t(st.st_size))Fail("Unexpected checkpoint trailing data.");
        Bytes scratch(1<<20);
        for(const auto& s:file->sections) {
            std::uint64_t hash=0xcbf29ce484222325ull;
            for(std::uint64_t at=0;at<s.size;) {
                auto chunk=std::span(scratch).first(static_cast<std::size_t>(std::min<std::uint64_t>(scratch.size(),s.size-at)));
                Read(file->fd.value,s.offset+at,chunk);HashMore(hash,chunk);at+=chunk.size();
            }
            if(hash!=s.checksum)Fail("Checkpoint section checksum failed.");
        }
        if(!file->Unchanged())Fail("Checkpoint changed during verification.");
        out.impl=std::move(file);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool Save(const std::filesystem::path& path,const Archive& archive,std::string& error,Limits limits) {
    std::filesystem::path temporary;
    try {
        CheckLimits(limits);CheckIdentity(archive.identity);
        if(archive.sections.empty() || archive.sections.size()>limits.sections)Fail("Invalid checkpoint section count.");
        Bytes table(archive.sections.size()*EntryBytes,0),header(HeaderBytes,0);
        std::copy(Magic.begin(),Magic.end(),header.begin());
        Put(header,8,1,4);Put(header,12,HeaderBytes,4);Put(header,16,archive.sections.size(),4);
        Put(header,20,archive.identity.size(),4);
        std::copy(archive.identity.begin(),archive.identity.end(),header.begin()+48);
        std::uint64_t end=HeaderBytes+table.size();
        std::set<std::pair<SectionKind,std::uint64_t>> ids;
        for(std::size_t k=0;k<archive.sections.size();++k) {
            const auto& section=archive.sections[k];
            if(!Known(section.kind) || !section.version || !ids.emplace(section.kind,section.id).second ||
               section.bytes.size()>limits.section_bytes || end>limits.file_bytes ||
               section.bytes.size()>limits.file_bytes-end)Fail("Invalid checkpoint section.");
            const auto at=k*EntryBytes;
            Put(table,at,static_cast<std::uint32_t>(section.kind),4);Put(table,at+4,section.version,4);
            Put(table,at+8,section.id);Put(table,at+16,end);Put(table,at+24,section.bytes.size());
            Put(table,at+32,Hash(section.bytes));end+=section.bytes.size();
        }
        if(end>limits.file_bytes)Fail("Checkpoint exceeds its size limit.");
        Put(header,24,end);Put(header,32,Hash(table));Put(header,40,0);Put(header,40,Hash(header));
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        auto pattern=path.string()+".tmp-XXXXXX";
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        Fd fd(mkstemp(name.data()));if(fd.value<0)Fail("Could not create checkpoint temporary file.");
        temporary=name.data();
        Write(fd.value,header);Write(fd.value,table);
        for(const auto& section:archive.sections)Write(fd.value,section.bytes);
        if(fsync(fd.value))Fail("Could not flush checkpoint.");
        const int closed=close(fd.value);fd.value=-1;
        if(closed)Fail("Could not finish checkpoint file.");
        std::filesystem::rename(temporary,path);
        error.clear();return true;
    } catch(const std::exception& e) {
        if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
        error=e.what();return false;
    }
}
bool Load(const std::filesystem::path& path,const std::string& identity,Archive& out,std::string& error,Limits limits) {
    try {
        CheckLimits(limits);CheckIdentity(identity);
        Fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
        if(fd.value<0)Fail("Could not open checkpoint.");
        struct stat st{};
        if(fstat(fd.value,&st) || !S_ISREG(st.st_mode) || st.st_size<static_cast<off_t>(HeaderBytes) ||
           std::uint64_t(st.st_size)>limits.file_bytes)Fail("Invalid checkpoint file size or type.");
        Bytes header(HeaderBytes);Read(fd.value,0,header);
        if(!std::equal(Magic.begin(),Magic.end(),header.begin()) || Get(header,8,4)!=1 || Get(header,12,4)!=HeaderBytes)
            Fail("Unsupported checkpoint format.");
        const auto header_hash=Get(header,40);Put(header,40,0);
        if(Hash(header)!=header_hash)Fail("Checkpoint header checksum failed.");
        const auto count=Get(header,16,4),length=Get(header,20,4);
        if(!count || count>limits.sections || !length || length>128 || Get(header,24)!=std::uint64_t(st.st_size))
            Fail("Invalid checkpoint header.");
        for(std::size_t k=48+length;k<header.size();++k)if(header[k])Fail("Invalid checkpoint identity padding.");
        const std::string stored(header.begin()+48,header.begin()+48+length);
        if(stored!=identity)Fail("Checkpoint belongs to a different prepared build.");
        const auto table_bytes=count*EntryBytes;
        if(table_bytes>std::uint64_t(st.st_size)-HeaderBytes)Fail("Truncated checkpoint section table.");
        Bytes table(static_cast<std::size_t>(table_bytes));Read(fd.value,HeaderBytes,table);
        if(Hash(table)!=Get(header,32))Fail("Checkpoint section table checksum failed.");
        Archive archive;archive.identity=stored;
        std::set<std::pair<SectionKind,std::uint64_t>> ids;
        std::uint64_t end=HeaderBytes+table_bytes;
        // Validate all ranges and identities before allocating any payload.
        for(std::size_t k=0;k<count;++k) {
            const auto at=k*EntryBytes;
            const auto offset=Get(table,at+16),size=Get(table,at+24);
            const auto kind=static_cast<SectionKind>(Get(table,at,4));
            if(!Known(kind) || !Get(table,at+4,4) || Get(table,at+40) ||
               !ids.emplace(kind,Get(table,at+8)).second || offset!=end ||
               size>limits.section_bytes || end>std::uint64_t(st.st_size) ||
               size>std::uint64_t(st.st_size)-end)Fail("Invalid checkpoint section layout.");
            end+=size;
        }
        if(end!=std::uint64_t(st.st_size))Fail("Unexpected checkpoint trailing data.");
        for(std::size_t k=0;k<count;++k) {
            const auto at=k*EntryBytes;
            Section section;section.kind=static_cast<SectionKind>(Get(table,at,4));
            section.version=static_cast<std::uint32_t>(Get(table,at+4,4));section.id=Get(table,at+8);
            section.bytes.resize(static_cast<std::size_t>(Get(table,at+24)));
            Read(fd.value,Get(table,at+16),section.bytes);
            if(Hash(section.bytes)!=Get(table,at+32))Fail("Checkpoint section checksum failed.");
            archive.sections.push_back(std::move(section));
        }
        out=std::move(archive);error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
}
