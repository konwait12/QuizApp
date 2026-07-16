package com.quizapp;

class ChooseBackupDestinationRunnable implements Runnable {
    private final MainActivity activity;
    private final String fileName;

    ChooseBackupDestinationRunnable(MainActivity activity, String fileName) {
        this.activity = activity;
        this.fileName = fileName;
    }

    @Override
    public void run() {
        activity.chooseBackupDestination(fileName);
    }
}
