package androidx.security.crypto;
import android.content.Context;
import android.content.SharedPreferences;
public class EncryptedSharedPreferences {
    public enum PrefKeyEncryptionScheme { AES256_SIV }
    public enum PrefValueEncryptionScheme { AES256_GCM }
    public static SharedPreferences create(String fileName, String masterKeyAlias, Context ctx,
                                           PrefKeyEncryptionScheme k, PrefValueEncryptionScheme v) {
        return ctx.getSharedPreferences(fileName, Context.MODE_PRIVATE);
    }
}
