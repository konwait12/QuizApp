package com.quizapp;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.DownloadManager;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.content.pm.PackageManager;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.util.Base64;
import android.view.View;
import android.webkit.WebSettings;
import android.webkit.WebChromeClient;
import android.webkit.ValueCallback;
import android.webkit.WebView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

public class MainActivity extends Activity {
    static final String PUBLIC_BANK_RELATIVE_PATH = Environment.DIRECTORY_DOCUMENTS + "/QuizApp/data/";
    static final int FILE_CHOOSER_REQUEST = 1001;
    static final int BACKUP_EXPORT_REQUEST = 1002;
    static final int DOCUMENT_EXPORT_REQUEST = 1003;
    static final int STORAGE_PERMISSION_REQUEST = 1004;
    private WebView webView;
    private ValueCallback<Uri[]> fileCallback;
    private long pendingApkDownloadId = -1L;
    private OutputStream pendingBackupOutput;
    private OutputStream pendingDocumentOutput;

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
        webView.addJavascriptInterface(new NativeBridge(this), "QuizAppNative");

        setContentView(webView);
        webView.loadUrl("file:///android_asset/index.html");
    }

    void startApkDownload(String url) {
        Uri uri;
        try {
            uri = Uri.parse(url);
        } catch (Exception e) {
            Toast.makeText(this, "APK 下载链接无效", Toast.LENGTH_SHORT).show();
            reportDownloadProgress(0, "APK 下载链接无效");
            return;
        }
        if (!"https".equalsIgnoreCase(uri.getScheme())) {
            Toast.makeText(this, "仅支持 HTTPS APK 下载链接", Toast.LENGTH_SHORT).show();
            reportDownloadProgress(0, "仅支持 HTTPS APK 下载链接");
            return;
        }

        try {
            DownloadManager.Request request = new DownloadManager.Request(uri);
            request.setTitle("QuizApp 更新包");
            request.setDescription("下载完成后将打开系统安装器");
            request.setMimeType("application/vnd.android.package-archive");
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setDestinationInExternalFilesDir(this, Environment.DIRECTORY_DOWNLOADS, "QuizApp-latest.apk");

            DownloadManager manager = (DownloadManager) getSystemService(Context.DOWNLOAD_SERVICE);
            pendingApkDownloadId = manager.enqueue(request);
            registerReceiver(new DownloadCompleteReceiver(this, pendingApkDownloadId), new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE));
            reportDownloadProgress(0, "已开始下载更新包");
            new Thread(new DownloadProgressRunnable(this, pendingApkDownloadId)).start();
            Toast.makeText(this, "开始下载更新包", Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            reportDownloadProgress(0, "无法启动下载，请打开 Release 链接重试");
            Toast.makeText(this, "无法启动下载，请打开 Release 链接重试", Toast.LENGTH_LONG).show();
            openExternal(url);
        }
    }

    void installDownloadedApk(long downloadId) {
        try {
            DownloadManager manager = (DownloadManager) getSystemService(Context.DOWNLOAD_SERVICE);
            Uri apkUri = manager.getUriForDownloadedFile(downloadId);
            if (apkUri == null) {
                Toast.makeText(this, "下载未完成，请从通知栏重试", Toast.LENGTH_LONG).show();
                return;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !getPackageManager().canRequestPackageInstalls()) {
                Toast.makeText(this, "请允许安装未知来源应用，然后从下载通知安装", Toast.LENGTH_LONG).show();
                Intent settingsIntent = new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()));
                startActivity(settingsIntent);
                return;
            }
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(apkUri, "application/vnd.android.package-archive");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(intent);
        } catch (Exception e) {
            Toast.makeText(this, "无法打开安装器，请从下载通知安装", Toast.LENGTH_LONG).show();
        }
    }

    void reportDownloadProgress(int percent, String message) {
        evaluateJavascript("window.updateDownloadProgress && window.updateDownloadProgress(" + percent + "," + jsString(message) + ");");
    }

    void reportBankDirectoryResult(boolean ok, String message) {
        evaluateJavascript("window.onNativeBankDirectoryResult && window.onNativeBankDirectoryResult(" + (ok ? "true" : "false") + "," + jsString(message) + ");");
    }

    void reportBackupDestinationResult(boolean ok, String message) {
        evaluateJavascript("window.onNativeBackupDestinationReady && window.onNativeBackupDestinationReady(" + (ok ? "true" : "false") + "," + jsString(message) + ");");
    }

    void reportDocumentDestinationResult(boolean ok, String message) {
        evaluateJavascript("window.onNativeNotebookExportDestinationReady && window.onNativeNotebookExportDestinationReady(" + (ok ? "true" : "false") + "," + jsString(message) + ");");
    }

    void chooseBackupDestination(String fileName) {
        cancelBackupExport();
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/json");
        intent.putExtra(Intent.EXTRA_TITLE, fileName == null || fileName.trim().isEmpty() ? "QuizApp-backup.quizbackup" : fileName);
        try {
            startActivityForResult(intent, BACKUP_EXPORT_REQUEST);
        } catch (ActivityNotFoundException e) {
            reportBackupDestinationResult(false, "当前系统没有可用的文件保存器");
        }
    }

    synchronized boolean appendBackupChunk(String text) {
        if (pendingBackupOutput == null) {
            return false;
        }
        try {
            pendingBackupOutput.write((text == null ? "" : text).getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (Exception e) {
            cancelBackupExport();
            return false;
        }
    }

    synchronized boolean finishBackupExport() {
        if (pendingBackupOutput == null) {
            return false;
        }
        try {
            pendingBackupOutput.flush();
            pendingBackupOutput.close();
            pendingBackupOutput = null;
            return true;
        } catch (Exception e) {
            cancelBackupExport();
            return false;
        }
    }

    synchronized void cancelBackupExport() {
        if (pendingBackupOutput == null) {
            return;
        }
        try {
            pendingBackupOutput.close();
        } catch (Exception ignored) {
        }
        pendingBackupOutput = null;
    }

    void chooseDocumentDestination(String fileName, String mimeType) {
        cancelDocumentExport();
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        String resolvedType = mimeType == null || mimeType.trim().isEmpty() ? "application/octet-stream" : mimeType.trim();
        intent.setType(resolvedType);
        intent.putExtra(Intent.EXTRA_TITLE, fileName == null || fileName.trim().isEmpty() ? "QuizApp-export.pdf" : fileName);
        try {
            startActivityForResult(intent, DOCUMENT_EXPORT_REQUEST);
        } catch (ActivityNotFoundException e) {
            reportDocumentDestinationResult(false, "当前系统没有可用的文件保存器");
        }
    }

    synchronized boolean appendDocumentBase64Chunk(String encoded) {
        if (pendingDocumentOutput == null) {
            return false;
        }
        try {
            byte[] bytes = Base64.decode(encoded == null ? "" : encoded, Base64.NO_WRAP);
            pendingDocumentOutput.write(bytes);
            return true;
        } catch (Exception e) {
            cancelDocumentExport();
            return false;
        }
    }

    synchronized boolean finishDocumentExport() {
        if (pendingDocumentOutput == null) {
            return false;
        }
        try {
            pendingDocumentOutput.flush();
            pendingDocumentOutput.close();
            pendingDocumentOutput = null;
            return true;
        } catch (Exception e) {
            cancelDocumentExport();
            return false;
        }
    }

    synchronized void cancelDocumentExport() {
        if (pendingDocumentOutput == null) {
            return;
        }
        try {
            pendingDocumentOutput.close();
        } catch (Exception ignored) {
        }
        pendingDocumentOutput = null;
    }

    void openBankDirectory() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            if (!syncBundledBanksToPublicDocuments()) {
                reportBankDirectoryResult(false, "无法创建公共题库文件夹：Documents/QuizApp/data");
                Toast.makeText(this, "无法创建公共题库文件夹", Toast.LENGTH_LONG).show();
                return;
            }
            if (tryOpenPublicBankDirectory()) {
                reportBankDirectoryResult(true, "已打开默认题库文件夹：Documents/QuizApp/data");
                Toast.makeText(this, "已打开 Documents/QuizApp/data", Toast.LENGTH_SHORT).show();
                return;
            }
            if (tryOpenDocumentTree(publicBankDocumentUri())) {
                reportBankDirectoryResult(true, "已定位默认题库文件夹：Documents/QuizApp/data");
                Toast.makeText(this, "请选择或打开 Documents/QuizApp/data", Toast.LENGTH_LONG).show();
                return;
            }
            reportBankDirectoryResult(false, "已创建 Documents/QuizApp/data，但当前系统没有可用的文件管理器。");
            Toast.makeText(this, "当前系统没有可用的文件管理器", Toast.LENGTH_LONG).show();
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
            && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE }, STORAGE_PERMISSION_REQUEST);
            return;
        }

        File directory = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS), "QuizApp/data");
        if (!ensureDirectory(directory)) {
            reportBankDirectoryResult(false, "无法创建默认题库文件夹");
            Toast.makeText(this, "无法创建默认题库文件夹", Toast.LENGTH_LONG).show();
            return;
        }
        syncBundledBanks(directory);
        if (tryOpenDirectory(directory)) {
            reportBankDirectoryResult(true, "已打开默认题库文件夹：Documents/QuizApp/data");
            Toast.makeText(this, "已打开 Documents/QuizApp/data", Toast.LENGTH_SHORT).show();
            return;
        }
        if (tryOpenDocumentTree(publicBankDocumentUri())) {
            reportBankDirectoryResult(true, "已定位默认题库文件夹：Documents/QuizApp/data");
            Toast.makeText(this, "请选择或打开 Documents/QuizApp/data", Toast.LENGTH_LONG).show();
            return;
        }
        reportBankDirectoryResult(false, "已创建 Documents/QuizApp/data，但当前系统没有可用的文件管理器。");
        Toast.makeText(this, "当前系统没有可用的文件管理器", Toast.LENGTH_LONG).show();
    }

    private boolean syncBundledBanksToPublicDocuments() {
        try {
            syncPublicBankTree(getAssets(), "data", "");
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private void syncPublicBankTree(AssetManager assets, String assetDirectory, String relativeDirectory) throws Exception {
        String[] names = assets.list(assetDirectory);
        if (names == null) {
            return;
        }
        for (int i = 0; i < names.length; i++) {
            String name = names[i];
            if (name == null || name.isEmpty()) {
                continue;
            }
            String assetPath = assetDirectory + "/" + name;
            String[] children = assets.list(assetPath);
            if (children != null && children.length > 0 && !name.endsWith(".json")) {
                syncPublicBankTree(assets, assetPath, relativeDirectory + name + "/");
            } else if (name.endsWith(".json")) {
                copyAssetToPublicDocuments(assets, assetPath, relativeDirectory, name);
            }
        }
    }

    private void copyAssetToPublicDocuments(AssetManager assets, String assetPath, String relativeDirectory, String fileName) throws Exception {
        String relativePath = PUBLIC_BANK_RELATIVE_PATH + relativeDirectory;
        Uri collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI;
        String[] projection = new String[] { MediaStore.MediaColumns._ID };
        String selection = MediaStore.MediaColumns.DISPLAY_NAME + "=? AND " + MediaStore.MediaColumns.RELATIVE_PATH + "=?";
        String[] selectionArgs = new String[] { fileName, relativePath };
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(collection, projection, selection, selectionArgs, null);
            if (cursor != null && cursor.moveToFirst()) {
                return;
            }
        } finally {
            if (cursor != null) cursor.close();
        }

        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, fileName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/json");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, relativePath);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri target = getContentResolver().insert(collection, values);
        if (target == null) {
            throw new IllegalStateException("Unable to create public bank file");
        }
        try {
            InputStream input = assets.open(assetPath);
            OutputStream output = getContentResolver().openOutputStream(target, "w");
            if (output == null) {
                input.close();
                throw new IllegalStateException("Unable to write public bank file");
            }
            try {
                byte[] buffer = new byte[8192];
                int read;
                while ((read = input.read(buffer)) > 0) output.write(buffer, 0, read);
                output.flush();
            } finally {
                input.close();
                output.close();
            }
            ContentValues ready = new ContentValues();
            ready.put(MediaStore.MediaColumns.IS_PENDING, 0);
            getContentResolver().update(target, ready, null, null);
        } catch (Exception e) {
            getContentResolver().delete(target, null, null);
            throw e;
        }
    }

    private Uri publicBankDocumentUri() {
        return DocumentsContract.buildDocumentUri(
            "com.android.externalstorage.documents",
            "primary:" + Environment.DIRECTORY_DOCUMENTS + "/QuizApp/data"
        );
    }

    private boolean tryOpenPublicBankDirectory() {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(publicBankDocumentUri(), DocumentsContract.Document.MIME_TYPE_DIR);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        return tryStart(intent);
    }

    private boolean ensureDirectory(File directory) {
        return directory.exists() || directory.mkdirs();
    }

    private void syncBundledBanks(File directory) {
        try {
            AssetManager assets = getAssets();
            syncBundledBankTree(assets, "data", directory);
        } catch (Exception ignored) {
        }
    }

    private void syncBundledBankTree(AssetManager assets, String assetDirectory, File targetDirectory) throws Exception {
        String[] names = assets.list(assetDirectory);
        if (names == null) {
            return;
        }
        for (int i = 0; i < names.length; i++) {
            String name = names[i];
            if (name == null || name.isEmpty()) {
                continue;
            }
            String assetPath = assetDirectory + "/" + name;
            File target = new File(targetDirectory, name);
            if (name.endsWith(".json")) {
                if (!target.exists()) {
                    copyAsset(assets, assetPath, target);
                }
                continue;
            }
            String[] children = assets.list(assetPath);
            if (children != null && children.length > 0) {
                if (!ensureDirectory(target)) {
                    throw new IllegalStateException("Unable to create bank directory");
                }
                syncBundledBankTree(assets, assetPath, target);
            }
        }
    }

    private void copyAsset(AssetManager assets, String assetPath, File target) throws Exception {
        InputStream input = null;
        FileOutputStream output = null;
        try {
            File parent = target.getParentFile();
            if (parent != null && !ensureDirectory(parent)) {
                throw new IllegalStateException("Unable to create bank directory");
            }
            input = assets.open(assetPath);
            output = new FileOutputStream(target);
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) > 0) {
                output.write(buffer, 0, read);
            }
        } finally {
            if (input != null) {
                input.close();
            }
            if (output != null) {
                output.close();
            }
        }
    }

    private boolean tryOpenDirectory(File directory) {
        Intent folderIntent = new Intent(Intent.ACTION_VIEW);
        folderIntent.setDataAndType(Uri.fromFile(directory), "resource/folder");
        folderIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (tryStart(folderIntent)) {
            return true;
        }

        Intent fileIntent = new Intent(Intent.ACTION_VIEW);
        fileIntent.setData(Uri.parse(directory.toURI().toString()));
        fileIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        return tryStart(fileIntent);
    }

    private boolean tryOpenDocumentTree(Uri initialUri) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && initialUri != null) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, initialUri);
        }
        return tryStart(intent);
    }

    private boolean tryStart(Intent intent) {
        try {
            startActivity(intent);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private void evaluateJavascript(String script) {
        if (webView != null) {
            webView.post(new EvaluateJavascriptRunnable(webView, script));
        }
    }

    private String jsString(String value) {
        if (value == null) {
            return "''";
        }
        String text = value
            .replace("\\", "\\\\")
            .replace("'", "\\'")
            .replace("\n", "\\n")
            .replace("\r", "");
        return "'" + text + "'";
    }

    void openFileChooser(ValueCallback<Uri[]> callback, WebChromeClient.FileChooserParams params) {
        if (fileCallback != null) {
            fileCallback.onReceiveValue(null);
        }
        fileCallback = callback;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        ArrayList<String> mimeTypes = new ArrayList<>();
        if (params != null && params.getAcceptTypes() != null) {
            for (String acceptGroup : params.getAcceptTypes()) {
                if (acceptGroup == null) continue;
                for (String accept : acceptGroup.split(",")) {
                    String value = accept.trim();
                    if (value.contains("/") && !mimeTypes.contains(value)) {
                        mimeTypes.add(value);
                    }
                }
            }
        }
        if (mimeTypes.isEmpty()) {
            mimeTypes.add("application/json");
            mimeTypes.add("text/plain");
            mimeTypes.add("image/*");
            mimeTypes.add("application/pdf");
        }
        intent.setType(mimeTypes.size() == 1 ? mimeTypes.get(0) : "*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes.toArray(new String[0]));
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE,
            params != null && params.getMode() == WebChromeClient.FileChooserParams.MODE_OPEN_MULTIPLE);

        try {
            startActivityForResult(intent, FILE_CHOOSER_REQUEST);
        } catch (ActivityNotFoundException e) {
            fileCallback = null;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == DOCUMENT_EXPORT_REQUEST) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                cancelDocumentExport();
                reportDocumentDestinationResult(false, "已取消保存");
                return;
            }
            try {
                pendingDocumentOutput = getContentResolver().openOutputStream(data.getData(), "w");
                if (pendingDocumentOutput == null) {
                    reportDocumentDestinationResult(false, "无法打开所选保存位置");
                    return;
                }
                reportDocumentDestinationResult(true, "已选择保存位置");
            } catch (Exception e) {
                cancelDocumentExport();
                reportDocumentDestinationResult(false, "无法创建导出文件");
            }
            return;
        }
        if (requestCode == BACKUP_EXPORT_REQUEST) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                cancelBackupExport();
                reportBackupDestinationResult(false, "已取消保存");
                return;
            }
            try {
                pendingBackupOutput = getContentResolver().openOutputStream(data.getData(), "w");
                if (pendingBackupOutput == null) {
                    reportBackupDestinationResult(false, "无法打开所选保存位置");
                    return;
                }
                reportBackupDestinationResult(true, "已选择保存位置");
            } catch (Exception e) {
                cancelBackupExport();
                reportBackupDestinationResult(false, "无法创建备份文件");
            }
            return;
        }
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
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != STORAGE_PERMISSION_REQUEST) return;
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            openBankDirectory();
        } else {
            reportBankDirectoryResult(false, "需要存储权限才能创建 Documents/QuizApp/data");
            Toast.makeText(this, "未授予存储权限", Toast.LENGTH_LONG).show();
        }
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
