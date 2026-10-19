// SPDX-License-Identifier: GPL-2.0-or-later
// Real Metal history warping with a translating, nonuniform HDR object.
#import <Metal/Metal.h>
#include "gpu/metal/temporal_aa.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
constexpr unsigned Extent=64;
using Pixels=std::vector<_Float16>;
using Bytes=std::vector<std::uint8_t>;
using Gpu::Metal::MotionInput;
using Gpu::Metal::TemporalAA;

Pixels Pattern(float dx,float dy) {
    Pixels pixels(Extent*Extent*4);
    for (unsigned y=0;y<Extent;++y) for (unsigned x=0;x<Extent;++x) {
        const auto k=(y*Extent+x)*4;
        float lx=x+.5f-20-dx,ly=y+.5f-20-dy;
        bool object=lx>=0 && ly>=0 && lx<24 && ly<24;
        pixels[k]=object ? 1.f+.3f*std::sin(lx*.83f+ly*.21f) : .15f;
        pixels[k+1]=object ? .4f+.17f*std::cos(lx*.31f-ly*.79f) : .2f;
        pixels[k+2]=object ? 3.f+.6f*std::sin(lx*.47f+ly*.61f) : 2.f;
        pixels[k+3]=.375f;
    }
    return pixels;
}
Bytes Vectors(float dx,float dy) {
    Bytes pixels(Extent*Extent);
    const auto code=[](float v) { return std::uint8_t(std::clamp(std::lround(v*255),0l,255l)); };
    for (unsigned i=0;i<Extent*Extent/4;++i) {
        pixels[i*4]=code(.5f+(-dx/Extent)/-.225f);
        pixels[i*4+1]=code(.5f+(-dy/Extent)/-.4f);
        pixels[i*4+2]=128; // z=10 => z/(z+10)=.5
        pixels[i*4+3]=255;
    }
    return pixels;
}
struct Fixture {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLTexture> color,depth,native;
    Gpu::Metal::SceneCamera camera;
    explicit Fixture(id<MTLDevice> d):device(d),queue([d newCommandQueue]) {
        camera.width=camera.height=Extent; camera.projection={1,1,1,-.1};
        for (unsigned i:{0u,5u,10u}) camera.view[i]=camera.inverse[i]=1;
        auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:Extent height:Extent mipmapped:NO];
        desc.storageMode=MTLStorageModeShared; desc.usage=MTLTextureUsageShaderRead;
        color=[d newTextureWithDescriptor:desc]; assert(color);
        desc.pixelFormat=MTLPixelFormatDepth32Float; desc.storageMode=MTLStorageModePrivate;
        desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        depth=[d newTextureWithDescriptor:desc]; assert(depth);
        native=Texture(MTLPixelFormatRGBA8Unorm,Extent/2,Extent/2);
    }
    id<MTLTexture> Texture(MTLPixelFormat format,unsigned width,unsigned height) {
        auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
        desc.storageMode=MTLStorageModeShared; desc.usage=MTLTextureUsageShaderRead;
        auto texture=[device newTextureWithDescriptor:desc]; assert(texture); return texture;
    }
    void Upload(const Bytes& data) {
        [native replaceRegion:MTLRegionMake2D(0,0,Extent/2,Extent/2) mipmapLevel:0
            withBytes:data.data() bytesPerRow:Extent*2];
    }
    MotionInput Input(std::uint64_t current=101,std::uint64_t previous=100) {
        return {(__bridge void*)native,current,previous,{-.225f,-.4f}};
    }
    Pixels Render(TemporalAA& taa,const Pixels& pixels,std::uint64_t frame,const MotionInput* input=nullptr) {
        [color replaceRegion:MTLRegionMake2D(0,0,Extent,Extent) mipmapLevel:0
            withBytes:pixels.data() bytesPerRow:Extent*8];
        auto command=[queue commandBuffer];
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.depthAttachment.texture=depth; pass.depthAttachment.clearDepth=.99;
        pass.depthAttachment.loadAction=MTLLoadActionClear; pass.depthAttachment.storeAction=MTLStoreActionStore;
        [[command renderCommandEncoderWithDescriptor:pass] endEncoding];
        taa.ObserveCamera(camera);
        auto result=(__bridge id<MTLTexture>)taa.Encode((__bridge void*)command,(__bridge void*)color,
            (__bridge void*)depth,frame,input);
        assert(result);
        auto buffer=[device newBufferWithLength:Extent*Extent*8 options:MTLResourceStorageModeShared];
        auto blit=[command blitCommandEncoder];
        [blit copyFromTexture:result sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(Extent,Extent,1) toBuffer:buffer destinationOffset:0
            destinationBytesPerRow:Extent*8 destinationBytesPerImage:Extent*Extent*8];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        const auto* data=static_cast<const _Float16*>(buffer.contents);
        Pixels output(data,data+Extent*Extent*4);
        for (unsigned i=0;i<Extent*Extent;++i) {
            for (unsigned c=0;c<4;++c) assert(std::isfinite(float(output[i*4+c])));
            assert(output[i*4+3]==pixels[i*4+3]);
        }
        // The history path must retain scene-linear values above display white.
        assert(float(output[(32*Extent+32)*4+2])>1.9f);
        taa.EndFrame(); return output;
    }
    Pixels Pair(const Pixels& first,const Pixels& second,const MotionInput* input,
                std::uint64_t frame=101,std::uint64_t* admitted=nullptr) {
        TemporalAA taa((__bridge void*)device);
        Render(taa,first,100);
        auto out=Render(taa,second,frame,input);
        if (admitted) *admitted=taa.NativeMotionFrames();
        return out;
    }
};
double Error(const Pixels& actual,const Pixels& expected,float dx,float dy) {
    double sum=0; unsigned count=0;
    for (unsigned y=0;y<Extent;++y) for (unsigned x=0;x<Extent;++x) {
        float lx=x+.5f-20-dx,ly=y+.5f-20-dy;
        if (lx<4 || ly<4 || lx>=20 || ly>=20) continue;
        for (unsigned c=0;c<3;++c) { sum+=std::abs(float(actual[(y*Extent+x)*4+c])-float(expected[(y*Extent+x)*4+c])); ++count; }
    }
    assert(count); return sum/count;
}
void Equal(const Pixels& a,const Pixels& b) {
    assert(a.size()==b.size());
    for (unsigned i=0;i<a.size();++i) assert(a[i]==b[i]);
}
}

