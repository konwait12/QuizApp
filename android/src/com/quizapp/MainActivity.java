package com.quizapp;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.webkit.WebSettings;
import android.webkit.ValueCallback;
import android.webkit.WebView;

public class MainActivity extends Activity {
    static final int FILE_CHOOSER_REQUEST = 1001;
    private WebView webView;
    private ValueCallback<Uri[]> fileCallback;

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        webView = new WebView(this);
        webView.setWebViewClient(new QuizWebViewClient(this));
        webView.setWebChromeClient(new FileChooserChromeClient(this));
        webView.setOverScrollMode(View.OVER_SCROLL_NEVER);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        settings.setAllowFileAccessFromFileURLs(true);
        settings.setAllowUniversalAccessFromFileURLs(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            settings.setForceDark(WebSettings.FORCE_DARK_AUTO);
        }

        setContentView(webView);
        webView.loadUrl("file:///android_asset/index.html");
    }

    void openFileChooser(ValueCallback<Uri[]> callback) {
        if (fileCallback != null) {
            fileCallback.onReceiveValue(null);
        }
        fileCallback = callback;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "application/json",
            "text/json",
            "text/plain",
            "application/octet-stream"
        });

        try {
            startActivityForResult(intent, FILE_CHOOSER_REQUEST);
        } catch (ActivityNotFoundException e) {
            fileCallback = null;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != FILE_CHOOSER_REQUEST || fileCallback == null) {
            return;
        }

        Uri[] results = null;
        if (resultCode == RESULT_OK && data != null) {
            ClipData clipData = data.getClipData();
            if (clipData != null && clipData.getItemCount() > 0) {
                results = new Uri[clipData.getItemCount()];
                for (int i = 0; i < clipData.getItemCount(); i++) {
                    results[i] = clipData.getItemAt(i).getUri();
                }
            } else if (data.getData() != null) {
                results = new Uri[] { data.getData() };
            }
        }
        fileCallback.onReceiveValue(results);
        fileCallback = null;
    }

    @Override
    public void onBackPressed() {
        if (webView != null) {
            webView.evaluateJavascript("Boolean(window.handleNativeBack && window.handleNativeBack())", new BackNavigationCallback(this));
            return;
        }
        super.onBackPressed();
    }

    void exitFromBack() {
        super.onBackPressed();
    }

    void openExternal(String url) {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        startActivity(intent);
    }

    boolean isDarkMode() {
        int mode = getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        return mode == Configuration.UI_MODE_NIGHT_YES;
    }
}
