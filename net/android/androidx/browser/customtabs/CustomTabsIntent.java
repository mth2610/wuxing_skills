package androidx.browser.customtabs;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
public class CustomTabsIntent {
    public final Intent intent = new Intent();
    public void launchUrl(Context ctx, Uri url) {}
    public static final class Builder {
        public Builder() {}
        public Builder(CustomTabsSession session) {}
        public Builder setDefaultColorSchemeParams(CustomTabColorSchemeParams p) { return this; }
        public Builder setShowTitle(boolean showTitle) { return this; }
        public CustomTabsIntent build() { return new CustomTabsIntent(); }
    }
}
