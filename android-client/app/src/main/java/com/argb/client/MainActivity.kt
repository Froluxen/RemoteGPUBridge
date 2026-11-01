package com.argb.client

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import com.argb.client.network.StreamingClient
import com.argb.client.protocol.Protocol

class MainActivity : Activity(), SurfaceHolder.Callback {
    private var client: StreamingClient? = null
    private lateinit var statsView: TextView
    private lateinit var surfaceView: SurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val root = FrameLayout(this)

        surfaceView = SurfaceView(this)
        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        statsView = TextView(this).apply {
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 12f
            setBackgroundColor(0x80000000.toInt())
            setPadding(24, 24, 24, 24)
            text = "Conectando..."
        }
        root.addView(
            statsView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP or Gravity.START
            )
        )

        setContentView(root)
        surfaceView.holder.addCallback(this)
        surfaceView.setOnTouchListener { _, event ->
            handleTouch(event)
            true
        }
    }

    private fun handleTouch(event: MotionEvent) {
        val streamingClient = client ?: return
        val width = surfaceView.width.toFloat().coerceAtLeast(1.0f)
        val height = surfaceView.height.toFloat().coerceAtLeast(1.0f)

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val index = event.actionIndex
                streamingClient.sendTouch(
                    event.getPointerId(index),
                    Protocol.TOUCH_DOWN,
                    event.getX(index) / width,
                    event.getY(index) / height
                )
            }

            MotionEvent.ACTION_MOVE -> {
                for (index in 0 until event.pointerCount) {
                    streamingClient.sendTouch(
                        event.getPointerId(index),
                        Protocol.TOUCH_MOVE,
                        event.getX(index) / width,
                        event.getY(index) / height
                    )
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val index = event.actionIndex
                streamingClient.sendTouch(
                    event.getPointerId(index),
                    Protocol.TOUCH_UP,
                    event.getX(index) / width,
                    event.getY(index) / height
                )
            }

            MotionEvent.ACTION_CANCEL -> {
                for (index in 0 until event.pointerCount) {
                    streamingClient.sendTouch(
                        event.getPointerId(index),
                        Protocol.TOUCH_CANCEL,
                        event.getX(index) / width,
                        event.getY(index) / height
                    )
                }
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val streamingClient = StreamingClient(
            surface = holder.surface,
            onStats = { message -> runOnUiThread { statsView.text = message } }
        )
        client = streamingClient
        streamingClient.start()
        startSyntheticSwipeIfRequested()
    }

    // Modo de depuracion: simula un swipe Android -> PC para validar el puente
    // de entrada sin depender de la inyeccion de eventos del sistema.
    private fun startSyntheticSwipeIfRequested() {
        if (!intent.getBooleanExtra("simulateSwipe", false)) return
        Thread {
            try {
                Thread.sleep(4000)
                val streamingClient = client ?: return@Thread
                val points = listOf(
                    0.35f to 0.50f,
                    0.41f to 0.50f,
                    0.47f to 0.50f,
                    0.53f to 0.50f,
                    0.59f to 0.50f
                )
                val first = points.first()
                streamingClient.sendTouch(0, Protocol.TOUCH_DOWN, first.first, first.second)
                for ((x, y) in points.drop(1)) {
                    Thread.sleep(30)
                    streamingClient.sendTouch(0, Protocol.TOUCH_MOVE, x, y)
                }
                val last = points.last()
                streamingClient.sendTouch(0, Protocol.TOUCH_UP, last.first, last.second)
            } catch (_: InterruptedException) {
            }
        }.start()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        client?.stop()
        client = null
    }

    override fun onDestroy() {
        client?.stop()
        client = null
        super.onDestroy()
    }
}
