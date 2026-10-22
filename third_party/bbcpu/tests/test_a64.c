/* tests/test_a64.c (arm64 hosts): src/cpu/a64.h encodings against the system assembler. */
#include "../src/cpu/a64.h"
#include <stdio.h>

extern const uint32_t ref_start[], ref_end[];
__asm__(".text\n.p2align 2\n.globl _ref_start\n_ref_start:\n"
        "movz x3, #0x1234, lsl #16\n movk w5, #0xbeef\n movn x7, #5\n"
        "mov x1, x2\n mov w1, w2\n add x0, x1, #12\n sub w3, w4, #1, lsl #12\n"
        "adds x0, x1, x2\n subs w3, w4, w5\n add x6, x7, x8, lsl #3\n sub x6, x7, x8, asr #2\n"
        "adc x1, x2, x3\n adcs w1, w2, w3\n sbc x1, x2, x3\n sbcs w1, w2, w3\n"
        "and x1, x2, x3\n ands w1, w2, w3\n eor x4, x5, x6\n bic x4, x5, x6\n mvn w4, w5\n orr x9, x10, x11, lsr #4\n"
        "lsl x1, x2, x3\n lsr w1, w2, w3\n asr x1, x2, x3\n ror w1, w2, w3\n"
        "ubfx x1, x2, #8, #8\n sbfx w1, w2, #4, #12\n bfi x1, x2, #8, #16\n bfxil w1, w2, #3, #5\n"
        "lsl x1, x2, #5\n lsr w1, w2, #7\n asr x1, x2, #63\n sxtb x1, w2\n sxth w1, w2\n sxtw x1, w2\n uxtb w1, w2\n uxth w1, w2\n"
        "madd x1, x2, x3, x4\n msub w1, w2, w3, w4\n mul x1, x2, x3\n smulh x1, x2, x3\n umulh x1, x2, x3\n smull x1, w2, w3\n umull x1, w2, w3\n"
        "udiv x1, x2, x3\n sdiv w1, w2, w3\n clz x1, x2\n rbit w1, w2\n rev x1, x2\n rev w1, w2\n rev16 w1, w2\n"
        "csel x1, x2, x3, lt\n csinc w1, w2, w3, eq\n cset x1, hi\n mrs x5, nzcv\n msr nzcv, x6\n"
        "ldrb w1, [x2, #3]\n ldrh w1, [x2, #6]\n ldr w1, [x2, #12]\n ldr x1, [x2, #24]\n strb w1, [x2, #3]\n str x1, [x2, #32]\n"
        "ldr x1, [x2, x3]\n strh w1, [x2, x3]\n ldrsb x1, [x2, x3]\n ldrsw x1, [x2, x3]\n"
        "stp x29, x30, [sp, #-16]!\n ldp x29, x30, [sp], #16\n stp x19, x20, [x0, #32]\n ldp x19, x20, [x0, #48]\n"
        "ldr q3, [x1, #64]\n str q3, [x1, #32]\n"
        "casal x1, x2, [x3]\n casal w1, w2, [x3]\n swpal x1, x2, [x3]\n ldaddal w1, w2, [x3]\n ldclral x1, x2, [x3]\n ldeoral x1, x2, [x3]\n ldsetal x1, x2, [x3]\n dmb ish\n"
        "blr x16\n br x17\n ret\n"
        "fmul s1, s2, s3\n fdiv d1, d2, d3\n fadd s4, s5, s6\n fsub d4, d5, d6\n fsqrt s1, s2\n fsqrt d1, d2\n"
        "fmul v1.4s, v2.4s, v3.4s\n fdiv v1.2d, v2.2d, v3.2d\n fadd v4.4s, v5.4s, v6.4s\n fsub v4.2d, v5.2d, v6.2d\n"
        "and v1.16b, v2.16b, v3.16b\n orr v1.16b, v2.16b, v3.16b\n eor v1.16b, v2.16b, v3.16b\n bic v1.16b, v2.16b, v3.16b\n"
        "mov v1.s[2], v3.s[1]\n mov v1.d[1], v3.d[0]\n mov v2.s[3], w5\n mov v2.d[0], x5\n mov w3, v4.s[2]\n mov x3, v4.d[1]\n"
        "mov v7.16b, v8.16b\n movi v9.2d, #0\n fcmp s1, s2\n fcmp d1, d2\n fcsel s1, s2, s3, gt\n fcsel d1, d2, d3, lt\n"
        "scvtf s1, w2\n scvtf d1, x2\n fcvtzs w1, s2\n fcvtzs x1, d2\n fcvt d1, s2\n fcvt s1, d2\n"
        "fcmgt v1.4s, v2.4s, v3.4s\n fcmgt v1.2d, v2.2d, v3.2d\n bsl v1.16b, v2.16b, v3.16b\n"
        "ldr s1, [x2, #8]\n ldr d1, [x2, #16]\n ldr q1, [x2, #32]\n str s1, [x2, #4]\n str d1, [x2, #8]\n str q1, [x2, #48]\n"
        "fmov w1, s2\n fmov x1, d2\n fmov s1, w2\n fmov d1, x2\n"
        "adr x17, .-4\n"
        "eor x8, x8, #0x20000000\n orr x1, x2, #0x800\n and x3, x4, #0xffffffffdfffffff\n and w1, w2, #0xff\n ands x1, x2, #0xffff\n and x1, x2, #0xf0000000\n"
        "add v1.16b, v2.16b, v3.16b\n add v1.8h, v2.8h, v3.8h\n add v1.4s, v2.4s, v3.4s\n add v1.2d, v2.2d, v3.2d\n"
        "sub v1.16b, v2.16b, v3.16b\n sub v1.8h, v2.8h, v3.8h\n sub v1.4s, v2.4s, v3.4s\n sub v1.2d, v2.2d, v3.2d\n"
        "mul v1.8h, v2.8h, v3.8h\n mul v1.4s, v2.4s, v3.4s\n"
        "cmeq v1.16b, v2.16b, v3.16b\n cmeq v1.8h, v2.8h, v3.8h\n cmeq v1.4s, v2.4s, v3.4s\n cmeq v1.2d, v2.2d, v3.2d\n"
        "cmgt v1.16b, v2.16b, v3.16b\n cmgt v1.8h, v2.8h, v3.8h\n cmgt v1.4s, v2.4s, v3.4s\n cmgt v1.2d, v2.2d, v3.2d\n"
        "smax v1.16b, v2.16b, v3.16b\n smax v1.8h, v2.8h, v3.8h\n smax v1.4s, v2.4s, v3.4s\n"
        "smin v1.16b, v2.16b, v3.16b\n smin v1.8h, v2.8h, v3.8h\n smin v1.4s, v2.4s, v3.4s\n"
        "umax v1.16b, v2.16b, v3.16b\n umax v1.8h, v2.8h, v3.8h\n umax v1.4s, v2.4s, v3.4s\n"
        "umin v1.16b, v2.16b, v3.16b\n umin v1.8h, v2.8h, v3.8h\n umin v1.4s, v2.4s, v3.4s\n"
        "sqadd v1.16b, v2.16b, v3.16b\n sqadd v1.8h, v2.8h, v3.8h\n uqadd v1.16b, v2.16b, v3.16b\n uqadd v1.8h, v2.8h, v3.8h\n"
        "sqsub v1.16b, v2.16b, v3.16b\n sqsub v1.8h, v2.8h, v3.8h\n uqsub v1.16b, v2.16b, v3.16b\n uqsub v1.8h, v2.8h, v3.8h\n"
        "urhadd v1.16b, v2.16b, v3.16b\n urhadd v1.8h, v2.8h, v3.8h\n"
        "faddp v1.4s, v2.4s, v3.4s\n faddp v1.2d, v2.2d, v3.2d\n fcmeq v1.4s, v2.4s, v3.4s\n fcmeq v1.2d, v2.2d, v3.2d\n"
        "fcmge v1.4s, v2.4s, v3.4s\n fcmge v1.2d, v2.2d, v3.2d\n fcmgt v1.4s, v2.4s, v3.4s\n fcmgt v1.2d, v2.2d, v3.2d\n"
        "zip1 v1.16b, v2.16b, v3.16b\n zip1 v1.8h, v2.8h, v3.8h\n zip1 v1.4s, v2.4s, v3.4s\n zip1 v1.2d, v2.2d, v3.2d\n"
        "zip2 v1.16b, v2.16b, v3.16b\n zip2 v1.8h, v2.8h, v3.8h\n zip2 v1.4s, v2.4s, v3.4s\n zip2 v1.2d, v2.2d, v3.2d\n"
        "uzp1 v1.4s, v2.4s, v3.4s\n uzp2 v1.4s, v2.4s, v3.4s\n trn1 v1.4s, v2.4s, v3.4s\n trn2 v1.4s, v2.4s, v3.4s\n"
        "tbl v1.16b, {v2.16b}, v3.16b\n orn v1.16b, v2.16b, v3.16b\n"
        "mvn v1.16b, v2.16b\n cnt v1.8b, v2.8b\n addv b1, v2.8b\n"
        "cmlt v1.16b, v2.16b, #0\n cmlt v1.8h, v2.8h, #0\n cmlt v1.4s, v2.4s, #0\n cmlt v1.2d, v2.2d, #0\n"
        "fsqrt v1.4s, v2.4s\n fsqrt v1.2d, v2.2d\n scvtf v1.4s, v2.4s\n fcvtzs v1.4s, v2.4s\n fcvtl v1.2d, v2.2s\n fcvtn v1.2s, v2.2d\n"
        "sqxtn v1.4h, v2.4s\n sqxtn2 v1.8h, v2.4s\n sqxtun v1.4h, v2.4s\n sqxtun2 v1.8h, v2.4s\n"
        "sqxtn v1.8b, v2.8h\n sqxtn2 v1.16b, v2.8h\n sqxtun v1.8b, v2.8h\n sqxtun2 v1.16b, v2.8h\n"
        "sxtl v1.8h, v2.8b\n sxtl v1.4s, v2.4h\n sxtl v1.2d, v2.2s\n uxtl v1.8h, v2.8b\n uxtl v1.4s, v2.4h\n uxtl v1.2d, v2.2s\n"
        "ld1r {v1.4s}, [x2]\n ld1r {v1.2d}, [x2]\n"
        "shl v1.4s, v2.4s, #3\n shl v1.2d, v2.2d, #40\n shl v1.8h, v2.8h, #1\n ushr v1.4s, v2.4s, #31\n ushr v1.2d, v2.2d, #63\n"
        "ushr v1.16b, v2.16b, #7\n sshr v1.8h, v2.8h, #15\n sshr v1.4s, v2.4s, #1\n usra v1.8h, v2.8h, #7\n usra v1.4s, v2.4s, #14\n usra v1.2d, v2.2d, #28\n"
        "ext v1.16b, v2.16b, v3.16b, #5\n movi v1.16b, #0x8f\n movi v1.4s, #0x80, lsl #24\n mvni v1.4s, #0x80, lsl #24\n movi v1.4s, #0x4f, lsl #24\n fmov v1.4s, #1.0\n"
        "mov v1.b[3], v2.b[7]\n mov v1.h[5], v2.h[1]\n mov v1.b[2], w5\n mov v1.h[7], wzr\n mov v1.s[1], wzr\n umov w3, v4.b[8]\n umov w3, v4.h[3]\n"
        "dup v1.4s, v2.s[0]\n dup v1.2d, v2.d[1]\n dup v1.16b, w3\n dup v1.4s, w3\n"
        "fmov s1, s17\n fmov d2, d30\n csinv x1, x2, x3, lo\n csinv w1, w2, wzr, hs\n mrs x5, cntvct_el0\n cmp x1, #12\n cmp w2, #4095\n cmn x3, #1\n"
        ".globl _ref_end\n_ref_end:\n");

