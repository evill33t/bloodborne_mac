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
    if(site==2)return " if ((vid & 63u) == 0u) { const uint dc_observed_[3] = {s0, s5, s4}; dc_record_wind_site(dc_wind_record, 2u, dc_observed_, dc_camera_leaf_, vid, iid, p_ != nullptr && dc_camera_y_mapped && dc_camera_z_mapped); }";
    const unsigned count=site ? 16 : 4,first=site ? 8 : 12;
    auto text=" if ((vid & 63u) == 0u) { const uint dc_observed_["+std::to_string(count)+"] = {";
    for(unsigned k=0;k<count;++k)text+=(k ? ", " : "")+std::string("s")+std::to_string(first+k);
    return text+"}; dc_record_wind_site(dc_wind_record, "+std::to_string(site)+
        "u, dc_observed_, dc_vbase("+(site ? "s0, s1" : "s20, s21")+"), vid, iid, p_ != nullptr); }";
}
std::string MatrixCall() {
    std::string call="    if ((vid & 63u) == 0u && exec_lo != 0u) { const uint dc_matrix_observed_[12] = {";
    for(unsigned k=0;k<12;++k)call+=(k ? ", " : "")+std::string("v")+std::to_string(23+k);
    return call+"}; dc_record_wind_matrix(dc_wind_record, dc_matrix_observed_, uint4(s16, s17, s18, s19), uint3(v0, v1, v2), vid, iid); }\n";
}
void Verify(const Shader::Program& p) {
    Require(Shader::VertexWindConsumedEligible(p),"Full Dream read-only/site admission rejected");
    std::vector<std::string> unsupported;const auto normal=Shader::GenerateMsl(p,{},&unsupported);
    Require(unsupported.empty() && normal.find("dc_wind_record")==std::string::npos,"OFF changed normal shader");
    Shader::MslOptions options;options.wind_consumed=true;
    auto diagnostic=Shader::GenerateMsl(p,options,&unsupported);Require(unsupported.empty(),"Diagnostic translation unsupported");
    Require(diagnostic.find("s15 = p_ ? p_[3] : 0u;"+SiteCall(0))!=std::string::npos,"Wind sample is not after original load");
    Require(diagnostic.find("s23 = p_ ? p_[15] : 0u;"+SiteCall(1))!=std::string::npos,"View sample is not after original load");
    const std::string saved_base=" const ulong dc_camera_leaf_ = dc_vbase(s0, s1);";
    Require(diagnostic.find(saved_base+" s0 = p_ ? p_[0] : 0u;"+SiteCall(2))!=std::string::npos,
        "Camera base was not saved before the original overwriting X load or camera registers were reread");
    Require(diagnostic.find("s4 = p_ ? p_[0] : 0u; dc_camera_z_mapped = p_ != nullptr;")!=std::string::npos,
        "Original camera Z null result is not retained");
    Require(diagnostic.find("s5 = p_ ? p_[0] : 0u; dc_camera_y_mapped = p_ != nullptr;")!=std::string::npos,
        "Original camera Y null result is not retained");
    Require(diagnostic.find("v34 = f_[3];\n"+MatrixCall())!=std::string::npos,"Matrix observation is not after the third original row load");
    RemoveOne(diagnostic,MslPrelude);RemoveOne(diagnostic,SiteCall(0));RemoveOne(diagnostic,SiteCall(1));
    RemoveOne(diagnostic,SiteCall(2));RemoveOne(diagnostic,saved_base);
    RemoveOne(diagnostic,MatrixCall());
    RemoveOne(diagnostic," dc_camera_z_mapped = p_ != nullptr;");RemoveOne(diagnostic," dc_camera_y_mapped = p_ != nullptr;");
    RemoveOne(diagnostic,"    bool dc_camera_z_mapped = false, dc_camera_y_mapped = false;\n");
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
        for(auto pc:{0x000cu,0x0010u,0x001cu,0x14b4u,0x14e8u,0x14ecu,0x14f8u,0x1500u}) {
            bad=p;auto found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            ++found->smrd_offset;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong load offset admitted");
            bad=p;found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            bad.code.erase(found);Require(!Shader::VertexWindConsumedEligible(bad),"Missing instruction admitted");
        }
        for(auto pc:{0x00b4u,0x00bcu,0x00c4u}) {
            bad=p;auto found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            ++found->srsrc;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong matrix descriptor admitted");
            bad=p;found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            found->idxen=false;Require(!Shader::VertexWindConsumedEligible(bad),"Wrong matrix address mode admitted");
        }
        bad=p;bad.code[4].enc=Enc::MUBUF;bad.code[4].op=u16(Mubuf::BUFFER_STORE_DWORD);
        Require(!Shader::VertexWindConsumedEligible(bad),"Unreflected guest writer admitted");
        bad=p;bad.unsupported.push_back("fixture");Require(!Shader::VertexWindConsumedEligible(bad),"Unsupported program admitted");
        if(argc==3) {auto original=Original(argv[2]);Verify(*original);std::puts("Exact retained Dream VS/fetch program admitted; original instructions unchanged.");}
        std::puts("Consumed-wind CPU: complete proof, exact wind/view sites, no guest rereads, conflicts and unchanged OFF MSL passed.");
    } catch(const std::exception& e) {std::fprintf(stderr,"Consumed-wind fixture: %s\n",e.what());return 1;}
}
