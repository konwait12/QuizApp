package com.quizapp;

import android.webkit.WebView;

class EvaluateJavascriptRunnable implements Runnable {
    private final WebView webView;
    private final String script;

    EvaluateJavascriptRunnable(WebView webView, String script) {
        this.webView = webView;
        this.script = script;
    }

    @Override
    public void run() {
        webView.evaluateJavascript(script, null);
    }
}
