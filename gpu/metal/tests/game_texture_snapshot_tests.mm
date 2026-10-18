// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/texture_snapshot.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <unistd.h>

int main() {
    @autoreleasepool {
        namespace T=Gpu::Metal::TextureSnapshot;
        namespace M=Gpu::Metal::MaterialSnapshot;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 77;
        id<MTLCommandQueue> queue=[device newCommandQueue];assert(queue);
        void* d=(__bridge void*)device;void* q=(__bridge void*)queue;
        char directory[]="/tmp/bb-metal-snapshot-XXXXXX";assert(mkdtemp(directory));
        const auto path=std::filesystem::path(directory)/"fixture.bbmat";
        std::string error;
        for(auto format:{MTLPixelFormatRGBA8Unorm,MTLPixelFormatRGBA8Unorm_sRGB}) {
            auto* descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:4 height:4 mipmapped:YES];
            descriptor.storageMode=MTLStorageModePrivate;
            descriptor.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget|MTLTextureUsagePixelFormatView;
            id<MTLTexture> texture=[device newTextureWithDescriptor:descriptor];assert(texture);
            void* t=(__bridge void*)texture;
            M::Texture original;original.width=4;original.height=4;original.format=format;
            for(unsigned size:{4,2,1}) {
                std::vector<std::uint8_t> bytes(size*size*4);
                for(std::size_t k=0;k<bytes.size();++k)bytes[k]=std::uint8_t(k*13+size);
                bytes[0]=64;bytes[1]=0;bytes[2]=7;bytes[3]=0; // opaque stain with zero transmittance
                original.mips.push_back(std::move(bytes));
            }
            assert(T::Restore(d,q,t,original,error));
            M::Texture actual;assert(T::Capture(d,q,t,actual,error) && actual==original);
            // File round trip and destructive live texture mutation must not
            // affect the archived alpha, sRGB bytes or lower mip levels.
            M::Snapshot snapshot;snapshot.game_hash="metal-stain-fixture";
            M::Material material;material.key=1;material.shader_hash=2;material.colour=actual;material.surface=actual;
            snapshot.materials.push_back(material);assert(M::Save(path,snapshot,error));
            auto cleared=original;for(auto& mip:cleared.mips)std::fill(mip.begin(),mip.end(),0);
            assert(T::Restore(d,q,t,cleared,error));
            assert(T::Capture(d,q,t,actual,error) && actual==cleared && actual!=original);
            M::Snapshot loaded;assert(M::Load(path,snapshot.game_hash,loaded,error));
            assert(T::Restore(d,q,t,loaded.materials[0].colour,error));
            assert(T::Capture(d,q,t,actual,error) && actual==original);
            auto invalid=original;invalid.mips.back().pop_back();
            assert(!T::Restore(d,q,t,invalid,error));
            assert(T::Capture(d,q,t,actual,error) && actual==original);
            auto preserved=actual;assert(!T::Capture(d,nullptr,t,actual,error) && actual==preserved);
        }
        std::filesystem::remove_all(directory);
        puts("Metal checkpoint textures: private GPU RGBA/sRGB, exact alpha/all mips and disk restore passed");
    }
}
