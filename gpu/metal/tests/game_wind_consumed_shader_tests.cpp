// SPDX-License-Identifier: GPL-2.0-or-later
// CPU emitter/proof tests only; optional private --capture remains read-only.
#include "game_wind_consumed_fixture.h"
#include "gpu/metal/wind_consumed_msl.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" void runtime_restart() {std::abort();}
namespace {
using namespace WindConsumedFixture;
using namespace Gpu::Metal::WindConsumed;
void RemoveOne(std::string& source,const std::string& part) {
    const auto at=source.find(part);Require(at!=std::string::npos,"Missing diagnostic fragment");
    Require(source.find(part,at+part.size())==std::string::npos,"Duplicate diagnostic fragment");source.erase(at,part.size());
}
std::string SiteCall(unsigned site) {
    const unsigned count=site ? 16 : 4,first=site ? 8 : 12;
    auto text=" if ((vid & 63u) == 0u) { const uint dc_observed_["+std::to_string(count)+"] = {";
    for(unsigned k=0;k<count;++k)text+=(k ? ", " : "")+std::string("s")+std::to_string(first+k);
    return text+"}; dc_record_wind_site(dc_wind_record, "+std::to_string(site)+
        "u, dc_observed_, dc_vbase("+(site ? "s0, s1" : "s20, s21")+"), vid, iid, p_ != nullptr); }";
}
void Verify(const Shader::Program& p) {
    Require(Shader::VertexWindConsumedEligible(p),"Full Dream read-only/site admission rejected");
    std::vector<std::string> unsupported;const auto normal=Shader::GenerateMsl(p,{},&unsupported);
    Require(unsupported.empty() && normal.find("dc_wind_record")==std::string::npos,"OFF changed normal shader");
    Shader::MslOptions options;options.wind_consumed=true;
    auto diagnostic=Shader::GenerateMsl(p,options,&unsupported);Require(unsupported.empty(),"Diagnostic translation unsupported");
    Require(diagnostic.find("s15 = p_ ? p_[3] : 0u;"+SiteCall(0))!=std::string::npos,"Wind sample is not after original load");
    Require(diagnostic.find("s23 = p_ ? p_[15] : 0u;"+SiteCall(1))!=std::string::npos,"View sample is not after original load");
    RemoveOne(diagnostic,MslPrelude);RemoveOne(diagnostic,SiteCall(0));RemoveOne(diagnostic,SiteCall(1));
    RemoveOne(diagnostic,", device atomic_uint* dc_wind_record [[buffer(14)]]");
    RemoveOne(diagnostic,", device atomic_uint* dc_wind_record");RemoveOne(diagnostic,", dc_wind_record");
    Require(diagnostic==normal,"Diagnostic changed original loads/arithmetic/exports");
    for(unsigned conflict=0;conflict<12;++conflict) {
        options={};options.wind_consumed=true;
        switch(conflict) {
        case 0:options.debug_capture=true;break;case 1:options.points=true;break;
        case 2:options.rect_list=true;break;case 3:options.readonly_guest_memory=true;break;
        case 4:options.native_vertex_fetch=true;break;case 5:options.direct_vertex_root=true;break;
        case 6:options.static_smrd=true;break;case 7:options.direct_smrd=true;break;
        case 8:options.simd_smrd=true;break;case 9:options.probe_pc=0;break;
        case 10:options.tess_stage=Shader::TessStage::Domain;break;case 11:options.native_tess_layout=1;break;
        }
        const auto rejected=Shader::GenerateMsl(p,options,&unsupported);
        Require(rejected.find("dc_wind_record")==std::string::npos,"Conflicting variant admitted diagnostic ABI");
    }
}
}
int main(int argc,char** argv) {
    if(argc!=1 && !(argc==3 && std::strcmp(argv[1],"--capture")==0))return 2;
    try {
        auto p=SyntheticShape();Verify(p);
        auto bad=p;bad.hash^=1;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong hash admitted");
        bad=p;bad.stage=Shader::Stage::Fragment;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong stage admitted");
        for(auto pc:{0x000cu,0x001cu,0x14b4u,0x14ecu}) {
            bad=p;auto found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            ++found->smrd_offset;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong load offset admitted");
            bad=p;bad.code.erase(bad.code.begin()+pc/4);Require(!Shader::VertexWindConsumedEligible(bad),"Missing instruction admitted");
        }
        bad=p;bad.code[4].enc=Enc::MUBUF;bad.code[4].op=u16(Mubuf::BUFFER_STORE_DWORD);
        Require(!Shader::VertexWindConsumedEligible(bad),"Unreflected guest writer admitted");
        bad=p;bad.unsupported.push_back("fixture");Require(!Shader::VertexWindConsumedEligible(bad),"Unsupported program admitted");
        if(argc==3) {auto original=Original(argv[2]);Verify(*original);std::puts("Exact retained Dream VS/fetch program admitted; original instructions unchanged.");}
        std::puts("Consumed-wind CPU: complete proof, exact wind/view sites, no guest rereads, conflicts and unchanged OFF MSL passed.");
    } catch(const std::exception& e) {std::fprintf(stderr,"Consumed-wind fixture: %s\n",e.what());return 1;}
}
