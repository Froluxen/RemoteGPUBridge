package com.argb.client.network

import android.view.Surface
import com.argb.client.decoder.H264Decoder
import com.argb.client.protocol.Protocol
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class StreamingClient(
    private val surface: Surface,
    private val onStats: (String) -> Unit,
    private val host: String = "127.0.0.1",
    private val videoPort: Int = 5000,
    private val controlPort: Int = 5001
) {
    private val running = AtomicBoolean(false)
    private val sequence = AtomicInteger(1)
    private val controlWriteLock = Any()
    private var videoThread: Thread? = null
    private var controlThread: Thread? = null

    @Volatile
    private var controlOutput: OutputStream? = null

    @Volatile
    private var videoSocket: Socket? = null

    @Volatile
    private var controlSocket: Socket? = null

    private val decoder = H264Decoder(surface)

    fun start() {
        if (running.getAndSet(true)) return
        videoThread = Thread({ videoLoop() }, "argb-video").also { it.start() }
        controlThread = Thread({ controlLoop() }, "argb-control").also { it.start() }
    }

    fun stop() {
        running.set(false)
        closeQuietly(videoSocket)
        closeQuietly(controlSocket)
        videoThread?.join(1000)
        controlThread?.join(1000)
        decoder.release()
    }

    fun sendTouch(pointerId: Int, action: Int, x: Float, y: Float) {
        val output = controlOutput ?: return
        val payload = ByteBuffer.allocate(22)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(pointerId)
            .putShort(action.toShort())
            .putFloat(x)
            .putFloat(y)
            .putLong(System.currentTimeMillis())
            .array()

        synchronized(controlWriteLock) {
            try {
                Protocol.writeMessage(output, Protocol.TYPE_INPUT, payload, sequence.getAndIncrement())
            } catch (_: Exception) {
            }
        }
    }

    private fun connect(port: Int): Socket {
        val socket = Socket()
        socket.tcpNoDelay = true
        socket.connect(InetSocketAddress(host, port), 3000)
        return socket
    }

    private fun videoLoop() {
        while (running.get()) {
            try {
                val socket = connect(videoPort)
                videoSocket = socket
                val input = BufferedInputStream(socket.getInputStream())
                onStats("Video conectado")
                while (running.get()) {
                    val message = Protocol.readMessage(input)
                    when (message.type) {
                        Protocol.TYPE_CONFIG -> {
                            val config = Protocol.parseVideoConfig(message.payload)
                            decoder.configure(config)
                            onStats("CONFIG ${config.width}x${config.height} @ ${config.fps} fps")
                        }
                        Protocol.TYPE_VIDEO -> decoder.queue(message.payload)
                    }
                }
            } catch (exception: Exception) {
                if (running.get()) {
                    onStats("Video: ${exception.message}")
                }
            } finally {
                closeQuietly(videoSocket)
                videoSocket = null
                decoder.release()
            }
            if (running.get()) Thread.sleep(500)
        }
    }

    private fun controlLoop() {
        while (running.get()) {
            var pinger: Thread? = null
            try {
                val socket = connect(controlPort)
                controlSocket = socket
                val input = BufferedInputStream(socket.getInputStream())
                val output = BufferedOutputStream(socket.getOutputStream())
                controlOutput = output

                synchronized(controlWriteLock) {
                    Protocol.writeMessage(
                        output,
                        Protocol.TYPE_HELLO,
                        ByteArray(0),
                        sequence.getAndIncrement()
                    )
                }
                onStats("Control conectado")

                pinger = Thread({
                    try {
                        while (running.get() && !socket.isClosed) {
                            synchronized(controlWriteLock) {
                                val ping = ByteBuffer.allocate(8)
                                    .order(ByteOrder.LITTLE_ENDIAN)
                                    .putLong(System.currentTimeMillis())
                                    .array()
                                Protocol.writeMessage(
                                    output,
                                    Protocol.TYPE_PING,
                                    ping,
                                    sequence.getAndIncrement()
                                )
                            }
                            Thread.sleep(1000)
                        }
                    } catch (_: Exception) {
                    }
                }, "argb-ping")
                pinger.start()

                while (running.get()) {
                    val message = Protocol.readMessage(input)
                    when (message.type) {
                        Protocol.TYPE_PING -> {
                            if (message.payload.size >= 8) {
                                val sent = ByteBuffer.wrap(message.payload)
                                    .order(ByteOrder.LITTLE_ENDIAN)
                                    .long
                                val rtt = System.currentTimeMillis() - sent
                                onStats("RTT $rtt ms")
                            }
                        }
                        Protocol.TYPE_STATS -> Unit
                        Protocol.TYPE_HELLO -> onStats("HELLO recibido")
                    }
                }
            } catch (exception: Exception) {
                if (running.get()) {
                    onStats("Control: ${exception.message}")
                }
            } finally {
                controlOutput = null
                closeQuietly(controlSocket)
                controlSocket = null
                pinger?.join(1000)
            }
            if (running.get()) Thread.sleep(500)
        }
    }

    private fun closeQuietly(socket: Socket?) {
        try {
            socket?.close()
        } catch (_: Exception) {
        }
    }
}
