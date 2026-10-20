package com.xe.xehost

import android.content.Context
import android.util.Log
import okhttp3.Cache
import okhttp3.Call
import okhttp3.Callback
import okhttp3.Cookie
import okhttp3.CookieJar
import okhttp3.HttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.Response
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

object XeNet {
    data class Result(val status: Int, val body: ByteArray, val finalUrl: String,
                      val contentType: String, val error: String?, val ms: Long)
    private val calls = ConcurrentHashMap<Int, Call>()
    private val cookieMap = ConcurrentHashMap<String, List<Cookie>>()
    @Volatile private var ua = "XE/1.0"
    @Volatile private var appContext: Context? = null
    private val client: OkHttpClient by lazy {
        val ctx = appContext
        val builder = OkHttpClient.Builder()
            .connectTimeout(20, TimeUnit.SECONDS).readTimeout(20, TimeUnit.SECONDS)
            .writeTimeout(20, TimeUnit.SECONDS).followRedirects(true).followSslRedirects(true)
            .connectionPool(okhttp3.ConnectionPool(8, 5, TimeUnit.MINUTES))
            .cookieJar(object : CookieJar {
                override fun saveFromResponse(url: HttpUrl, cookies: List<Cookie>) { cookieMap[url.host] = cookies }
                override fun loadForRequest(url: HttpUrl): List<Cookie> = cookieMap[url.host]?.filter { it.matches(url) } ?: emptyList()
            })
            .addInterceptor { chain -> chain.proceed(chain.request().newBuilder().header("User-Agent", ua).build()) }
        if (ctx != null) runCatching {
            builder.cache(Cache(File(ctx.cacheDir, "xe-http-cache"), 50L * 1024L * 1024L))
        }.onFailure { Log.w("xe", "HTTP cache unavailable", it) }
        builder.build()
    }
    @JvmStatic fun configure(context: Context) { if (appContext == null) appContext = context.applicationContext }
    @JvmStatic fun setUserAgent(value: String) { ua = value.ifBlank { "XE/1.0" } }
    private fun headerMap(lines: Array<String>): Map<String,String> {
        val result = LinkedHashMap<String,String>()
        for (line in lines) { val i=line.indexOf(':'); if (i>0) result[line.substring(0,i).trim()] = line.substring(i+1).trim() }
        return result
    }
    @JvmStatic fun requestAsync(url: String, headers: Array<String>, method: String, body: ByteArray?, id: Int) {
        val start = System.nanoTime()
        try {
            val b = Request.Builder().url(url)
            headerMap(headers).forEach { (k,v) -> b.header(k,v) }
            val rb = if (method == "POST") (body ?: ByteArray(0)).toRequestBody("application/octet-stream".toMediaType()) else null
            b.method(method, rb)
            val call = client.newCall(b.build()); calls[id] = call
            call.enqueue(object : Callback {
                override fun onFailure(call: Call, e: IOException) {
                    calls.remove(id, call)
                    nativeComplete(id,0,ByteArray(0),url,"",e.message ?: "network request failed",(System.nanoTime()-start)/1_000_000L)
                }
                override fun onResponse(call: Call, response: Response) {
                    response.use { res ->
                        calls.remove(id, call)
                        try {
                            val stream = res.body?.byteStream()
                            val out = ByteArrayOutputStream()
                            val buf = ByteArray(16*1024); var total=0
                            if (stream != null) while (true) {
                                val n=stream.read(buf); if(n<0) break
                                total += n
                                if(total > 32*1024*1024) throw IOException("response body exceeds 32 MiB safety limit")
                                out.write(buf,0,n)
                            }
                            nativeComplete(id,res.code,out.toByteArray(),res.request.url.toString(),
                                res.header("Content-Type") ?: "","",(System.nanoTime()-start)/1_000_000L)
                        } catch (e: Exception) {
                            nativeComplete(id,res.code,ByteArray(0),res.request.url.toString(),
                                res.header("Content-Type") ?: "",e.message ?: "failed reading response",(System.nanoTime()-start)/1_000_000L)
                        }
                    }
                }
            })
        } catch (e: Exception) {
            nativeComplete(id,0,ByteArray(0),url,"",e.message ?: "invalid request",(System.nanoTime()-start)/1_000_000L)
        }
    }
    @JvmStatic fun cancel(id: Int) { calls.remove(id)?.cancel() }
    @JvmStatic fun requestBlocking(url: String, headers: Array<String>): Result {
        val start=System.nanoTime()
        return try {
            val b=Request.Builder().url(url); headerMap(headers).forEach { (k,v)->b.header(k,v) }
            client.newCall(b.get().build()).execute().use { r ->
                val bytes=r.body?.bytes() ?: ByteArray(0)
                if(bytes.size>32*1024*1024) Result(r.code,ByteArray(0),r.request.url.toString(),r.header("Content-Type") ?: "","response body exceeds 32 MiB safety limit",(System.nanoTime()-start)/1_000_000L)
                else Result(r.code,bytes,r.request.url.toString(),r.header("Content-Type") ?: "",null,(System.nanoTime()-start)/1_000_000L)
            }
        } catch(e:Exception) { Result(0,ByteArray(0),url,"",e.message ?: "network request failed",(System.nanoTime()-start)/1_000_000L) }
    }
    @JvmStatic private external fun nativeComplete(id:Int,status:Int,body:ByteArray,finalUrl:String,contentType:String,error:String,ms:Long)
}
