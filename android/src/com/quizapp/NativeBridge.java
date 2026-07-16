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

    @JavascriptInterface
    public void chooseBackupDestination(String fileName) {
        activity.runOnUiThread(new ChooseBackupDestinationRunnable(activity, fileName));
    }

    @JavascriptInterface
    public boolean appendBackupChunk(String text) {
        return activity.appendBackupChunk(text);
    }

    @JavascriptInterface
    public boolean finishBackupExport() {
        return activity.finishBackupExport();
    }

    @JavascriptInterface
    public void cancelBackupExport() {
        activity.cancelBackupExport();
    }

    @JavascriptInterface
    public void chooseDocumentDestination(String fileName, String mimeType) {
        activity.runOnUiThread(new ChooseDocumentDestinationRunnable(activity, fileName, mimeType));
    }

    @JavascriptInterface
    public boolean appendDocumentBase64Chunk(String encoded) {
        return activity.appendDocumentBase64Chunk(encoded);
    }

    @JavascriptInterface
    public boolean finishDocumentExport() {
        return activity.finishDocumentExport();
    }

    @JavascriptInterface
    public void cancelDocumentExport() {
        activity.cancelDocumentExport();
    }
}
