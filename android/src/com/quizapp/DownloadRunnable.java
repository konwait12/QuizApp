package com.quizapp;

class DownloadRunnable implements Runnable {
    private final MainActivity activity;
    private final String url;

    DownloadRunnable(MainActivity activity, String url) {
        this.activity = activity;
        this.url = url;
    }

    @Override
    public void run() {
        activity.startApkDownload(url);
    }
}
