package org.quizapp.platform;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.net.Uri;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

public final class LegacyMigrationBridge {
    public static final int IDLE = 0;
    public static final int RUNNING = 1;
    public static final int COMPLETE = 2;
    public static final int FAILED = 3;
    public static final int NO_DATA = 4;

    private static final int MAX_PAYLOAD_CHARS = 512 * 1024 * 1024;
    private static volatile int status = IDLE;
    private static volatile String resultPath = "";
    private static volatile String error = "";
    private static WebView activeWebView;

    private LegacyMigrationBridge() {}

    public static boolean hasSourceData() {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null) {
            return true;
        }
        return containsFile(new File(activity.getDataDir(), "app_webview"), 0);
    }

    private static boolean containsFile(File entry, int depth) {
        if (entry == null || !entry.exists() || depth > 8) {
            return false;
        }
        if (entry.isFile()) {
            return entry.length() > 0;
        }
        File[] children = entry.listFiles();
        if (children == null) {
            return false;
        }
        for (File child : children) {
            if (containsFile(child, depth + 1)) {
                return true;
            }
        }
        return false;
    }

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    public static synchronized boolean start(String sourceVersion) {
        final Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || status == RUNNING) {
            return false;
        }
        status = RUNNING;
        resultPath = "";
        error = "";
        activity.runOnUiThread(() -> {
            try {
                destroyWebView();
                WebView webView = new WebView(activity);
                activeWebView = webView;
                webView.setVisibility(View.INVISIBLE);
                webView.getSettings().setJavaScriptEnabled(true);
                webView.getSettings().setDomStorageEnabled(true);
                webView.getSettings().setDatabaseEnabled(true);
                webView.getSettings().setAllowFileAccess(true);
                webView.getSettings().setAllowContentAccess(false);
                webView.getSettings().setBlockNetworkLoads(true);
                webView.getSettings().setAllowFileAccessFromFileURLs(false);
                webView.getSettings().setAllowUniversalAccessFromFileURLs(false);
                webView.addJavascriptInterface(new ExportInterface(activity), "QuizAppMigration");
                webView.setWebViewClient(new WebViewClient() {
                    @Override
                    public boolean shouldOverrideUrlLoading(
                            WebView view, WebResourceRequest request) {
                        String scheme = request.getUrl().getScheme();
                        return !"file".equalsIgnoreCase(scheme);
                    }
                });
                activity.addContentView(webView, new FrameLayout.LayoutParams(1, 1));
                String version = sourceVersion == null ? "legacy-webview" : sourceVersion;
                webView.loadUrl(
                        "file:///android_asset/index.html"
                                + "?sourceVersion=" + Uri.encode(version));
            } catch (Exception exception) {
                finishFailed(exception.toString());
            }
        });
        return true;
    }

    public static int status() {
        return status;
    }

    public static String resultPath() {
        return resultPath;
    }

    public static String error() {
        return error;
    }

    public static synchronized void clearResult() {
        if (!resultPath.isEmpty()) {
            new File(resultPath).delete();
        }
        resultPath = "";
        error = "";
        status = IDLE;
        Activity activity = QuizAppActivity.currentActivity();
        if (activity != null) activity.runOnUiThread(LegacyMigrationBridge::destroyWebView);
    }

    private static synchronized void finishComplete(String path) {
        resultPath = path;
        error = "";
        status = COMPLETE;
        destroyOnUiThread();
    }

    private static synchronized void finishFailed(String message) {
        resultPath = "";
        error = message == null ? "Legacy WebView export failed" : message;
        status = FAILED;
        destroyOnUiThread();
    }

    private static synchronized void finishNoData() {
        resultPath = "";
        error = "";
        status = NO_DATA;
        destroyOnUiThread();
    }

    private static void destroyOnUiThread() {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity != null) activity.runOnUiThread(LegacyMigrationBridge::destroyWebView);
    }

    private static void destroyWebView() {
        WebView webView = activeWebView;
        activeWebView = null;
        if (webView == null) return;
        webView.stopLoading();
        webView.removeJavascriptInterface("QuizAppMigration");
        if (webView.getParent() instanceof ViewGroup) {
            ((ViewGroup) webView.getParent()).removeView(webView);
        }
        webView.destroy();
    }

    private static final class ExportInterface {
        private final Activity activity;

        ExportInterface(Activity activity) {
            this.activity = activity;
        }

        @JavascriptInterface
        public void complete(String payload) {
            if (payload == null || payload.isEmpty() || payload.length() > MAX_PAYLOAD_CHARS) {
                finishFailed("Legacy migration payload is empty or too large");
                return;
            }
            try {
                File directory = new File(activity.getFilesDir(), "legacy-migration");
                if (!directory.exists() && !directory.mkdirs()) {
                    throw new IllegalStateException("Legacy migration directory cannot be created");
                }
                File temporary = new File(directory, "legacy-v1.json.tmp");
                File target = new File(directory, "legacy-v1.json");
                try (FileOutputStream stream = new FileOutputStream(temporary, false)) {
                    stream.write(payload.getBytes(StandardCharsets.UTF_8));
                    stream.getFD().sync();
                }
                try {
                    Files.move(
                            temporary.toPath(),
                            target.toPath(),
                            StandardCopyOption.REPLACE_EXISTING,
                            StandardCopyOption.ATOMIC_MOVE);
                } catch (Exception ignored) {
                    Files.move(
                            temporary.toPath(),
                            target.toPath(),
                            StandardCopyOption.REPLACE_EXISTING);
                }
                finishComplete(target.getAbsolutePath());
            } catch (Exception exception) {
                finishFailed(exception.toString());
            }
        }

        @JavascriptInterface
        public void noData() {
            finishNoData();
        }

        @JavascriptInterface
        public void fail(String message) {
            finishFailed(message);
        }
    }
}
