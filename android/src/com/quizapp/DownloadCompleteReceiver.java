package com.quizapp;

import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

class DownloadCompleteReceiver extends BroadcastReceiver {
    private final MainActivity activity;
    private final long downloadId;

    DownloadCompleteReceiver(MainActivity activity, long downloadId) {
        this.activity = activity;
        this.downloadId = downloadId;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        long id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L);
        if (id != downloadId) {
            return;
        }
        try {
            activity.unregisterReceiver(this);
        } catch (IllegalArgumentException ignored) {
        }
        activity.installDownloadedApk(id);
    }
}
