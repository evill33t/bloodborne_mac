// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/raw_texture_snapshot.h"
#include "src/checkpoint/archive.h"
#include <cassert>
#include <bit>
#include <cstdio>
#include <cstring>
#include <unistd.h>
extern "C" void runtime_restart() {__builtin_abort();}

namespace {
using namespace Gpu::Metal::RawTextureSnapshot;
using Clock=std::chrono::steady_clock;
id<MTLTexture> Texture(id<MTLDevice> device,MTLPixelFormat format,MTLTextureType type) {
    auto* d=[MTLTextureDescriptor new];d.pixelFormat=format;d.textureType=type;
    d.width=8;d.height=type==MTLTextureTypeCube || type==MTLTextureTypeCubeArray?8:5;
    d.depth=type==MTLTextureType3D?4:1;
    d.arrayLength=type==MTLTextureTypeCubeArray?2:type==MTLTextureType2DArray?3:1;
    const bool depth=format==MTLPixelFormatDepth32Float_Stencil8 || format==MTLPixelFormatDepth32Float ||
        format==MTLPixelFormatDepth16Unorm || format==MTLPixelFormatStencil8;
    d.mipmapLevelCount=depth?1:3;d.storageMode=MTLStorageModePrivate;
    d.usage=type==MTLTextureType3D?MTLTextureUsageShaderRead:MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    id<MTLTexture> result=[device newTextureWithDescriptor:d];assert(result);return result;
}
std::vector<std::uint8_t> Pattern(const Plane& p,std::uint32_t format) {
    std::vector<std::uint8_t> bytes(p.size,0);
    for(std::uint32_t z=0;z<p.depth;++z)for(std::uint32_t y=0;y<p.height;++y)for(std::uint32_t x=0;x<p.width;++x) {
        auto* pixel=bytes.data()+z*p.image_bytes+y*p.row_bytes+x*p.texel_bytes;
        if(p.aspect==Aspect::Depth && format!=MTLPixelFormatDepth16Unorm) {
            const float value=float((x+13*y+7*z+3*p.slice)%97)/96;
            std::memcpy(pixel,&value,4);
        } else for(unsigned b=0;b<p.texel_bytes;++b)pixel[b]=std::uint8_t(x*37+y*59+z*19+p.slice*11+p.level*17+b*41);
    }
    return bytes;
}
}
int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 77;
        id<MTLCommandQueue> queue=[device newCommandQueue];assert(queue);
        const auto d=(__bridge void*)device,q=(__bridge void*)queue;
        char directory[]="/tmp/bb-raw-texture-checkpoint-XXXXXX";assert(mkdtemp(directory));
        const auto path=std::filesystem::path(directory)/"texture.bbstate";
        unsigned count=0;
        for(const auto& [format,type]:{
            std::pair{MTLPixelFormatRGBA8Unorm_sRGB,MTLTextureType2D},
            std::pair{MTLPixelFormatBGRA8Unorm,MTLTextureType2D},
            std::pair{MTLPixelFormatRGBA8Uint,MTLTextureType2DArray},
            std::pair{MTLPixelFormatRGB10A2Unorm,MTLTextureType2D},
            std::pair{MTLPixelFormatRG11B10Float,MTLTextureType2D},
            std::pair{MTLPixelFormatRGBA16Float,MTLTextureType3D},
            std::pair{MTLPixelFormatR32Uint,MTLTextureType2D},
            std::pair{MTLPixelFormatRG32Float,MTLTextureTypeCube},
            std::pair{MTLPixelFormatRGBA32Uint,MTLTextureTypeCubeArray},
            std::pair{MTLPixelFormatDepth32Float_Stencil8,MTLTextureType2DArray},
            std::pair{MTLPixelFormatDepth32Float,MTLTextureType2D},
            std::pair{MTLPixelFormatDepth16Unorm,MTLTextureType2D},
            std::pair{MTLPixelFormatStencil8,MTLTextureType2D}}) {
            auto original=Texture(device,format,type);auto restored=Texture(device,format,type);
            Descriptor descriptor;std::vector<Plane> planes;std::string error;
            assert(Describe((__bridge void*)original,descriptor,planes,error));
            std::vector<Plane> layout;assert(Layout(d,descriptor,layout,error) && layout==planes);
            auto invalid_descriptor=descriptor;invalid_descriptor.levels=33;
            assert(!Layout(d,invalid_descriptor,layout,error) && layout==planes);
            invalid_descriptor=descriptor;invalid_descriptor.type=0xffffffff;
            assert(!Layout(d,invalid_descriptor,layout,error) && layout==planes);
            Bb::Checkpoint::Archive archive;archive.identity="raw-texture-planes-v1";
            for(std::size_t n=0;n<planes.size();++n) {
                const auto expected=Pattern(planes[n],format);const auto deadline=Clock::now()+std::chrono::seconds(5);
                assert(Restore(d,q,(__bridge void*)original,n,expected,deadline,error));
            }
            for(std::size_t n=0;n<planes.size();++n) {
                std::vector<std::uint8_t> bytes;
                assert(Capture(d,q,(__bridge void*)original,n,bytes,Clock::now()+std::chrono::seconds(5),error));
                assert(bytes==Pattern(planes[n],format));
                archive.sections.push_back({Bb::Checkpoint::SectionKind::Gpu,1,n,std::move(bytes)});
            }
            assert(Bb::Checkpoint::Save(path,archive,error));
            Bb::Checkpoint::Archive loaded;assert(Bb::Checkpoint::Load(path,archive.identity,loaded,error));
            for(std::size_t n=0;n<planes.size();++n)
                assert(Restore(d,q,(__bridge void*)restored,n,loaded.sections[n].bytes,Clock::now()+std::chrono::seconds(5),error));
            for(std::size_t n=0;n<planes.size();++n) {
                std::vector<std::uint8_t> bytes;
                assert(Capture(d,q,(__bridge void*)restored,n,bytes,Clock::now()+std::chrono::seconds(5),error));
                assert(bytes==loaded.sections[n].bytes);
            }
            const std::uint8_t invalid[]{0};
            assert(!Restore(d,q,(__bridge void*)restored,0,invalid,Clock::now()+std::chrono::seconds(5),error));
            std::vector<std::uint8_t> unchanged{7,3,9};
            assert(!Capture(d,q,(__bridge void*)restored,planes.size(),unchanged,Clock::now()+std::chrono::seconds(5),error));
            assert((unchanged==std::vector<std::uint8_t>{7,3,9}));
            assert(!Capture(d,q,(__bridge void*)restored,0,unchanged,Clock::now()-std::chrono::seconds(1),error));
            assert((unchanged==std::vector<std::uint8_t>{7,3,9}));
            assert(Capture(d,q,(__bridge void*)restored,0,unchanged,Clock::now()+std::chrono::seconds(5),error));
            assert(unchanged==loaded.sections[0].bytes);++count;
        }
        std::filesystem::remove_all(directory);
        std::printf("Raw Metal checkpoint: %u format/type combinations, every mip/slice/depth/stencil plane, disk roundtrip and rejected-write preservation passed.\n",count);
    }
}
