package com.xe.xehost

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.os.Handler
import android.os.Looper
import android.view.SurfaceView
import android.view.View
import android.widget.FrameLayout
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.Tracks
import androidx.media3.common.text.CueGroup
import androidx.media3.datasource.okhttp.OkHttpDataSource
import androidx.media3.exoplayer.DefaultLoadControl
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector
import okhttp3.OkHttpClient
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit

class XePlayer(private val context: Context, private val root: FrameLayout, private val glView: View) {
    private val main = Handler(Looper.getMainLooper())
    private var player: ExoPlayer? = null
    private var surface: SurfaceView? = null
    private var nativeHandle = 1L
    private var pos = 0.0
    private var dur = 0.0
    private var buffering = 0
    private var paused = 0
    private var error = ""
    private var subtitle = ""
    private var tracks = Array(3) { emptyList<String>() }
    private var trackSelector: DefaultTrackSelector? = null
    private var currentUrl = ""
    private var currentHeaders: Map<String, String> = emptyMap()
    private var previewFuture: Future<*>? = null
    private val previewExecutor = Executors.newSingleThreadExecutor { r -> Thread(r, "XE-Preview").apply { isDaemon = true } }

    private val ticker = object : Runnable {
        override fun run() {
            val p = player
            if (p != null) {
                updateSnapshot(p)
                pushTracks()
                pushSnapshot()
                main.postDelayed(this, 250L)
            }
        }
    }

    fun attachNative(handle: Long) { nativeHandle = handle }

    fun open(url: String, headerPairs: Array<String>, startSec: Double) {
        if (Looper.myLooper() != Looper.getMainLooper()) { main.post { open(url, headerPairs, startSec) }; return }
        closeInternal(false)
        error = ""; buffering = 1; paused = 0; pos = 0.0; dur = 0.0; subtitle = ""
        val headers = linkedMapOf<String, String>()
        var i = 0
        while (i + 1 < headerPairs.size) { headers[headerPairs[i]] = headerPairs[i + 1]; i += 2 }
        currentUrl = url; currentHeaders = headers
        val client = OkHttpClient.Builder().connectTimeout(20, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS).followRedirects(true).build()
        val httpFactory = OkHttpDataSource.Factory(client).setUserAgent("XstreamTV/XE")
            .setDefaultRequestProperties(headers)
        val loadControl = DefaultLoadControl.Builder()
            .setBufferDurationsMs(5_000, 25_000, 1_000, 2_000)
            .setTargetBufferBytes(50 * 1024 * 1024)
            .setPrioritizeTimeOverSizeThresholds(true).build()
        val selector = DefaultTrackSelector(context)
        trackSelector = selector
        val exo = ExoPlayer.Builder(context)
            .setMediaSourceFactory(DefaultMediaSourceFactory(context).setDataSourceFactory(httpFactory))
            .setLoadControl(loadControl).setTrackSelector(selector).build()
        val sv = SurfaceView(context).apply {
            layoutParams = FrameLayout.LayoutParams(-1, -1)
        }
        root.addView(sv, 0)
        surface = sv
        if (glView is android.opengl.GLSurfaceView) {
            glView.setZOrderMediaOverlay(true)
        }
        exo.setVideoSurfaceView(sv)
        exo.addListener(object : Player.Listener {
            override fun onPlaybackStateChanged(state: Int) { updateSnapshot(exo); pushSnapshot() }
            override fun onIsPlayingChanged(isPlaying: Boolean) { paused = if (isPlaying) 0 else 1; pushSnapshot() }
            override fun onPlayerError(playbackException: PlaybackException) {
                error = when (playbackException.errorCode) {
                    PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_FAILED,
                    PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_TIMEOUT -> "Network connection failed. Check your connection and try again."
                    PlaybackException.ERROR_CODE_IO_BAD_HTTP_STATUS -> "The stream server rejected the request."
                    PlaybackException.ERROR_CODE_DECODING_FAILED,
                    PlaybackException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED -> "This video format is not supported."
                    else -> "Unable to play this stream. Try another source."
                }
                buffering = 0; pushSnapshot()
            }
            override fun onTracksChanged(tracksNow: Tracks) { collectTracks(exo); pushTracks() }
            override fun onCues(cueGroup: CueGroup) {
                subtitle = cueGroup.cues.joinToString("\n") { it.text?.toString().orEmpty() }.trim()
                pushSnapshot()
            }
        })
        player = exo
        exo.setMediaItem(MediaItem.fromUri(url))
        exo.prepare()
        if (startSec > 0) exo.seekTo((startSec * 1000.0).toLong())
        exo.playWhenReady = true
        main.removeCallbacks(ticker); main.post(ticker)
        pushSnapshot()
    }

    private fun updateSnapshot(p: ExoPlayer) {
        pos = p.currentPosition.coerceAtLeast(0L) / 1000.0
        dur = if (p.duration == C.TIME_UNSET) 0.0 else p.duration.coerceAtLeast(0L) / 1000.0
        buffering = if (p.playbackState == Player.STATE_BUFFERING) 1 else 0
        paused = if (p.playWhenReady && p.isPlaying) 0 else 1
    }

