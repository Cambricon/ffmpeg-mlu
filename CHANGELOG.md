# Cambricon® ffmpeg-mlu open source release v1.0.4!
**Whats new**
 - Added abort event handling to avoid dead-lock when receive abort event.
 
**Known limitations**:

 - If try to transcode a stream within only "one process" or one thread, need define ``instance_id`` to "1" for ``neuware-mlu270-1.3.0-1``

# Cambricon® ffmpeg-mlu open source release v1.0.3!
**Whats new**
 - Added more errors handling
 
**Known limitations**:

 - If try to transcode a stream within only "one process" or one thread, need define ``instance_id`` to "1" for ``neuware-mlu270-1.3.0-1``