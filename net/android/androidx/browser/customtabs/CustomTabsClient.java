package androidx.browser.customtabs;
import android.content.Context;
public class CustomTabsClient {
    public boolean warmup(long flags) { return false; }
    public CustomTabsSession newSession(CustomTabsCallback cb) { return null; }
    public static boolean bindCustomTabsService(Context ctx, String pkg, CustomTabsServiceConnection conn) { return false; }
}
