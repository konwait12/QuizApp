package com.quizapp;

class OpenBankDirectoryRunnable implements Runnable {
    private final MainActivity activity;

    OpenBankDirectoryRunnable(MainActivity activity) {
        this.activity = activity;
    }

    @Override
    public void run() {
        activity.openBankDirectory();
    }
}
