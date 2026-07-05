package com.quizapp;

import android.webkit.ValueCallback;

@SuppressWarnings("rawtypes")
class BackNavigationCallback implements ValueCallback {
    private final MainActivity activity;

    BackNavigationCallback(MainActivity activity) {
        this.activity = activity;
    }

    @Override
    public void onReceiveValue(Object value) {
        if (!"true".equals(String.valueOf(value))) {
            activity.exitFromBack();
        }
    }
}
