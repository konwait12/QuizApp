package org.quizapp.platform;

import android.app.Activity;
import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

import java.lang.ref.WeakReference;

public final class QuizAppActivity extends QtActivity {
    private static WeakReference<Activity> current = new WeakReference<>(null);

    public static Activity currentActivity() {
        return current.get();
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        current = new WeakReference<>(this);
    }

    @Override
    protected void onDestroy() {
        if (current.get() == this) {
            current.clear();
        }
        super.onDestroy();
    }
}
