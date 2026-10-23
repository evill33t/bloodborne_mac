// SPDX-License-Identifier: GPL-2.0-or-later
// Include in renderer Impl. No guest descriptor or payload CPU read, source
// copy, UD mutation, extra encoder/commit, wait or drain is added.
std::string wind_consumed_root;
unsigned wind_consumed_pass=0; // 0 all, 1 color, 2 depth-only
WindConsumed::Window wind_consumed_window;
std::vector<id<MTLBuffer>> wind_consumed_records;
std::unordered_map<const Program*,bool> wind_consumed_programs;
void InitWindConsumedCapture() {
  const auto frames=WindConsumed::ConfiguredFrames(std::getenv("BB_METAL_WIND_CONSUMED_FRAMES"));
  const char* path=std::getenv("BB_METAL_WIND_CONSUMED_DIR");
  if(!frames || !path || !*path)return;
  unsigned pass_kind=0;
  if(const char* kind=std::getenv("BB_METAL_WIND_CONSUMED_PASS")) {
    if(!std::strcmp(kind,"color"))pass_kind=1;
    else if(!std::strcmp(kind,"depth"))pass_kind=2;
    else if(std::strcmp(kind,"all")) {DC_INFO("gpu: consumed-wind pass selector rejected");return;}
  }
  std::error_code error;const auto root=std::filesystem::weakly_canonical(path,error);
  if(error || !CapturePathAllowed(root)) {DC_INFO("gpu: consumed-wind directory rejected");return;}
  std::filesystem::create_directories(root,error);
  if(error) {DC_INFO("gpu: consumed-wind directory creation failed");return;}
  // Allocation succeeds for the entire bounded observation before any shader
  // can select the diagnostic ABI. A record is never reset/reused after use.
  std::vector<id<MTLBuffer>> records;records.reserve(frames*WindConsumed::DrawsPerFrame);
  for(unsigned k=0;k<frames*WindConsumed::DrawsPerFrame;++k) {
    id<MTLBuffer> record=[device newBufferWithLength:sizeof(WindConsumed::Record)
        options:MTLResourceStorageModeShared];
    if(!record || !record.contents) {DC_INFO("gpu: consumed-wind allocation failed; diagnostic disabled");return;}
    records.push_back(record);
  }
  wind_consumed_records=std::move(records);wind_consumed_window=WindConsumed::Window(frames);
  wind_consumed_pass=pass_kind;
  wind_consumed_root=root.string();
  DC_INFO("gpu: consumed-wind diagnostic at %s: <=%u frames/%u draws per frame; vid&63==0; private VS buffer14; windXYZ/W140 and view-projection words",
      wind_consumed_root.c_str(),frames,WindConsumed::DrawsPerFrame);
}
bool WindConsumedRequested(const Program& program,bool ordinary,bool color_pass=false) {
  if(wind_consumed_root.empty() || !ordinary || program.stage!=Stage::Vertex ||
      program.hash!=WindConsumed::ShaderHash)return false;
  if((wind_consumed_pass==1 && !color_pass) || (wind_consumed_pass==2 && color_pass))return false;
  auto [entry,inserted]=wind_consumed_programs.try_emplace(&program,false);
  if(inserted)entry->second=Shader::VertexWindConsumedEligible(program);
  return entry->second && wind_consumed_window.Request(frame_count,trace);
}
// Final actual-consumer binding, after every possible setup/index/profiling
// cut and immediately before draw encode. The renderer lock serializes the
// frame, reservation and encode. Only the private record is read on the CPU.
bool BindWindConsumedDraw(const Program& program,const u32* prepared_ud,u64 vs_key,
    u64 pipeline_key,u64 draw_map_generation,const DrawCall& draw,
    unsigned width=0,unsigned height=0,unsigned color_targets=0) {
  if(!cmd || !enc || program.hash!=WindConsumed::ShaderHash ||
      wind_consumed_window.total>=wind_consumed_records.size() ||
      !wind_consumed_window.Reserve(frame_count))return false;
  id<MTLBuffer> record=wind_consumed_records[wind_consumed_window.total-1];
  const auto initial=WindConsumed::InitialRecord();
  std::memcpy(record.contents,initial.data(),sizeof(initial));
  [enc setVertexBuffer:record offset:0 atIndex:WindConsumed::Binding];
  const auto filename=wind_consumed_root+"/wind_consumed_f"+std::to_string(frame_count)+
      "_draw"+std::to_string(draw_count);
  NSMutableArray* user=[NSMutableArray new];for(unsigned k=0;k<16;++k)[user addObject:@(prepared_ud[k])];
  NSDictionary* metadata=@{@"schema":@(WindConsumed::SchemaValue),@"shader_hash":@"e4abfca709b00ef3",
      @"sites":@[@{@"kind":@"wind",@"original_pc":@0x001c,@"byte_offset":@128,@"words":@4,@"first_sgpr":@12},
          @{@"kind":@"view_projection",@"original_pc":@0x14ec,@"byte_offset":@800,@"words":@16,@"first_sgpr":@8}],
      @"sample_mask":@(WindConsumed::SampleMask),@"frame":@(frame_count),@"draw":@(draw_count),
      @"target_width":@(width),@"target_height":@(height),@"color_targets":@(color_targets),
      @"consumer_submission":@(submission_id),
      @"consumer_command":@(reinterpret_cast<std::uintptr_t>((__bridge const void*)cmd)),
      @"consumer_encoder":@(reinterpret_cast<std::uintptr_t>((__bridge const void*)enc)),
      @"consumer_cpu_seconds":@(StutterTrace::Now()),@"actual_map_generation":@(draw_map_generation),
      @"vs_key":@(vs_key),@"pipeline_key":@(pipeline_key),@"prepared_user_data":[user copy],
      @"reported_count":@(draw.count),@"reported_instances":@(draw.instances),@"indexed":@(draw.indexed),
      @"window_first_frame":@(wind_consumed_window.first),@"window_frames":@(wind_consumed_window.frames),
      @"record_slot":@(wind_consumed_window.total-1),@"record_bytes":@(sizeof(WindConsumed::Record)),
      @"provenance":@"Already loaded original s12..15 at PC001c and s8..23 at PC14ec; no diagnostic guest reread",
      @"limitations":@"Sparse actual VS executions only; atomics perturb timing; raw bit ranges are not float ordering or assumed time units; equality cannot prove all vertices or other inputs"};
  // Completion owns record+immutable metadata. No Impl/this or guest pointer
  // survives in the handler. A preceding command/copy cannot complete this.
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    @autoreleasepool {
      NSMutableDictionary* result=[metadata mutableCopy];result[@"gpu_status"]=@(completed.status);
      bool saved=false;
      if(completed.status==MTLCommandBufferStatusCompleted) {
        WindConsumed::Record words;std::memcpy(words.data(),record.contents,sizeof(words));
        std::ofstream output(filename+".bin",std::ios::binary);
        output.write(reinterpret_cast<const char*>(words.data()),sizeof(words));output.close();saved=output.good();
        NSMutableArray* raw=[NSMutableArray new];for(auto word:words)[raw addObject:@(word)];result[@"record_words"]=[raw copy];
        result[@"wind_samples"]=@(words[WindConsumed::WindSamples]);result[@"view_samples"]=@(words[WindConsumed::ViewSamples]);
        result[@"sampled_w140_varied"]=@(WindConsumed::Varied(words,WindConsumed::Site::Wind,3));
        result[@"sampled_wind_leaf_uniform"]=@(WindConsumed::SelectedLeafUniform(words,WindConsumed::Site::Wind));
        result[@"sampled_view_leaf_uniform"]=@(WindConsumed::SelectedLeafUniform(words,WindConsumed::Site::View));
        result[@"sampled_wind_null_reads"]=@(words[WindConsumed::WindNull]);result[@"sampled_view_null_reads"]=@(words[WindConsumed::ViewNull]);
        result[@"counter_overflow"]=@(words[WindConsumed::CounterOverflow]);
      }
      result[@"complete"]=@(saved);
      if(completed.error)result[@"gpu_error"]=completed.error.localizedDescription;
      NSData* json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
      [json writeToFile:@((filename+".json").c_str()) atomically:YES];
    }
  }];
  return true;
}
