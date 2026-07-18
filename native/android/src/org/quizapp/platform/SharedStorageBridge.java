package org.quizapp.platform;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;

import java.io.File;

public final class SharedStorageBridge {
    private static final String ROOT_NAME = "QuizApp";

    private SharedStorageBridge() {}

    public static String rootPath() {
        return new File(Environment.getExternalStorageDirectory(), ROOT_NAME).getAbsolutePath();
    }

    public static boolean requiresDirectAccessPermission() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R;
    }

    public static boolean hasDirectAccess() {
        Activity activity = QuizAppActivity.currentActivity();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        if (activity == null) {
            return false;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    public static boolean requestDirectAccess() {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null) {
            return false;
        }
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Intent appIntent = new Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + activity.getPackageName()));
                activity.startActivity(appIntent);
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                activity.requestPermissions(
                        new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE}, 721);
            }
            return true;
        } catch (Exception ignored) {
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                return true;
            } catch (Exception ignoredAgain) {
                return false;
            }
        }
    }

    public static boolean openDirectory() {
        Activity activity = QuizAppActivity.currentActivity();
        if (activity == null) {
            return false;
        }
        String documentId = "primary:" + ROOT_NAME;
        Uri initialUri = DocumentsContract.buildDocumentUri(
                "com.android.externalstorage.documents", documentId);
        if (openDocumentUri(activity, initialUri)) {
            return true;
        }
        Uri rootUri = DocumentsContract.buildRootUri(
                "com.android.externalstorage.documents", "primary");
        return openDocumentUri(activity, rootUri);
    }

    private static boolean openDocumentUri(Activity activity, Uri uri) {
        try {
            Intent view = new Intent(Intent.ACTION_VIEW);
            view.setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR);
            view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            activity.startActivity(view);
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }
}
