# Cambricon® ffmpeg-mlu open source release v1.1.2!
**Whats new**
 - Supported encoder rate control mode(vbr/cbr/cqp).
 - Supported setting encoder vui sar attribute.
 - Modified encoder&decoder log level.
 - Modified getting encoder header info in init function.
 - Fixed decoder timing logic bug with decoder emptying frame queue.
 - Supports for ``cntookit-mlu270-1.5.0 or later``.

# Cambricon® ffmpeg-mlu open source release v1.1.1!
**Whats new**
 - Fix decoder eos event doesn't come back, sometimes.
 - Fix encode warnig: deinitEncoder wait app release output buffer timeout.
 - Supported decoder&encoder open-close test.
 - Supported ffmpeg-mlu version info print.
 - Supported mlu decoder hardware scale postproc.
 - Supported mlu encoder hardware scale preproc.
 - Supports for ``neuware-mlu270-1.4.0 or later``.

# Cambricon® ffmpeg-mlu open source release v1.1.0!
**Whats new**
 - Supported mlu h264/hevc/vp8/vp9/mjpeg decoder.
 - Supported mlu h264/hevc/mjpeg encoder.
 - Supported mlu hardware transcode.
 - Supported mlu decoder hardware scale postproc.
 - Supports for ``neuware-mlu270-1.4.0 or later``.

# Cambricon® ffmpeg-mlu open source release v1.0.9!
**Whats new**
 - Added mlu mjpeg encoder.
 - Added p010 pix fmt.
 - Supported 8bit to 10bit transcode.
 - Remove supports for ``neuware-mlu270-1.3.0``

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