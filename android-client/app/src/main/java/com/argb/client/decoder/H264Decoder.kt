package com.argb.client.decoder

import android.media.MediaCodec
import android.media.MediaFormat
import android.view.Surface
import com.argb.client.protocol.Protocol
import java.nio.ByteBuffer

class H264Decoder(private val surface: Surface) {
    private var codec: MediaCodec? = null
    private var fps = 30
    private var frameIndex = 0L

    fun configure(config: Protocol.VideoConfig) {
        release()
        fps = if (config.fps > 0) config.fps else 30

        val format = MediaFormat.createVideoFormat(
            MediaFormat.MIMETYPE_VIDEO_AVC,
            config.width,
            config.height
        )
        if (config.sps.isNotEmpty()) {
            format.setByteBuffer("csd-0", ByteBuffer.wrap(config.sps))
        }
        if (config.pps.isNotEmpty()) {
            format.setByteBuffer("csd-1", ByteBuffer.wrap(config.pps))
        }

        val mediaCodec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        mediaCodec.configure(format, surface, null, 0)
        mediaCodec.start()
        codec = mediaCodec
        frameIndex = 0
    }

    fun queue(packet: ByteArray) {
        val mediaCodec = codec ?: return

        val inputIndex = mediaCodec.dequeueInputBuffer(10_000)
        if (inputIndex >= 0) {
            val inputBuffer = mediaCodec.getInputBuffer(inputIndex)
            if (inputBuffer != null) {
                inputBuffer.clear()
                inputBuffer.put(packet)
                val presentationTimeUs = frameIndex * 1_000_000L / fps
                mediaCodec.queueInputBuffer(inputIndex, 0, packet.size, presentationTimeUs, 0)
                frameIndex++
            }
        }

        drain(mediaCodec)
    }

    private fun drain(mediaCodec: MediaCodec) {
        val info = MediaCodec.BufferInfo()
        while (true) {
            val outputIndex = mediaCodec.dequeueOutputBuffer(info, 0)
            when {
                outputIndex >= 0 -> mediaCodec.releaseOutputBuffer(outputIndex, true)
                outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> return
                outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> Unit
                else -> return
            }
        }
    }

    fun release() {
        codec?.let {
            try { it.stop() } catch (_: Exception) { }
            try { it.release() } catch (_: Exception) { }
        }
        codec = null
    }
}
