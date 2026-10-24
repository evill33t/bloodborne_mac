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
std::string FixedCall(unsigned site) {
    constexpr unsigned counter[]{120,121,122,123,124},minimum[]{125,197,235,241,247},maximum[]{161,216,238,244,251};
    std::vector<std::string> words;
    if(site==0) {
        for(unsigned k=16;k<=35;++k)words.push_back("s"+std::to_string(k));
        for(unsigned k:{4u,5u,6u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,20u,21u,22u})words.push_back("v"+std::to_string(k));
    } else if(site==1) {
        for(unsigned k=16;k<=19;++k)words.push_back("s"+std::to_string(k));
        for(unsigned k=0;k<3;++k)words.push_back("v"+std::to_string(k));
        for(unsigned k=23;k<=34;++k)words.push_back("v"+std::to_string(k));
    } else if(site<4)words={"v26","v30","v34"};
    else words={"v5","v6","v7","v8"};
    auto text="    if (vid == 0u && iid == 0u && exec_lo != 0u) { const uint dc_fixed_values_["+std::to_string(words.size())+"] = {";
    for(unsigned k=0;k<words.size();++k)text+=(k ? ", " : "")+words[k];
    return text+"}; dc_record_fixed_vertex(dc_wind_record, "+std::to_string(counter[site])+"u, "+
        std::to_string(minimum[site])+"u, "+std::to_string(maximum[site])+"u, dc_fixed_values_, "+std::to_string(words.size())+"u); }\n";
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
    const auto fixed_fetch=diagnostic.find(FixedCall(0));
    const auto matrix_root=diagnostic.find("32u); s16 = p_ ? p_[0]");
    Require(fixed_fetch!=std::string::npos && fixed_fetch<matrix_root,"Fixed original fetch registers were captured after their root-load overwrite");
    Require(diagnostic.find(MatrixCall()+"    }\n"+FixedCall(1))!=std::string::npos,"Fixed matrix registers were not captured after the third original load");
    Require(diagnostic.find("v34 = as_type<uint>(as_type<float>(v31) * as_type<float>(v4) + as_type<float>(v34));\n"+FixedCall(2))!=std::string::npos,
        "Fixed undeformed world position is not after original PC00f8");
    Require(diagnostic.find("v26 = as_type<uint>(as_type<float>(v3) * as_type<float>(v21) + as_type<float>(v26));\n"+FixedCall(3))!=std::string::npos,
        "Fixed deformed world position is not after original PC1534");
    Require(diagnostic.find("out.pos = float4(as_type<float>(v5), as_type<float>(v6), as_type<float>(v7), as_type<float>(v8));\n"+FixedCall(4))!=std::string::npos,
        "Fixed clip registers are not captured after the original export");
    for(unsigned site=0;site<5;++site)RemoveOne(diagnostic,FixedCall(site));
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
        for(unsigned field=0;field<5;++field) {
            bad=p;
            if(field==0)--bad.num_user_sgprs;
            if(field==1)--bad.vgpr_comp_cnt;
            if(field==2)++bad.fetch_ud;
            if(field==3)bad.fetch_bytes+=4;
            if(field==4)bad.code.pop_back();
            Require(!Shader::VertexWindConsumedEligible(bad),"Changed original fixed-vertex decoder context admitted");
        }
        for(const unsigned pc:{0x100000u,0x100018u,0x00f8u,0x1534u,0x1578u}) {
            bad=p;const auto found=std::find_if(bad.code.begin(),bad.code.end(),[&](const auto& i){return i.pc==pc;});
            Require(found!=bad.code.end(),"Synthetic fixed-vertex test site missing");
            if(found->enc==Enc::EXP)++found->vsrc[0];else ++found->op;
            Require(!Shader::VertexWindConsumedEligible(bad),"Changed fixed-vertex load/math/export site admitted at PC"+std::to_string(pc));
        }
        bad=p;bad.code[4].enc=Enc::MUBUF;bad.code[4].op=u16(Mubuf::BUFFER_STORE_DWORD);
        Require(!Shader::VertexWindConsumedEligible(bad),"Unreflected guest writer admitted");
        bad=p;bad.unsupported.push_back("fixture");Require(!Shader::VertexWindConsumedEligible(bad),"Unsupported program admitted");
        if(argc==3) {auto original=Original(argv[2]);Verify(*original);std::puts("Exact retained Dream VS/fetch program admitted; original instructions unchanged.");}
        std::puts("Consumed-wind CPU: complete proof, exact wind/view sites, no guest rereads, conflicts and unchanged OFF MSL passed.");
    } catch(const std::exception& e) {std::fprintf(stderr,"Consumed-wind fixture: %s\n",e.what());return 1;}
}
