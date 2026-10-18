// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Bb::Checkpoint {
// This container does not decide whether a collection of sections is a complete
// playable state. The runtime coordinator must enforce its own section schema.
enum class SectionKind : std::uint32_t {
    Cpu=1, MemoryLayout=2, MemoryBytes=3, Threads=4, Synchronization=5,
    Files=6, Clocks=7, Services=8, Audio=9, Gpu=10, Fixture=0x7fffffff
};
struct Section {
    SectionKind kind{};
    std::uint32_t version=1;
    std::uint64_t id=0;
    std::vector<std::uint8_t> bytes;
    bool operator==(const Section&) const=default;
};
struct Archive {
    std::string identity;
    std::vector<Section> sections;
    bool operator==(const Archive&) const=default;
};
struct Limits {
    std::uint64_t file_bytes=256ull<<20;
    std::uint64_t section_bytes=64ull<<20;
    std::uint32_t sections=4096;
};
bool Save(const std::filesystem::path&,const Archive&,std::string& error,Limits={});
// Always validate the complete container before replacing the output. Loading
// data never changes memory mappings or jumps to a saved CPU context.
bool Load(const std::filesystem::path&,const std::string& identity,Archive&,
          std::string& error,Limits={});
// Sources are read in bounded chunks while the coordinator holds the frozen
// token. A multi-GiB guest heap never needs a second full in-memory copy.
struct StreamSection {
    SectionKind kind{};
    std::uint32_t version=1;
    std::uint64_t id=0,size=0;
    std::function<bool(std::uint64_t,std::span<std::uint8_t>,std::string&)> read;
};
bool SaveStream(const std::filesystem::path&,const std::string& identity,
                std::span<const StreamSection>,std::string& error,Limits={});
struct SectionInfo {
    SectionKind kind{};
    std::uint32_t version=1;
    std::uint64_t id=0,offset=0,size=0,checksum=0;
};
class VerifiedArchive {
public:
    VerifiedArchive();
    ~VerifiedArchive();
    VerifiedArchive(VerifiedArchive&&) noexcept;
    VerifiedArchive& operator=(VerifiedArchive&&) noexcept;
    VerifiedArchive(const VerifiedArchive&)=delete;
    VerifiedArchive& operator=(const VerifiedArchive&)=delete;
    std::span<const SectionInfo> Sections() const;
    bool Read(std::size_t section,std::uint64_t offset,std::span<std::uint8_t>,std::string& error) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend bool OpenVerified(const std::filesystem::path&,const std::string&,VerifiedArchive&,std::string&,Limits);
};
// Validates every payload checksum with <=1 MiB scratch before publishing an
// index. Keeps the opened inode and shared advisory lock until destruction;
// rejects file mutation during verification or subsequent reads.
bool OpenVerified(const std::filesystem::path&,const std::string& identity,VerifiedArchive&,
                  std::string& error,Limits={});
}
