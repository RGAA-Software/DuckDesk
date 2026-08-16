package com.pixels.yun.client.util

import android.util.Log
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.security.KeyStore
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManager
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager


class HttpUtil {

    companion object {
        const val TAG = "http"

        fun reqUrl(url: String): String? {

//            val trustManagerFactory =
//                TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
//            trustManagerFactory.init(null as KeyStore?)
//            val trustManagers = trustManagerFactory.trustManagers
//            check(!(trustManagers.size != 1 || trustManagers[0] !is X509TrustManager)) { "Unexpected default trust managers:" + trustManagers.contentToString() }
//            val trustManager = trustManagers[0] as X509TrustManager
//            val sslContext = SSLContext.getInstance("SSL")
//            sslContext.init(null, arrayOf<TrustManager>(trustManager), null)
//            val sslSocketFactory: SSLSocketFactory = sslContext.socketFactory

            var x509TrustManager = SSLSocketClient.getX509TrustManager();
            var trustManager = SSLSocketClient.getTrustManager(x509TrustManager);
            val client = OkHttpClient.Builder()
                .sslSocketFactory(SSLSocketClient.getSSLSocketFactory(trustManager), x509TrustManager)
                .hostnameVerifier(SSLSocketClient.getHostnameVerifier())
                .connectTimeout(3, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS).build()
            try {
                val request = Request.Builder()
                    .url(url)
                    .build();
                val resp = client.newCall(request).execute();
                if (resp.code != 200) {
                    return null;
                }
                val value = resp.body?.string();
                return value
            } catch (e: Exception) {
                Log.i(TAG, "reqUrl failed: ${e.message}")
                return null;
            }
        }

        fun postUrl(url: String, args: Map<String, String>): String? {
            val x509TrustManager = SSLSocketClient.getX509TrustManager();
            val trustManager = SSLSocketClient.getTrustManager(x509TrustManager);
            val client = OkHttpClient.Builder()
                .sslSocketFactory(SSLSocketClient.getSSLSocketFactory(trustManager), x509TrustManager)
                .hostnameVerifier(SSLSocketClient.getHostnameVerifier())
                .connectTimeout(3, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS).build()
            val mediaType = "application/json;charset=utf-8".toMediaTypeOrNull()!!
            val jsonBody = JSONObject()
            args.forEach { (k, v) ->
                jsonBody.put(k, v)
            }
            val requestBody: RequestBody = jsonBody.toString().toRequestBody(mediaType)
            val request = Request.Builder()
                .url(url)
                .post(requestBody)
                .build()

            try {
                val resp = client.newCall(request).execute()
                if (resp.code != 200) {
                    return null;
                }
                val value = resp.body?.string();
                return value
            } catch (e: Exception) {
                Log.i(TAG, "Failed to execute request: ${e.message}")
                return ""
            }
//            client.newCall(request).enqueue(object : okhttp3.Callback {
//                override fun onResponse(call: okhttp3.Call, response: okhttp3.Response) {
//                    if (response.isSuccessful) {
//                        val responseString = response.body?.string()
//                        Log.i(TAG, "Response is: $responseString")
//                    } else {
//                        Log.i(TAG, "Failed to fetch data: ${response.code}")
//                    }
//                    response.close() // Important to close the response body!
//                }
//
//                override fun onFailure(call: okhttp3.Call, e: IOException) {
//                    Log.i(TAG, "Failed to execute request: ${e.message}")
//                }
//            })
        }
    }
}