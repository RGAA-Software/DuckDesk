package yun.pixels.client.core.network

import java.security.KeyFactory
import java.security.Provider
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import org.conscrypt.Conscrypt

internal fun interface Ed25519SignatureVerifier {
    fun verify(publicKey: ByteArray, message: ByteArray, signature: ByteArray): Boolean
}

internal class JcaEd25519Verifier(private val provider: Provider? = null) : Ed25519SignatureVerifier {
    override fun verify(publicKey: ByteArray, message: ByteArray, signature: ByteArray): Boolean = runCatching {
        val keyFactory = provider?.let { KeyFactory.getInstance(ED25519, it) } ?: KeyFactory.getInstance(ED25519)
        val verifier = provider?.let { Signature.getInstance(ED25519, it) } ?: Signature.getInstance(ED25519)
        val encodedPublicKey = ED25519_X509_PREFIX + publicKey
        verifier.initVerify(keyFactory.generatePublic(X509EncodedKeySpec(encodedPublicKey)))
        verifier.update(message)
        verifier.verify(signature)
    }.getOrDefault(false)

    companion object {
        fun android(): JcaEd25519Verifier = JcaEd25519Verifier(Conscrypt.newProvider())
    }
}

private const val ED25519 = "Ed25519"
private val ED25519_X509_PREFIX = byteArrayOf(0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00)
