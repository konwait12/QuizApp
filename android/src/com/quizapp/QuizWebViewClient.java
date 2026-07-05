package com.quizapp;

import android.net.Uri;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;

class QuizWebViewClient extends WebViewClient {
    private final MainActivity activity;

    QuizWebViewClient(MainActivity activity) {
        this.activity = activity;
    }

    @Override
    public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
        Uri uri = request.getUrl();
        if ("file".equals(uri.getScheme())) {
            return false;
        }
        activity.openExternal(uri.toString());
        return true;
    }

    @Override
    public void onPageFinished(WebView view, String url) {
        view.evaluateJavascript("window.applyNativeTheme && window.applyNativeTheme(" + activity.isDarkMode() + ")", null);
    }
}
