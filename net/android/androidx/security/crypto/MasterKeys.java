package androidx.security.crypto;
import android.security.keystore.KeyGenParameterSpec;
public class MasterKeys {
    public static final KeyGenParameterSpec AES256_GCM_SPEC = null;
    public static String getOrCreate(KeyGenParameterSpec spec) { return "_wuxing_eos_master_"; }
}
