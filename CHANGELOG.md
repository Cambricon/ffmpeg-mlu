# Cambricon® ffmpeg-mlu open source release v1.0.3!
**Whats new**
 - Added more errors handling
 
**Known limitations**:

 - If try to transcode a stream within only "one process" or one thread, need define instance_id to "1" for codec limitation.
 - Prefer to use multi-thread design model, not  one tread with more context swtich.