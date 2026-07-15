package com.quizapp;

import android.net.Uri;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebView;

class FileChooserChromeClient extends WebChromeClient {
    private final MainActivity activity;

    FileChooserChromeClient(MainActivity activity) {
        this.activity = activity;
    }

    @Override
    public boolean onShowFileChooser(WebView webView, ValueCallback<Uri[]> filePathCallback, FileChooserParams fileChooserParams) {
        activity.openFileChooser(filePathCallback, fileChooserParams);
        return true;
    }
}
