package org.quizapp.platform;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class SecureSecretBridge {
    private static final String STORE_NAME = "quizapp_secure_secrets";
    private static final String KEY_ALIAS = "QuizAppSecureSecretsV1";
    private static final String TRANSFORMATION = "AES/GCM/NoPadding";

    private SecureSecretBridge() {}

    public static synchronized boolean writeSecret(String name, String value) {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || name == null || name.isEmpty() || value == null) return false;
        try {
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey());
            cipher.updateAAD(name.getBytes(StandardCharsets.UTF_8));
            byte[] encrypted = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            String payload = Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP)
                    + ":" + Base64.encodeToString(encrypted, Base64.NO_WRAP);
            preferences(activity).edit().putString(name, payload).apply();
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    public static synchronized String readSecret(String name) {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || name == null || name.isEmpty()) return null;
        String payload = preferences(activity).getString(name, "");
        if (payload == null || payload.isEmpty()) return "";
        int separator = payload.indexOf(':');
        if (separator <= 0 || separator >= payload.length() - 1) return null;
        try {
            byte[] iv = Base64.decode(payload.substring(0, separator), Base64.NO_WRAP);
            byte[] encrypted = Base64.decode(payload.substring(separator + 1), Base64.NO_WRAP);
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), new GCMParameterSpec(128, iv));
            cipher.updateAAD(name.getBytes(StandardCharsets.UTF_8));
            return new String(cipher.doFinal(encrypted), StandardCharsets.UTF_8);
        } catch (Exception ignored) {
            return null;
        }
    }

    public static synchronized boolean removeSecret(String name) {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || name == null || name.isEmpty()) return false;
        preferences(activity).edit().remove(name).apply();
        return true;
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(STORE_NAME, Context.MODE_PRIVATE);
    }

    private static SecretKey getOrCreateKey() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(KEY_ALIAS)) {
            return ((KeyStore.SecretKeyEntry) store.getEntry(KEY_ALIAS, null)).getSecretKey();
        }
        KeyGenerator generator = KeyGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build());
        return generator.generateKey();
    }
}
