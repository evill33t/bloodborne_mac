// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/shadow_htile_clear.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace C=Gpu::Metal::ShadowHtileClear;
using C::Word;
int main() {
    Gpu::Shader::Program program;
    program.stage=Gpu::Shader::Stage::Compute;program.hash=C::ShaderHash;
    program.num_user_sgprs=12;program.compute_rsrc2=0x98;program.has_branches=program.writes_exec=true;
    const auto shape=C::OriginalShape();program.code.assign(shape.begin(),shape.end());
    assert(C::ProgramEligible(program));
    // Changing any instruction, including the preserve-13 predicate or its
    // branches, must not inherit the full-clear interpretation by stale hash.
    for(unsigned n=0;n<program.code.size();++n) {
        ++program.code[n].op;assert(!C::ProgramEligible(program));--program.code[n].op;
    }
    program.code[8].idxen=false;assert(!C::ProgramEligible(program));program.code[8].idxen=true;
    program.code[17].glc=true;assert(!C::ProgramEligible(program));program.code[17].glc=false;
    program.code[7].simm16=11;assert(!C::ProgramEligible(program));program.code[7].simm16=12;
    auto original=program;
    program.stage=Gpu::Shader::Stage::Fragment;assert(!C::ProgramEligible(program));program=original;
    program.num_user_sgprs=0;assert(!C::ProgramEligible(program));program=original;
    program.compute_rsrc2^=0x80;assert(!C::ProgramEligible(program));program=original;
    program.vgpr_comp_cnt=1;assert(!C::ProgramEligible(program));program=original;
    program.unsupported.push_back("unknown");assert(!C::ProgramEligible(program));program=original;
    program.fetch_bytes=4;assert(!C::ProgramEligible(program));program=original;
    constexpr C::Address Meta=0x808b4a0000ull,Depth=0x80874a0000ull,Uniform=0x8040000000ull,Generation=42;
    std::array<Word,16> user{};
    user[4]=Word(Meta);user[5]=0x43080;user[6]=C::Words;user[7]=0x24004;
    user[8]=Word(Uniform);user[9]=0x108080;user[10]=1;user[11]=0x77fac;
    const auto original_user=user;
    C::Surface surface{Meta,Depth,Generation,C::Width,C::Height,C::Width,C::Height,1,1,1,true,true,true,C::DepthClearWord};
    const auto original_surface=surface;
    C::CpuReadProof uniform_proof{Uniform,8,Generation,Generation,true};
    C::CpuReadProof metadata_proof{Meta,C::MetadataBytes,Generation,Generation,true};
    std::array<Word,2> uniform{C::Words,C::ClearWord};
    std::array<Word,3> groups{4096,1,1},threads{64,1,1};bool indirect=false;
    std::vector<Word> metadata(C::Words);
    // Exercise every non-preserved nibble, not only a uniform-zero image.
    for(unsigned n=0;n<metadata.size();++n)metadata[n]=0x12345000u|((n%15)<13 ? n%15 : n%15+1);
    const auto decide=[&] {return C::Recognize(program,user,groups,threads,indirect,surface,
        uniform_proof,uniform,metadata_proof,metadata);};
    auto decision=decide();assert(decision.plan && decision.reason==C::Reject::None);
    assert(decision.plan->target==Depth && decision.plan->metadata==Meta && decision.plan->generation==Generation);
    assert(decision.plan->depth_clear_word==0x3f800000);
    C::GpuReadProof gpu_uniform{Uniform,8,Generation,Generation,true};
    C::GpuReadProof gpu_metadata{Meta,C::MetadataBytes,Generation,Generation,true};
    const auto gpu_decide=[&] {return C::RecognizeGpu(program,user,groups,threads,indirect,surface,gpu_uniform,gpu_metadata);};
    assert(gpu_decide().plan);
    gpu_metadata.ready=false;assert(gpu_decide().reason==C::Reject::Readiness);gpu_metadata.ready=true;
    gpu_uniform.ready=false;assert(gpu_decide().reason==C::Reject::Readiness);gpu_uniform.ready=true;
    --gpu_metadata.bytes;assert(gpu_decide().reason==C::Reject::Readiness);++gpu_metadata.bytes;
    ++gpu_uniform.address;assert(gpu_decide().reason==C::Reject::Readiness);--gpu_uniform.address;
    ++gpu_metadata.generation_after;assert(gpu_decide().reason==C::Reject::Readiness);--gpu_metadata.generation_after;
    ++gpu_uniform.generation_before;assert(gpu_decide().reason==C::Reject::Readiness);--gpu_uniform.generation_before;
    surface.mapping_generation++;assert(gpu_decide().reason==C::Reject::Readiness);surface=original_surface;
    program.code[11].op++;assert(gpu_decide().reason==C::Reject::Program);program=original;
    user[7]^=1;assert(gpu_decide().reason==C::Reject::MetadataDescriptor);user=original_user;
    indirect=true;assert(gpu_decide().reason==C::Reject::Dispatch);indirect=false;
    user[8]=Word(Meta);user[9]=0x00108000u|Word(Meta>>32);
    assert(decide().reason==C::Reject::InputAlias && gpu_decide().reason==C::Reject::InputAlias);user=original_user;
    user[8]=Word(Meta+C::MetadataBytes-16);user[9]=0x00108000u|Word(Meta>>32);
    assert(decide().reason==C::Reject::InputAlias && gpu_decide().reason==C::Reject::InputAlias);user=original_user;
    for(unsigned at:std::array<unsigned,3>{0,C::Words/2,C::Words-1}) {
        const auto before=metadata[at];metadata[at]=0xfffffffd;
        assert(!decide().plan && decide().reason==C::Reject::PreservedTiles);metadata[at]=before;
    }
    --uniform[0];assert(decide().reason==C::Reject::Uniform);++uniform[0];
    // The old conservative full-range bounds word is NOT the observed clear.
    uniform[1]=0xfffc000f;assert(decide().reason==C::Reject::Uniform);uniform[1]=C::ClearWord;
    --groups[0];assert(decide().reason==C::Reject::Dispatch);++groups[0];
    groups[1]=2;assert(decide().reason==C::Reject::Dispatch);groups[1]=1;
    threads[0]=32;assert(decide().reason==C::Reject::Dispatch);threads[0]=64;
    indirect=true;assert(decide().reason==C::Reject::Dispatch);indirect=false;
    surface.matching_targets=2;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    surface.depth32=false;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    surface.width=2048;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    surface.pitch=8192;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    surface.layers=2;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    surface.depth_clear_word=0x3f000000;assert(decide().reason==C::Reject::Surface);surface=original_surface;
    for(unsigned word:std::array<unsigned,4>{4,5,6,7}) {
        user[word]^=1;assert(decide().reason==C::Reject::MetadataDescriptor);user=original_user;
    }
    for(unsigned word:std::array<unsigned,4>{8,9,10,11}) {
        user[word]^=word==9 ? (1u<<16) : 1u;assert(decide().reason==C::Reject::UniformDescriptor);user=original_user;
    }
    // Current map ownership, complete input coverage and the renderer's
    // no-pending-writer/callback read proof are all mandatory independently.
    metadata_proof.ready=false;assert(decide().reason==C::Reject::Readiness);metadata_proof.ready=true;
    uniform_proof.ready=false;assert(decide().reason==C::Reject::Readiness);uniform_proof.ready=true;
    ++metadata_proof.generation_after;assert(decide().reason==C::Reject::Readiness);--metadata_proof.generation_after;
    ++uniform_proof.generation_before;assert(decide().reason==C::Reject::Readiness);--uniform_proof.generation_before;
    --metadata_proof.bytes;assert(decide().reason==C::Reject::Readiness);++metadata_proof.bytes;
    ++uniform_proof.address;assert(decide().reason==C::Reject::Readiness);--uniform_proof.address;
    metadata.pop_back();assert(decide().reason==C::Reject::Readiness);metadata.push_back(0);
    assert(decide().plan);
    std::puts("Shadow HTILE clear contract: exact program/ABI, complete dispatch/surface, guarded current reads, observed FFF0 value and preserved-13 rejection passed");
}
