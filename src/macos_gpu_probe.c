/* SPDX-License-Identifier: GPL-2.0-or-later
 * CPU bring-up provider. Missing GPU imports remain unresolved and stop in
 * probe.c; this diagnostic executable cannot claim a successful rendered boot. */
#include "gpu/bbgpu.h"
#include <stdio.h>
#include <string.h>
void bbgpu_register_kernel(void) {}
void bbnet_configure(const char *a,const char *b,const char *c,const char *d) { (void)a;(void)b;(void)c;(void)d; }
void bbnet_set_image(unsigned char *image,uint64_t size) { (void)image;(void)size; }
void bbnet_initialize(void) {}
void bbmac_features_segment(uint64_t at,uint64_t size,uint32_t flags) { (void)at;(void)size;(void)flags; }
void bbmac_features_relative(uint64_t at,uint64_t target) { (void)at;(void)target; }
void bbmac_features_install(void) {}
int bbgpu_init(const BbGpuConfig *config) {
    (void)config;
    fputs("STOP: the direct Metal command processor is not connected; use --cpu-only for runtime diagnostics.\n",stderr);
    return -1;
}
uintptr_t bbgpu_resolve(const char *name) { (void)name; return 0; }
int bbgpu_handle_fault(void *context,void *address) { (void)context;(void)address; return 0; }
void bbgpu_dump_guest_writes(void *context) { (void)context; }
int bbgpu_text_input_begin(const char *a,const char *b) { (void)a;(void)b; return 0; }
int bbgpu_text_input_poll(char *out,uint64_t size) { if (out && size) *out=0; return 2; }
int bbgpu_overlay_captures_input(void) { return 0; }
void bbgpu_mouse_take(BbMouseInput *out) { memset(out,0,sizeof(*out)); }
void bbgpu_input_configure(int a,int32_t b,int32_t c) { (void)a;(void)b;(void)c; }
int bbgpu_input_reload_requested(void) { return 0; }
void bbgpu_mouse_camera_configure(int a,float b,float x,float y,int sync) { (void)a;(void)b;(void)x;(void)y;(void)sync; }
int bbgpu_audio_muted(void) { return 0; }
void bbgpu_prompts_configure(const char *const *inputs,int count) { (void)inputs;(void)count; }
unsigned bbgpu_symbol_count(void) { return 0; }
