package com.argb.client

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import com.argb.client.network.StreamingClient

class MainActivity : Activity(), SurfaceHolder.Callback {
    private var client: StreamingClient? = null
    private lateinit var statsView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val root = FrameLayout(this)

        val surfaceView = SurfaceView(this)
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
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val streamingClient = StreamingClient(
            surface = holder.surface,
            onStats = { message -> runOnUiThread { statsView.text = message } }
        )
        client = streamingClient
        streamingClient.start()
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
