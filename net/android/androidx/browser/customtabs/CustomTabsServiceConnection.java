package androidx.browser.customtabs;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.os.IBinder;
public abstract class CustomTabsServiceConnection implements ServiceConnection {
    public abstract void onCustomTabsServiceConnected(ComponentName name, CustomTabsClient client);
    @Override public void onServiceConnected(ComponentName name, IBinder service) {}
}
