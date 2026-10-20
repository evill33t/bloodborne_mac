// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/texture_write_journal.h"
#include <array>
#include <cassert>
#include <cstdio>

using Gpu::Metal::TextureWriteJournal;
using R=TextureWriteJournal::Range;
using Relation=TextureWriteJournal::Relation;
int main() {
    using J=TextureWriteJournal;
    assert(!J::Enabled(false,nullptr));assert(!J::Enabled(false,"1"));
    assert(J::Enabled(true,nullptr));assert(J::Enabled(true,"1"));
    assert(!J::Enabled(true,"0"));assert(!J::Enabled(true,"invalid"));
    J journal;
    const std::array<R,2> source{{{0,64},{1024,64}}}; // physical zero is valid
    assert(journal.Inspect(0,source).relation==Relation::Unchanged);
    const std::array<R,1> unrelated{{{4096,128}}};journal.Publish(unrelated);
    assert(journal.Inspect(0,source).relation==Relation::Disjoint);
    const auto first=journal.Epoch();
    const std::array<R,1> alias{{{1040,4}}};journal.Publish(alias);
    assert(journal.Inspect(first,source).relation==Relation::Overlap);
    assert(!journal.Inspect(first,source).cpu_callbacks);
    const auto completed=journal.Epoch();
    // There is intentionally no Flip or completion clear. An unused texture
    // still observes writes when it returns many frames later.
    assert(journal.Inspect(first,source).FullCheck());
    assert(journal.Current(completed));assert(!journal.Current(first));
    journal.Publish(alias,false,true);
    const auto scheduled=journal.Inspect(completed,source);
    assert(scheduled.FullCheck() && scheduled.cpu_callbacks);
    assert(journal.Inspect(journal.Epoch(),source).relation==Relation::Unchanged);
    journal.Publish({},true);
    assert(journal.Inspect(completed,unrelated).relation==Relation::Unknown);
    assert(journal.Inspect(completed,source).cpu_callbacks);
    assert(journal.Inspect(completed,source,false).relation==Relation::Unknown);

    J bounded(2);bounded.Publish(source);
    assert(bounded.Size()==2);const auto observed=bounded.Epoch();
    bounded.Publish(unrelated);assert(bounded.Size()==2 && bounded.ForgottenThrough()==observed);
    assert(bounded.Inspect(0,alias).relation==Relation::Unknown);
    assert(bounded.Inspect(observed,alias).relation==Relation::Disjoint);
    bounded.Publish(alias);bounded.Publish(alias);
    assert(bounded.Inspect(observed,source).relation==Relation::Unknown);
    const auto refreshed=bounded.Epoch();assert(bounded.Current(refreshed));
    assert(bounded.Inspect(refreshed,source).relation==Relation::Unchanged);

    J merging;
    const std::array<R,4> adjacent{{{16,16},{32,16},{24,8},{128,4}}};merging.Publish(adjacent);
    assert(merging.Size()==2);
    const std::array<R,1> tail{{{47,1}}},gap{{{48,80}}};
    assert(merging.Inspect(0,tail).relation==Relation::Overlap);
    assert(merging.Inspect(0,gap).relation==Relation::Disjoint);
    const std::array<R,1> overflow{{{UINT64_MAX-1,4}}};merging.Publish(overflow);
    assert(merging.Inspect(0,gap).relation==Relation::Unknown);
    assert(merging.Inspect(merging.Epoch()+1,gap).relation==Relation::Unknown);
    J saturated(2,UINT64_MAX);saturated.Publish(alias);
    assert(!saturated.Current(UINT64_MAX));assert(saturated.Inspect(UINT64_MAX,source).FullCheck());
    J none(0);none.Publish(alias);assert(!none.Size());assert(none.Inspect(0,source).FullCheck());
    const auto before=journal.Epoch();journal.Publish({});assert(journal.Epoch()==before);
    std::puts("texture write journal: physical aliases, deferred CPU callbacks, retained/forgotten history, bounded storage, overflow and gating passed");
}
