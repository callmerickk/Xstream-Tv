package com.streamx.xe

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.SurfaceView
import android.view.View
import android.widget.FrameLayout
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.Format
import androidx.media3.common.text.Cue
import androidx.media3.common.text.CueGroup
import androidx.media3.datasource.DefaultHttpDataSource
import androidx.media3.datasource.okhttp.OkHttpDataSource
import androidx.media3.exoplayer.DefaultLoadControl
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector
import androidx.media3.ui.CaptionStyleCompat
import androidx.media3.ui.PlayerView
import okhttp3.OkHttpClient
import java.util.concurrent.TimeUnit

/** Android playback host. The GL view must be an overlay above this SurfaceView. */
class XePlayer(
    private val context: Context,
    private val root: FrameLayout,
    private val glView: View
) {
    private val main = Handler(Looper.getMainLooper())
    private var player: ExoPlayer? = null
    private var surface: SurfaceView? = null
    private var started = false
    private var latestPos = 0.0
    private var latestDur = 0.0
    private var latestBuffering = 0
    private var subtitleText = ""
    private var tracks = Array(3) { emptyList<String>() }
    private var trackGroups: androidx.media3.common.Tracks? = null
    private var nativeHandle: Long = 0

    private val ticker = object : Runnable {
        override fun run() {
            val p = player
            if (p != null) {
                latestPos = p.currentPosition.coerceAtLeast(0L) / 1000.0
                latestDur = if (p.duration == C.TIME_UNSET) 0.0 else p.duration.coerceAtLeast(0L) / 1000.0
                latestBuffering = if (p.playbackState == Player.STATE_BUFFERING) 1 else 0
                collectTracks(p)
                nativePushSnapshot(nativeHandle, latestPos, latestDur, latestBuffering, subtitleText)
            }
            main.postDelayed(this, 250L)
        }
    }

    fun attachNative(handle: Long) { nativeHandle = handle }

    fun open(url: String, headerPairs: Array<String>, startSec: Double) {
        check(Looper.myLooper() == Looper.getMainLooper()) { "XePlayer.open must run on main thread" }
        closeInternal(false)
        val headers = linkedMapOf<String, String>()
        var i = 0
        while (i + 1 < headerPairs.size) { headers[headerPairs[i]] = headerPairs[i + 1]; i += 2 }
        val client = OkHttpClient.Builder()
            .connectTimeout(20, TimeUnit.SECONDS).readTimeout(30, TimeUnit.SECONDS)
            .followRedirects(true).build()
        val httpFactory = OkHttpDataSource.Factory(client).setUserAgent("StreamX/XE")
            .setDefaultRequestProperties(headers)
        val loadControl = DefaultLoadControl.Builder()
            .setBufferDurationsMs(30_000, 60_000, 1_500, 5_000)
            .setTargetBufferBytes(DefaultLoadControl.DEFAULT_TARGET_BUFFER_BYTES)
            .setPrioritizeTimeOverSizeThresholds(true).build()
        val selector = DefaultTrackSelector(context)
        val exo = ExoPlayer.Builder(context)
            .setMediaSourceFactory(DefaultMediaSourceFactory(context).setDataSourceFactory(httpFactory))
            .setLoadControl(loadControl).setTrackSelector(selector).build()
        val sv = SurfaceView(context)
        sv.layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        // SurfaceView stays behind GL. Add it first, then make GL a media overlay.
        root.addView(sv, 0)
        if (glView is android.opengl.GLSurfaceView) {
            glView.setZOrderMediaOverlay(true)
            glView.setBackgroundColor(android.graphics.Color.TRANSPARENT)
        }
        val view = PlayerView(context).apply {
            player = exo
            useController = false
            // PlayerView isn't added: ExoPlayer renders directly to SurfaceView below GL.
        }
        exo.setVideoSurfaceView(sv)
        exo.addListener(object : Player.Listener {
            override fun onTracksChanged(tracksNow: androidx.media3.common.Tracks) { trackGroups = tracksNow; collectTracks(exo) }
            override fun onCues(cueGroup: CueGroup) {
                subtitleText = cueGroup.cues.joinToString("\n") { it.text?.toString().orEmpty() }.trim()
                nativePushSnapshot(nativeHandle, latestPos, latestDur, latestBuffering, subtitleText)
            }
        })
        surface = sv; player = exo
        exo.setMediaItem(MediaItem.fromUri(url))
        exo.prepare()
        if (startSec > 0.0) exo.seekTo((startSec * 1000.0).toLong())
        exo.playWhenReady = true
        started = true
        main.removeCallbacks(ticker); main.post(ticker)
    }

    private fun collectTracks(p: ExoPlayer) {
        val audio = mutableListOf<String>(); val subs = mutableListOf<String>(); val video = mutableListOf<String>()
        for (group in p.currentTracks.groups) {
            val type = group.type
            for (i in 0 until group.length) {
                val f: Format = group.getTrackFormat(i)
                val label = f.label ?: listOfNotNull(f.language, f.sampleMimeType, f.height.takeIf { it > 0 }?.let { "${it}p" }).joinToString(" · ").ifBlank { "Track ${i + 1}" }
                val selected = if (group.isTrackSelected(i)) " ✓" else ""
                when (type) { C.TRACK_TYPE_AUDIO -> audio += label + selected; C.TRACK_TYPE_TEXT -> subs += label + selected; C.TRACK_TYPE_VIDEO -> video += label + selected }
            }
        }
        tracks = arrayOf(audio, subs, video)
    }

    fun toggle() { player?.let { if (it.isPlaying) it.pause() else it.play() } }
    fun seek(seconds: Double) { val p = player ?: return; p.seekTo((p.currentPosition + seconds * 1000.0).toLong().coerceAtLeast(0L)) }
    fun selectTrack(kind: Int, index: Int) {
        val p = player ?: return
        val type = when (kind) { 0 -> C.TRACK_TYPE_AUDIO; 1 -> C.TRACK_TYPE_TEXT; else -> C.TRACK_TYPE_VIDEO }
        val groups = p.currentTracks.groups.filter { it.type == type }
        val flat = groups.flatMap { g -> (0 until g.length).map { g to it } }
        val choice = flat.getOrNull(index) ?: return
        val overrides = androidx.media3.common.TrackSelectionOverride(choice.first.mediaTrackGroup, listOf(choice.second))
        p.trackSelectionParameters = p.trackSelectionParameters.buildUpon().clearOverridesOfType(type).addOverride(overrides).build()
    }
    fun trackNames(kind: Int): List<String> = tracks.getOrElse(kind) { emptyList() }
    fun position() = latestPos
    fun duration() = latestDur
    fun buffering() = latestBuffering

    private fun closeInternal(stopTicker: Boolean) {
        if (stopTicker) main.removeCallbacks(ticker)
        player?.release(); player = null
        surface?.let { root.removeView(it) }; surface = null
        started = false; subtitleText = ""; tracks = Array(3) { emptyList() }
        nativePushSnapshot(nativeHandle, 0.0, 0.0, 0, "")
    }
    fun close() { check(Looper.myLooper() == Looper.getMainLooper()); closeInternal(true) }
    private external fun nativePushSnapshot(handle: Long, pos: Double, dur: Double, buffering: Int, subtitle: String)
    private external fun nativePushTracks(handle: Long, kind: Int, names: Array<String>)
    companion object { init { System.loadLibrary("xe") } }
}
