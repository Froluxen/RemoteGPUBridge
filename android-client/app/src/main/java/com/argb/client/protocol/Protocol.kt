package com.argb.client.protocol

import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

object Protocol {
    const val MAGIC = 0x41524742
    const val VERSION = 1
    const val HEADER_SIZE = 16
    const val MAX_PAYLOAD = 16 * 1024 * 1024

    const val TYPE_HELLO = 1
    const val TYPE_CONFIG = 2
    const val TYPE_VIDEO = 3
    const val TYPE_INPUT = 4
    const val TYPE_STATS = 5
    const val TYPE_PING = 6
    const val TYPE_ERROR = 7

    const val TOUCH_DOWN = 1
    const val TOUCH_MOVE = 2
    const val TOUCH_UP = 3
    const val TOUCH_CANCEL = 4

    data class Message(val type: Int, val payload: ByteArray, val sequence: Int)

    data class VideoConfig(
        val width: Int,
        val height: Int,
        val fps: Int,
        val bitrate: Int,
        val codec: String,
        val sps: ByteArray,
        val pps: ByteArray
    )

    fun readMessage(input: InputStream): Message {
        val header = ByteArray(HEADER_SIZE)
        readExact(input, header, HEADER_SIZE)

        val buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        val magic = buffer.int
        val version = buffer.short.toInt() and 0xFFFF
        val type = buffer.short.toInt() and 0xFFFF
        val length = buffer.int
        val sequence = buffer.int

        if (magic != MAGIC || version != VERSION || length < 0 || length > MAX_PAYLOAD) {
            throw IOException(
                "Header invalido: magic=0x${magic.toString(16)} version=$version length=$length"
            )
        }

        val payload = ByteArray(length)
        if (length > 0) {
            readExact(input, payload, length)
        }
        return Message(type, payload, sequence)
    }

    fun writeMessage(output: OutputStream, type: Int, payload: ByteArray, sequence: Int) {
        val header = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        header.putInt(MAGIC)
        header.putShort(VERSION.toShort())
        header.putShort(type.toShort())
        header.putInt(payload.size)
        header.putInt(sequence)
        output.write(header.array())
        output.write(payload)
        output.flush()
    }

    fun parseVideoConfig(payload: ByteArray): VideoConfig {
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        val width = buffer.int
        val height = buffer.int
        val fps = buffer.int
        val bitrate = buffer.int

        val codecLength = buffer.int
        if (codecLength < 0 || codecLength > buffer.remaining()) {
            throw IOException("Longitud de codec invalida")
        }
        val codecBytes = ByteArray(codecLength)
        buffer.get(codecBytes)
        val codec = String(codecBytes, Charsets.US_ASCII)

        val spsLength = buffer.int
        if (spsLength < 0 || spsLength > buffer.remaining()) {
            throw IOException("Longitud de SPS invalida")
        }
        val sps = ByteArray(spsLength)
        buffer.get(sps)

        val ppsLength = buffer.int
        if (ppsLength < 0 || ppsLength > buffer.remaining()) {
            throw IOException("Longitud de PPS invalida")
        }
        val pps = ByteArray(ppsLength)
        buffer.get(pps)

        return VideoConfig(width, height, fps, bitrate, codec, sps, pps)
    }

    private fun readExact(input: InputStream, buffer: ByteArray, size: Int) {
        var offset = 0
        while (offset < size) {
            val read = input.read(buffer, offset, size - offset)
            if (read <= 0) {
                throw EOFException()
            }
            offset += read
        }
    }
}
