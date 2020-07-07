# Cambricon® ffmpeg-mlu open source release v1.0.8!
**Whats new**
 - Modified timeout mechanism.
 - Adapted to the ffmpeg screenshot mechanism.
 - Modified mlumpp_dec log info.

# Cambricon® ffmpeg-mlu open source release v1.0.7!
**Whats new**
 - Added invalid address protection mechanism.
 - Set instance_id default value is 0, corresponding to VPU instance auto mode.
 - Added auto calulate time out mechanism in MLU encode part.

**Known limitations**:
 - N/A

# Cambricon® ffmpeg-mlu open source release v1.0.6!
**Whats new**
 - Added 10s timeout for feeding data.
 - Remove supports for ``neuware-mlu270-1.2.5-1``
 - Try to support video resolution changes.
 - Other improvements for adding system robust

**Known limitations**:
 - N/A

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