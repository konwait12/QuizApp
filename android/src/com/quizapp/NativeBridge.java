package com.quizapp;

import android.webkit.JavascriptInterface;

class NativeBridge {
    private final MainActivity activity;

    NativeBridge(MainActivity activity) {
        this.activity = activity;
    }

    @JavascriptInterface
    public void downloadAndInstallApk(String url) {
        activity.runOnUiThread(new DownloadRunnable(activity, url));
    }

    @JavascriptInterface
    public void openBankDirectory() {
        activity.runOnUiThread(new OpenBankDirectoryRunnable(activity));
    }
}
