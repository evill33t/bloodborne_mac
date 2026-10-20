// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/metal/native_indirect.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <span>

namespace Gpu::Metal {
// Renderer-thread metadata only. Completed writes deliberately remain here:
// completion does not make an already uploaded CPU texture current again.
class TextureWriteJournal {
public:
    using Range=NativeIndirect::Range;
    enum class Relation {Unchanged,Disjoint,Overlap,Unknown};
    struct Query {
        Relation relation=Relation::Unknown;
        bool cpu_callbacks=false;
        bool FullCheck() const {return relation==Relation::Overlap || relation==Relation::Unknown;}
    };
    static constexpr std::size_t kLimit=512;
    static bool Enabled(bool native,const char* value) {
        return native && (!value || !std::strcmp(value,"1"));
    }
    explicit TextureWriteJournal(std::size_t limit=kLimit,std::uint64_t initial_epoch=0)
        : capacity(std::min(limit,kLimit)),epoch(initial_epoch) {}
    std::uint64_t Epoch() const {return epoch;}
    bool Current(std::uint64_t checked) const {return !saturated && checked==epoch;}
    std::size_t Size() const {return records.size();}
    std::uint64_t ForgottenThrough() const {return forgotten;}
    void Publish(std::span<const Range> physical,bool unknown=false,bool deferred_cpu=false) {
        bool nonempty=unknown;
        for(const auto& range:physical) {
            nonempty|=range.bytes!=0;
            unknown|=range.address>UINT64_MAX-range.bytes;
        }
        if(!nonempty)return;
        if(epoch==UINT64_MAX) {saturated=true;forgotten=epoch;records.clear();return;}
        ++epoch;
        const auto append=[&](Range range,bool uncertain) {
            if(!capacity){forgotten=epoch;return;}
            if(records.size()==capacity){forgotten=std::max(forgotten,records.front().epoch);records.pop_front();}
            records.push_back({epoch,range,uncertain,deferred_cpu});
        };
        if(unknown){append({},true);return;}
        for(const auto& range:physical)if(range.bytes) {
            if(!records.empty()) {
                auto& previous=records.back();
                if(previous.epoch==epoch && !previous.unknown &&
                    range.address<=previous.physical.address+previous.physical.bytes &&
                    previous.physical.address<=range.address+range.bytes) {
                    const auto end=std::max(previous.physical.address+previous.physical.bytes,range.address+range.bytes);
                    previous.physical.address=std::min(previous.physical.address,range.address);
                    previous.physical.bytes=end-previous.physical.address;continue;
                }
            }
            append(range,false);
        }
    }
    Query Inspect(std::uint64_t checked,std::span<const Range> source,bool complete=true) const {
        if(saturated || checked>epoch || checked<forgotten)return {Relation::Unknown,true};
        if(checked==epoch)return {Relation::Unchanged,false};
        if(!complete)return {Relation::Unknown,true};
        Query answer{Relation::Disjoint,false};
        for(const auto& record:records)if(record.epoch>checked) {
            bool overlap=record.unknown;
            for(const auto& range:source) {
                if(!range.bytes || range.address>UINT64_MAX-range.bytes)return {Relation::Unknown,true};
                overlap|=NativeIndirect::Overlap(range,record.physical);
            }
            if(overlap) {
                if(record.unknown)answer.relation=Relation::Unknown;
                else if(answer.relation!=Relation::Unknown)answer.relation=Relation::Overlap;
                answer.cpu_callbacks|=record.deferred_cpu;
            }
        }
        return answer;
    }
private:
    struct Record {std::uint64_t epoch;Range physical;bool unknown,deferred_cpu;};
    std::size_t capacity;
    std::uint64_t epoch=0,forgotten=0;
    bool saturated=false;
    std::deque<Record> records;
};
} // namespace Gpu::Metal