int main(void) {
    uint32_t buf[512];
    A64 a = {buf, buf, buf + 512}, *e = &a;
    a64_movz(e, 1, 3, 0x1234, 16); a64_movk(e, 0, 5, 0xbeef, 0); a64_movn(e, 1, 7, 5, 0);
    a64_mov(e, 1, 1, 2); a64_mov(e, 0, 1, 2); a64_add_imm(e, 1, 0, 1, 12, 0); a64_sub_imm(e, 0, 3, 4, 1, 1);
    a64_adds(e, 1, 0, 1, 2); a64_subs(e, 0, 3, 4, 5); a64_arith(e, 0, 1, 6, 7, 8, 0, 3); a64_arith(e, 2, 1, 6, 7, 8, 2, 2);
    a64_adc(e, 1, 0, 1, 2, 3); a64_adc(e, 0, 1, 1, 2, 3); a64_sbc(e, 1, 0, 1, 2, 3); a64_sbc(e, 0, 1, 1, 2, 3);
    a64_and(e, 1, 1, 2, 3); a64_ands(e, 0, 1, 2, 3); a64_eor(e, 1, 4, 5, 6); a64_bic(e, 1, 4, 5, 6); a64_mvn(e, 0, 4, 5); a64_logic(e, 1, 0, 1, 9, 10, 11, 1, 4);
    a64_shiftv(e, 0, 1, 1, 2, 3); a64_shiftv(e, 1, 0, 1, 2, 3); a64_shiftv(e, 2, 1, 1, 2, 3); a64_shiftv(e, 3, 0, 1, 2, 3);
    a64_ubfx(e, 1, 1, 2, 8, 8); a64_sbfx(e, 0, 1, 2, 4, 12); a64_bfi(e, 1, 1, 2, 8, 16); a64_bfxil(e, 0, 1, 2, 3, 5);
    a64_lsl_imm(e, 1, 1, 2, 5); a64_lsr_imm(e, 0, 1, 2, 7); a64_asr_imm(e, 1, 1, 2, 63); a64_sxt(e, 1, 1, 2, 1); a64_sxt(e, 0, 1, 2, 2); a64_sxt(e, 1, 1, 2, 4); a64_uxt(e, 1, 2, 1); a64_uxt(e, 1, 2, 2);
    a64_madd(e, 1, 1, 2, 3, 4); a64_msub(e, 0, 1, 2, 3, 4); a64_mul(e, 1, 1, 2, 3); a64_smulh(e, 1, 2, 3); a64_umulh(e, 1, 2, 3); a64_smull(e, 1, 2, 3); a64_umull(e, 1, 2, 3);
    a64_udiv(e, 1, 1, 2, 3); a64_sdiv(e, 0, 1, 2, 3); a64_clz(e, 1, 1, 2); a64_rbit(e, 0, 1, 2); a64_rev(e, 1, 1, 2); a64_rev(e, 0, 1, 2); a64_rev16(e, 0, 1, 2);
    a64_csel(e, 1, 1, 2, 3, CC_LT); a64_csinc(e, 0, 1, 2, 3, CC_EQ); a64_cset(e, 1, 1, CC_HI); a64_mrs_nzcv(e, 5); a64_msr_nzcv(e, 6);
    a64_ldr_uoff(e, 0, 1, 2, 3); a64_ldr_uoff(e, 1, 1, 2, 6); a64_ldr_uoff(e, 2, 1, 2, 12); a64_ldr_uoff(e, 3, 1, 2, 24); a64_str_uoff(e, 0, 1, 2, 3); a64_str_uoff(e, 3, 1, 2, 32);
    a64_ldr_reg(e, 3, 1, 2, 3); a64_str_reg(e, 1, 1, 2, 3); a64_ldrs_reg(e, 0, 1, 2, 3); a64_ldrs_reg(e, 2, 1, 2, 3);
    a64_stp_pre(e, 29, 30, SP, -16); a64_ldp_post(e, 29, 30, SP, 16); a64_stp(e, 19, 20, 0, 32); a64_ldp(e, 19, 20, 0, 48);
    a64_ldr_q(e, 3, 1, 64); a64_str_q(e, 3, 1, 32);
    a64_casal(e, 3, 1, 2, 3); a64_casal(e, 2, 1, 2, 3); a64_swpal(e, 3, 1, 2, 3); a64_ldopal(e, 0, 2, 1, 2, 3); a64_ldopal(e, 1, 3, 1, 2, 3); a64_ldopal(e, 2, 3, 1, 2, 3); a64_ldopal(e, 3, 3, 1, 2, 3); a64_dmb_ish(e);
    a64_blr(e, 16); a64_br(e, 17); a64_ret(e);
    a64_fop_s(e, 0, 0, 1, 2, 3); a64_fop_s(e, 1, 1, 1, 2, 3); a64_fop_s(e, 2, 0, 4, 5, 6); a64_fop_s(e, 3, 1, 4, 5, 6); a64_fsqrt_s(e, 0, 1, 2); a64_fsqrt_s(e, 1, 1, 2);
    a64_fop_v(e, 0, 0, 1, 2, 3); a64_fop_v(e, 1, 1, 1, 2, 3); a64_fop_v(e, 2, 0, 4, 5, 6); a64_fop_v(e, 3, 1, 4, 5, 6);
    a64_vlogic(e, 0, 1, 2, 3); a64_vlogic(e, 1, 1, 2, 3); a64_vlogic(e, 2, 1, 2, 3); a64_vlogic(e, 3, 1, 2, 3);
    a64_ins_elem(e, 4, 1, 2, 3, 1); a64_ins_elem(e, 8, 1, 1, 3, 0); a64_ins_gpr(e, 4, 2, 3, 5); a64_ins_gpr(e, 8, 2, 0, 5); a64_umov(e, 4, 3, 4, 2); a64_umov(e, 8, 3, 4, 1);
    a64_vmov(e, 7, 8); a64_vzero(e, 9); a64_fcmp(e, 0, 1, 2); a64_fcmp(e, 1, 1, 2); a64_fcsel(e, 0, 1, 2, 3, CC_GT); a64_fcsel(e, 1, 1, 2, 3, CC_LT);
    a64_scvtf(e, 0, 0, 1, 2); a64_scvtf(e, 1, 1, 1, 2); a64_fcvtzs(e, 0, 0, 1, 2); a64_fcvtzs(e, 1, 1, 1, 2); a64_fcvt_sd(e, 1, 1, 2); a64_fcvt_sd(e, 0, 1, 2);
    a64_fcmgt_v(e, 0, 1, 2, 3); a64_fcmgt_v(e, 1, 1, 2, 3); a64_bsl(e, 1, 2, 3);
    a64_ldr_v(e, 2, 1, 2, 8); a64_ldr_v(e, 3, 1, 2, 16); a64_ldr_v(e, 4, 1, 2, 32); a64_str_v(e, 2, 1, 2, 4); a64_str_v(e, 3, 1, 2, 8); a64_str_v(e, 4, 1, 2, 48);
    a64_fmov_to_gpr(e, 0, 1, 2); a64_fmov_to_gpr(e, 1, 1, 2); a64_fmov_from_gpr(e, 0, 1, 2); a64_fmov_from_gpr(e, 1, 1, 2);
    a64_adr(e, 17, -4);
    a64_eor_bit(e, 8, 8, 29); a64_orr_bit(e, 1, 2, 11); a64_bic_bit(e, 3, 4, 29); a64_logic_imm(e, 0, 0, 1, 2, 0, 8); a64_logic_imm(e, 3, 1, 1, 2, 0, 16); a64_logic_imm(e, 0, 1, 1, 2, 28, 4);
    { static const uint32_t three[] = {V_ADD_16B, V_ADD_8H, V_ADD_4S, V_ADD_2D, V_SUB_16B, V_SUB_8H, V_SUB_4S, V_SUB_2D,
          V_MUL_8H, V_MUL_4S, V_CMEQ_16B, V_CMEQ_8H, V_CMEQ_4S, V_CMEQ_2D, V_CMGT_16B, V_CMGT_8H, V_CMGT_4S, V_CMGT_2D,
          V_SMAX_16B, V_SMAX_8H, V_SMAX_4S, V_SMIN_16B, V_SMIN_8H, V_SMIN_4S, V_UMAX_16B, V_UMAX_8H, V_UMAX_4S,
          V_UMIN_16B, V_UMIN_8H, V_UMIN_4S, V_SQADD_16B, V_SQADD_8H, V_UQADD_16B, V_UQADD_8H,
          V_SQSUB_16B, V_SQSUB_8H, V_UQSUB_16B, V_UQSUB_8H, V_URHADD_16B, V_URHADD_8H,
          V_FADDP_4S, V_FADDP_2D, V_FCMEQ_4S, V_FCMEQ_2D, V_FCMGE_4S, V_FCMGE_2D, V_FCMGT_4S, V_FCMGT_2D,
          V_ZIP1_16B, V_ZIP1_8H, V_ZIP1_4S, V_ZIP1_2D, V_ZIP2_16B, V_ZIP2_8H, V_ZIP2_4S, V_ZIP2_2D,
          V_UZP1_4S, V_UZP2_4S, V_TRN1_4S, V_TRN2_4S, V_TBL_16B, V_ORN};
      for (unsigned i = 0; i < sizeof(three) / sizeof(*three); ++i) a64_v3(e, three[i], 1, 2, 3);
      static const uint32_t two[] = {V_NOT_16B, V_CNT_8B, V_ADDV_8B, V_CMLT0_16B, V_CMLT0_8H, V_CMLT0_4S, V_CMLT0_2D,
          V_FSQRT_4S, V_FSQRT_2D, V_SCVTF_4S, V_FCVTZS_4S, V_FCVTL_2D, V_FCVTN_2S,
          V_SQXTN_4H, V_SQXTN2_8H, V_SQXTUN_4H, V_SQXTUN2_8H, V_SQXTN_8B, V_SQXTN2_16B, V_SQXTUN_8B, V_SQXTUN2_16B,
          V_SXTL_8H, V_SXTL_4S, V_SXTL_2D, V_UXTL_8H, V_UXTL_4S, V_UXTL_2D, V_LD1R_4S, V_LD1R_2D};
      for (unsigned i = 0; i < sizeof(two) / sizeof(*two); ++i) a64_v2(e, two[i], 1, 2); }
    a64_vshl(e, 4, 1, 2, 3); a64_vshl(e, 8, 1, 2, 40); a64_vshl(e, 2, 1, 2, 1); a64_vushr(e, 4, 1, 2, 31); a64_vushr(e, 8, 1, 2, 63);
    a64_vushr(e, 1, 1, 2, 7); a64_vsshr(e, 2, 1, 2, 15); a64_vsshr(e, 4, 1, 2, 1); a64_vusra(e, 2, 1, 2, 7); a64_vusra(e, 4, 1, 2, 14); a64_vusra(e, 8, 1, 2, 28);
    a64_ext(e, 1, 2, 3, 5); a64_movi_16b(e, 1, 0x8f); a64_movi_4s(e, 0, 1, 0x80, 3); a64_movi_4s(e, 1, 1, 0x80, 3); a64_movi_4s(e, 0, 1, 0x4f, 3); a64_fmov_one_4s(e, 1);
    a64_ins_elem(e, 1, 1, 3, 2, 7); a64_ins_elem(e, 2, 1, 5, 2, 1); a64_ins_gpr(e, 1, 1, 2, 5); a64_ins_gpr(e, 2, 1, 7, XZR); a64_ins_gpr(e, 4, 1, 1, XZR); a64_umov(e, 1, 3, 4, 8); a64_umov(e, 2, 3, 4, 3);
    a64_dup_elem(e, 4, 1, 2, 0); a64_dup_elem(e, 8, 1, 2, 1); a64_dup_gpr(e, 1, 1, 3); a64_dup_gpr(e, 4, 1, 3);
    a64_fmov_reg(e, 0, 1, 17); a64_fmov_reg(e, 1, 2, 30);
    a64_csinv(e, 1, 1, 2, 3, CC_LO); a64_csinv(e, 0, 1, 2, XZR, CC_HS); a64_mrs_cntvct(e, 5); a64_subs_imm(e, 1, XZR, 1, 12); a64_subs_imm(e, 0, XZR, 2, 4095); a64_adds_imm(e, 1, XZR, 3, 1);
    const int n = (int)(ref_end - ref_start), got = (int)(a.at - buf);
    int bad = n != got;
    if (bad) printf("count: reference %d, emitter %d\n", n, got);
    for (int i = 0; i < n && i < got; ++i)
        if (buf[i] != ref_start[i]) { printf("#%d: reference %08x, emitter %08x\n", i, ref_start[i], buf[i]); bad = 1; }
    puts(bad ? "a64: FAILED" : "a64: ok");
    return bad;
}
