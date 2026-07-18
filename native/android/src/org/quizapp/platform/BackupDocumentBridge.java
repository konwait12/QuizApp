package org.quizapp.platform;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class BackupDocumentBridge {
    private static final int REQUEST_OPEN = 9721;
    private static final int REQUEST_CREATE = 9722;
    private static final int STATE_IDLE = 0;
    private static final int STATE_PENDING = 1;
    private static final int STATE_SUCCEEDED = 2;
    private static final int STATE_FAILED = 3;
    private static final int STATE_CANCELLED = 4;
    private static final int KIND_NONE = 0;
    private static final int KIND_IMPORT = 1;
    private static final int KIND_EXPORT = 2;
    private static final ExecutorService IO_EXECUTOR = Executors.newSingleThreadExecutor();

    private static int state = STATE_IDLE;
    private static int kind = KIND_NONE;
    private static String sourcePath = "";
    private static String destinationPath = "";
    private static String completedPath = "";
    private static String error = "";

    private BackupDocumentBridge() {}

    public static synchronized boolean openDocument(String localDestinationPath) {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || state == STATE_PENDING || localDestinationPath == null
                || localDestinationPath.isEmpty()) return false;
        resetLocked();
        state = STATE_PENDING;
        kind = KIND_IMPORT;
        destinationPath = localDestinationPath;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        try {
            activity.startActivityForResult(intent, REQUEST_OPEN);
            return true;
        } catch (RuntimeException exception) {
            failLocked(exception);
            return false;
        }
    }

    public static synchronized boolean createDocument(String localSourcePath, String suggestedName) {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null || state == STATE_PENDING || localSourcePath == null
                || localSourcePath.isEmpty() || !new File(localSourcePath).isFile()) return false;
        resetLocked();
        state = STATE_PENDING;
        kind = KIND_EXPORT;
        sourcePath = localSourcePath;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_TITLE,
                suggestedName == null || suggestedName.isEmpty()
                        ? "QuizApp.quizbackup" : suggestedName);
        try {
            activity.startActivityForResult(intent, REQUEST_CREATE);
            return true;
        } catch (RuntimeException exception) {
            failLocked(exception);
            return false;
        }
    }

    public static boolean handleActivityResult(
            Activity activity, int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_OPEN && requestCode != REQUEST_CREATE) return false;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            synchronized (BackupDocumentBridge.class) {
                state = STATE_CANCELLED;
                error = "";
            }
            return true;
        }
        Uri uri = data.getData();
        IO_EXECUTOR.execute(() -> copyDocument(activity, requestCode, uri));
        return true;
    }

    private static void copyDocument(Activity activity, int requestCode, Uri uri) {
        try {
            if (requestCode == REQUEST_OPEN) {
                String target;
                synchronized (BackupDocumentBridge.class) { target = destinationPath; }
                File destination = new File(target);
                File parent = destination.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IllegalStateException("无法创建备份导入临时目录");
                }
                try (InputStream input = activity.getContentResolver().openInputStream(uri);
                     OutputStream output = new FileOutputStream(destination, false)) {
                    if (input == null) throw new IllegalStateException("无法读取所选备份文件");
                    copy(input, output);
                }
                synchronized (BackupDocumentBridge.class) {
                    completedPath = target;
                    state = STATE_SUCCEEDED;
                }
            } else {
                String source;
                synchronized (BackupDocumentBridge.class) { source = sourcePath; }
                try (InputStream input = new FileInputStream(source);
                     OutputStream output = activity.getContentResolver().openOutputStream(uri, "w")) {
                    if (output == null) throw new IllegalStateException("无法写入所选保存位置");
                    copy(input, output);
                }
                synchronized (BackupDocumentBridge.class) {
                    completedPath = uri.toString();
                    state = STATE_SUCCEEDED;
                }
            }
        } catch (Exception exception) {
            synchronized (BackupDocumentBridge.class) { failLocked(exception); }
        }
    }

    private static void copy(InputStream input, OutputStream output) throws Exception {
        byte[] buffer = new byte[1024 * 1024];
        int count;
        while ((count = input.read(buffer)) >= 0) {
            if (count > 0) output.write(buffer, 0, count);
        }
        output.flush();
    }

    public static synchronized int resultState() { return state; }
    public static synchronized int resultKind() { return kind; }
    public static synchronized String resultPath() { return completedPath; }
    public static synchronized String resultError() { return error; }
    public static synchronized void clearResult() { resetLocked(); }

    private static void failLocked(Exception exception) {
        state = STATE_FAILED;
        error = exception.getMessage() == null
                ? exception.getClass().getSimpleName() : exception.getMessage();
    }

    private static void resetLocked() {
        state = STATE_IDLE;
        kind = KIND_NONE;
        sourcePath = "";
        destinationPath = "";
        completedPath = "";
        error = "";
    }
}