    private fun collectTracks(p: ExoPlayer) {
        val lists = Array(3) { mutableListOf<String>() }
        for (group in p.currentTracks.groups) {
            val kind = when (group.type) { C.TRACK_TYPE_AUDIO -> 0; C.TRACK_TYPE_TEXT -> 1; C.TRACK_TYPE_VIDEO -> 2; else -> -1 }
            if (kind < 0) continue
            for (i in 0 until group.length) {
                val f: Format = group.getTrackFormat(i)
                val label = f.label ?: listOfNotNull(f.language, f.sampleMimeType,
                    f.height.takeIf { it > 0 }?.let { "${it}p" }).joinToString(" · ").ifBlank { "Track ${lists[kind].size + 1}" }
                lists[kind] += label + if (group.isTrackSelected(i)) "  ✓" else ""
            }
        }
        tracks = Array(3) { lists[it].toList() }
    }

    private fun pushSnapshot() {
        nativePushSnapshot(nativeHandle, pos, dur, buffering, paused, error, subtitle)
    }
    private fun pushTracks() {
        for (k in 0..2) nativePushTracks(nativeHandle, k, tracks[k].toTypedArray())
    }

    fun toggle() { player?.let { if (it.isPlaying) it.pause() else { it.playWhenReady = true; it.play() } } }
    fun seek(seconds: Double) {
        val p = player ?: return
        val target = (p.currentPosition + seconds * 1000.0).toLong().coerceAtLeast(0L)
        p.seekTo(if (p.duration == C.TIME_UNSET) target else target.coerceAtMost(p.duration))
    }
    fun selectTrack(kind: Int, index: Int) {
        val p = player ?: return
        val type = when (kind) { 0 -> C.TRACK_TYPE_AUDIO; 1 -> C.TRACK_TYPE_TEXT; 2 -> C.TRACK_TYPE_VIDEO; else -> return }
        val groups = p.currentTracks.groups.filter { it.type == type }
        val flat = groups.flatMap { g -> (0 until g.length).map { g to it } }
        if (kind == 1 && index == -1) {
            p.trackSelectionParameters = p.trackSelectionParameters.buildUpon()
                .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).clearOverridesOfType(C.TRACK_TYPE_TEXT).build()
            collectTracks(p); pushTracks(); return
        }
        val choice = flat.getOrNull(index) ?: return
        val override = androidx.media3.common.TrackSelectionOverride(choice.first.mediaTrackGroup, listOf(choice.second))
        p.trackSelectionParameters = p.trackSelectionParameters.buildUpon()
            .setTrackTypeDisabled(type, false).clearOverridesOfType(type).addOverride(override).build()
        collectTracks(p); pushTracks()
    }
    fun trackNames(kind: Int): List<String> = tracks.getOrElse(kind) { emptyList() }
    /** Seek preview: one small frame near [seconds], decoded off the main thread with the stream's own headers.
     *  Never touches playback; failures are ignored (the bar simply shows no picture). */
    fun requestPreview(seconds: Double, bucket: Int, gen: Int) {
        val url = currentUrl
        if (url.isBlank() || seconds < 0.0 || !seconds.isFinite()) { nativePreviewFailed(gen); return }
        val headers = HashMap(currentHeaders)
        previewFuture?.cancel(true)
        previewFuture = previewExecutor.submit {
            var mmr: MediaMetadataRetriever? = null
            try {
                mmr = MediaMetadataRetriever()
                mmr.setDataSource(url, headers)
                val src = mmr.getFrameAtTime((seconds * 1_000_000).toLong(), MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
                if (src == null) { nativePreviewFailed(gen); return@submit }
                val scale = minOf(1.0, 320.0 / maxOf(src.width, src.height))
                val w = maxOf(1, (src.width * scale).toInt()); val h = maxOf(1, (src.height * scale).toInt())
                val bmp = if (w != src.width || h != src.height) Bitmap.createScaledBitmap(src, w, h, true) else src
                val px = IntArray(w * h); bmp.getPixels(px, 0, w, 0, 0, w, h)
                val rgba = ByteArray(w * h * 4)
                for (k in px.indices) { val c = px[k]; val o = k * 4
                    rgba[o] = (c shr 16).toByte(); rgba[o + 1] = (c shr 8).toByte(); rgba[o + 2] = c.toByte(); rgba[o + 3] = (c ushr 24).toByte() }
                if (bmp !== src) bmp.recycle(); src.recycle()
                nativePushPreview(bucket, seconds, w, h, rgba, gen)
            } catch (_: Throwable) {
                nativePreviewFailed(gen)
            } finally { runCatching { mmr?.release() } }
        }
    }

    private fun closeInternal(stopTicker: Boolean) {
        previewFuture?.cancel(true); previewFuture = null
        currentUrl = ""; currentHeaders = emptyMap()
        if (stopTicker) main.removeCallbacks(ticker)
        player?.release(); player = null
        surface?.let { root.removeView(it) }; surface = null
        trackSelector = null; tracks = Array(3) { emptyList() }
        pos = 0.0; dur = 0.0; buffering = 0; paused = 0; error = ""; subtitle = ""
        pushSnapshot(); pushTracks()
    }
    fun close() {
        if (Looper.myLooper() != Looper.getMainLooper()) { main.post { close() }; return }
        closeInternal(true)
    }
    private external fun nativePushSnapshot(handle: Long, pos: Double, dur: Double, buffering: Int, paused: Int, error: String, subtitle: String)
    private external fun nativePushTracks(handle: Long, kind: Int, names: Array<String>)
    private external fun nativePushPreview(bucket: Int, shownSec: Double, width: Int, height: Int, rgba: ByteArray, gen: Int)
    private external fun nativePreviewFailed(gen: Int)
    companion object { init { System.loadLibrary("xe_android") } }
}
