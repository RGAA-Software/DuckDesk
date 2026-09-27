package yun.pixels.client.core.network

import java.net.URI
import java.security.cert.X509Certificate
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.X509TrustManager

/** Keep HTTPS transport while accepting private-deployment certificates without client-side validation. */
internal fun openClientHttpsConnection(url: String): HttpsURLConnection {
    val connection = URI(url).toURL().openConnection() as HttpsURLConnection
    val trustManager = object : X509TrustManager {
        override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) = Unit
        override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) = Unit
        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
    }
    val tlsContext = SSLContext.getInstance("TLS")
    tlsContext.init(null, arrayOf(trustManager), null)
    connection.sslSocketFactory = tlsContext.socketFactory
    connection.hostnameVerifier = javax.net.ssl.HostnameVerifier { _, _ -> true }
    return connection
}
