package org.quizapp.platform;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.FileProvider;

import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.lang.ref.WeakReference;

public final class QuizAppActivity extends QtActivity {
    private static final String TAG = "QuizAppUpdate";
    private static WeakReference<Activity> current = new WeakReference<>(null);
    private static String pendingPackagePath;

    public static Activity currentActivity() {
        return current.get();
    }

    public static boolean installDownloadedPackage(String path) {
        Activity activity = current.get();
        if (!(activity instanceof QuizAppActivity) || path == null || path.isEmpty()) {
            return false;
        }
        activity.runOnUiThread(() -> ((QuizAppActivity) activity).launchPackageInstaller(path));
        return true;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        current = new WeakReference<>(this);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (pendingPackagePath != null
                && (Build.VERSION.SDK_INT < Build.VERSION_CODES.O
                    || getPackageManager().canRequestPackageInstalls())) {
            String path = pendingPackagePath;
            pendingPackagePath = null;
            launchPackageInstaller(path);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (BackupDocumentBridge.handleActivityResult(this, requestCode, resultCode, data)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void launchPackageInstaller(String path) {
        File packageFile = new File(path);
        if (!packageFile.isFile()) {
            Log.e(TAG, "Downloaded package does not exist: " + path);
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getPackageManager().canRequestPackageInstalls()) {
            pendingPackagePath = path;
            Intent permissionIntent = new Intent(
                    Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + getPackageName()));
            startActivity(permissionIntent);
            return;
        }
        try {
            Uri packageUri = FileProvider.getUriForFile(
                    this,
                    getPackageName() + ".qtprovider",
                    packageFile);
            Intent installIntent = new Intent(Intent.ACTION_VIEW);
            installIntent.setDataAndType(
                    packageUri,
                    "application/vnd.android.package-archive");
            installIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            installIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(installIntent);
        } catch (RuntimeException error) {
            Log.e(TAG, "Unable to launch package installer", error);
        }
    }

    @Override
    protected void onDestroy() {
        if (current.get() == this) {
            current.clear();
        }
        super.onDestroy();
    }
}
