// SPDX-License-Identifier: GPL-2.0-or-later
// Included in game_bridge.mm's private namespace. No Cocoa work runs here;
// the presentation thread continues acknowledging frames while CP drains.
struct GraphicsCapture {
    bool paused=false;
    Gpu::Liverpool::CommandProcessor::CheckpointState frontend;
    std::unique_ptr<Gpu::Metal::MetalRenderer::CheckpointCapture> targets;
    std::vector<Gpu::Metal::MetalRenderer::CheckpointChunk> chunks;
    ~GraphicsCapture() {
        chunks.clear();targets.reset();
        if(paused)Gpu::Liverpool::Instance().ResumeAfterCheckpoint();
    }
};
void CheckpointError(char* out,size_t size,const std::string& why) {if(out && size)std::snprintf(out,size,"%s",why.c_str());}
int PauseGraphics(uint32_t timeout,void** out,char* error,size_t capacity) {
    if(!out)return 0;*out=nullptr;
    try {
        if(!ready.load(std::memory_order_acquire) || !renderer){CheckpointError(error,capacity,"Metal renderer is not ready.");return 0;}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
        auto capture=std::make_unique<GraphicsCapture>();std::string why;
        if(!Gpu::Liverpool::Instance().PauseForCheckpoint(capture->frontend,deadline,why)){CheckpointError(error,capacity,why);return 0;}
        capture->paused=true;capture->targets=renderer->CaptureCheckpoint(deadline,why);
        if(!capture->targets){CheckpointError(error,capacity,why);return 0;}
        std::vector<uint8_t> bytes;if(!Gpu::Metal::FrontendSnapshot::Encode(capture->frontend,bytes,why)){CheckpointError(error,capacity,why);return 0;}
        auto data=std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
        capture->chunks.push_back({0x1002,data->size(),[data](uint64_t offset,std::span<uint8_t> out,std::string& why) {
            if(offset>data->size() || out.size()>data->size()-offset){why="GPU frontend read is out of range.";return false;}
            if(!out.empty())std::memcpy(out.data(),data->data()+offset,out.size());why.clear();return true;
        }});
        const auto& targets=capture->targets->Chunks();capture->chunks.insert(capture->chunks.end(),targets.begin(),targets.end());
        *out=capture.release();CheckpointError(error,capacity,"");return 1;
    } catch(const std::exception& e){CheckpointError(error,capacity,e.what());return 0;}
}
void ReleaseGraphics(void* token) {delete static_cast<GraphicsCapture*>(token);}
int DescribeGraphics(void* token,BbCheckpointGraphicsChunk* out,size_t capacity,size_t* count) {
    if(!token || !count)return 1;const auto& chunks=static_cast<GraphicsCapture*>(token)->chunks;*count=chunks.size();
    if(!out)return capacity?1:0;if(capacity<chunks.size())return 1;
    for(size_t n=0;n<chunks.size();++n)out[n]={chunks[n].id,chunks[n].size};return 0;
}
int ReadGraphics(void* token,uint64_t id,uint64_t offset,void* bytes,size_t size,char* error,size_t capacity) {
    if(!token || (!bytes && size)){CheckpointError(error,capacity,"Invalid GPU capture read.");return 0;}
    try {
        const auto& chunks=static_cast<GraphicsCapture*>(token)->chunks;
        const auto c=std::find_if(chunks.begin(),chunks.end(),[&](const auto& v){return v.id==id;});
        std::string why;
        if(c==chunks.end()){CheckpointError(error,capacity,"Unknown GPU checkpoint chunk.");return 0;}
        const auto result=c->read(offset,{static_cast<uint8_t*>(bytes),size},why);CheckpointError(error,capacity,why);return result;
    } catch(const std::exception& e){CheckpointError(error,capacity,e.what());return 0;}
}
const BbCheckpointGraphicsHooks graphics_hooks{PauseGraphics,ReleaseGraphics,DescribeGraphics,ReadGraphics};