int main() {
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice(); if (!device) return 77;
        Fixture f(device);
        const auto first=Pattern(0,0);
        for (const auto delta:std::vector<std::array<float,2>>{{4,0},{-4,0},{0,4},{0,-4},{1.25f,-1.5f}}) {
            const auto current=Pattern(delta[0],delta[1]);
            f.Upload(Vectors(delta[0],delta[1])); auto input=f.Input();
            auto camera=f.Pair(first,current,nullptr);
            std::uint64_t count=0;
            auto native=f.Pair(first,current,&input,101,&count); assert(count==1);
            const double a=Error(camera,current,delta[0],delta[1]),b=Error(native,current,delta[0],delta[1]);
            std::printf("native translation (%g,%g): camera error %.7f, native error %.7f\n",delta[0],delta[1],a,b);
            assert(a>.001 && b<a*.3);
        }
        const auto current=Pattern(4,0),camera=f.Pair(first,current,nullptr);
        auto input=f.Input();
        // Each invalid per-pixel input must reproduce the camera path exactly.
        for (unsigned kind=0;kind<5;++kind) {
            auto bytes=Vectors(4,0);
            for (unsigned i=0;i<bytes.size();i+=4) {
                if (kind==0) bytes[i+3]=0;
                if (kind==1) bytes[i+3]=128;
                if (kind==2) bytes[i]=0;
                if (kind==3) bytes[i+1]=255;
                if (kind==4) bytes[i+2]=0;
            }
            f.Upload(bytes); std::uint64_t count=0;
            Equal(f.Pair(first,current,&input,101,&count),camera); assert(count==1);
        }
        // Neutral 127/128 codes cannot replace precise stationary reprojection.
        f.Upload(Vectors(0,0));
        Equal(f.Pair(first,first,&input),f.Pair(first,first,nullptr));
        // Sparse coverage changes only its exact 2x2 scene blocks; other pixels
        // remain identical to the camera result. No bilinear alpha leakage.
        auto sparse=Vectors(4,0);
        for (unsigned y=0;y<Extent/2;++y) for (unsigned x=0;x<Extent/2;++x)
            sparse[(y*(Extent/2)+x)*4+3]=(x>=16 && x<20 && y>=14 && y<18) ? 255 : 0;
        f.Upload(sparse); const auto sparse_result=f.Pair(first,current,&input);
        double camera_error=0,native_error=0;
        for (unsigned y=0;y<Extent;++y) for (unsigned x=0;x<Extent;++x) for (unsigned c=0;c<3;++c) {
            const auto k=(y*Extent+x)*4+c;
            if (x>=32 && x<40 && y>=28 && y<36) {
                camera_error+=std::abs(float(camera[k])-float(current[k]));
                native_error+=std::abs(float(sparse_result[k])-float(current[k]));
            } else assert(sparse_result[k]==camera[k]);
        }
        assert(native_error<camera_error*.3);
        // Frame identity, descriptor and scale failures admit no native frame.
        f.Upload(Vectors(4,0));
        auto wrong_size=f.Texture(MTLPixelFormatRGBA8Unorm,Extent/4,Extent/2);
        auto wrong_format=f.Texture(MTLPixelFormatRGBA16Float,Extent/2,Extent/2);
        auto array_desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:Extent/2 height:Extent/2 mipmapped:NO];
        array_desc.textureType=MTLTextureType2DArray; array_desc.arrayLength=2;
        array_desc.storageMode=MTLStorageModeShared; array_desc.usage=MTLTextureUsageShaderRead;
        auto wrong_type=[device newTextureWithDescriptor:array_desc]; assert(wrong_type);
        for (unsigned kind=0;kind<10;++kind) {
            auto bad=input; std::uint64_t frame=101;
            switch (kind) {
            case 0: bad.current_frame=100; break;
            case 1: bad.previous_frame=99; break;
            case 2: frame=103; bad.current_frame=103; bad.previous_frame=102; break;
            case 3: bad.texture=nullptr; break;
            case 4: bad.texture=(__bridge void*)wrong_size; break;
            case 5: bad.texture=(__bridge void*)wrong_format; break;
            case 6: bad.uv_scale[0]=std::numeric_limits<float>::quiet_NaN(); break;
            case 7: bad.uv_scale[1]=std::numeric_limits<float>::infinity(); break;
            case 8: bad.uv_scale[0]=0; break;
            case 9: bad.texture=(__bridge void*)wrong_type; break;
            }
            std::uint64_t count=99;
            Equal(f.Pair(first,current,&bad,frame,&count),camera); assert(count==0);
        }
        // Cold history and an explicit invalidation cannot admit motion.
        TemporalAA taa((__bridge void*)device);
        auto cold=f.Input(100,99); Equal(f.Render(taa,first,100,&cold),first); assert(!taa.NativeMotionFrames());
        taa.Invalidate(); Equal(f.Render(taa,current,101,&input),current); assert(!taa.NativeMotionFrames());
        // Advancing history must update its identity, rather than accepting a
        // permanently repeated registration or tying motion to a fixed frame.
        TemporalAA advancing((__bridge void*)device),camera_only((__bridge void*)device);
        f.Render(advancing,first,100); f.Render(camera_only,first,100);
        for (unsigned frame=101;frame<=102;++frame) {
            const float dx=float(frame-100)*4;
            const auto moved=Pattern(dx,0); const auto next=f.Input(frame,frame-1);
            const auto native=f.Render(advancing,moved,frame,&next);
            const auto baseline=f.Render(camera_only,moved,frame);
            assert(Error(native,moved,dx,0)<Error(baseline,moved,dx,0)*.3);
            assert(advancing.NativeMotionFrames()==frame-100);
        }
        puts("Metal native motion: signed/subpixel HDR warping, sparse coverage, quantization, depth and frame/descriptor fallbacks passed");
    }
}
