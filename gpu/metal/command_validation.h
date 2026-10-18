// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/metal/vendor/bbport/cp/cp_decoder.h"

namespace Gpu::Metal {
inline unsigned MinimumPacketBody(unsigned opcode) {
    using BbCp::Op;
    switch (static_cast<Op>(opcode)) {
    case Op::EventWriteEop: case Op::DrawIndex2: return 5;
    case Op::ReleaseMem: case Op::DmaData: case Op::WaitRegMem: return 6;
    case Op::EventWriteEos: case Op::DrawIndexOffset2: case Op::DispatchDirect:
    case Op::LoadConstRam: case Op::DumpConstRam: return 4;
    case Op::WriteData: case Op::IndirectBuffer: case Op::IndirectBufferConst:
    case Op::SetBase: case Op::DrawIndirect: case Op::DrawIndexIndirect: return 3;
    case Op::IndexBase: case Op::DrawIndexAuto: return 2;
    default: return 1;
    }
}
// Only compares register-write packets. Other commands can write guest memory
// or synchronize rings; the validation decoder must never execute them again.
class RegisterValidation {
    BbCp::Decoder decoder;
    BbCp::Sink sink;
    unsigned start=0,count=0;
public:
    void Prepare(std::span<const std::uint32_t> packet) {
        start=count=0;
        if (packet.size()<2) return;
        const BbCp::Header header{packet[0]};
        unsigned bank=0;
        switch (header.Opcode()) {
        case BbCp::Op::SetConfigReg: bank=BbCp::Bank::Config; break;
        case BbCp::Op::SetContextReg: bank=BbCp::Bank::Context; break;
        case BbCp::Op::SetShReg: bank=BbCp::Bank::Sh; break;
        case BbCp::Op::SetUconfigReg: bank=BbCp::Bank::Uconfig; break;
        default: return;
        }
        start=bank+(packet[1]&0xffff);
        if (start>=BbCp::Bank::End) return;
        count=unsigned(std::min<size_t>(packet.size()-2,BbCp::Bank::End-start));
        decoder.Decode(packet,sink);
    }
    std::optional<unsigned> Mismatch(std::span<const std::uint32_t> actual) {
        for (unsigned i=0;i<count;++i) if (start+i>=actual.size() || actual[start+i]!=decoder.Registers()[start+i]) return start+i;
        return {};
    }
    bool Checked() const { return count!=0; }
};
}
