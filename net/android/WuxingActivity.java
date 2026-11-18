package com.mth2610.wuxing;

import android.app.NativeActivity;
import android.os.Bundle;
import com.epicgames.mobile.eossdk.EOSSDK;

public class WuxingActivity extends NativeActivity {
    static {
        System.loadLibrary("EOSSDK");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        EOSSDK.init(this);
        super.onCreate(savedInstanceState);
    }
}
