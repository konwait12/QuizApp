package com.quizapp;

class ChooseDocumentDestinationRunnable implements Runnable {
    private final MainActivity activity;
    private final String fileName;
    private final String mimeType;

    ChooseDocumentDestinationRunnable(MainActivity activity, String fileName, String mimeType) {
        this.activity = activity;
        this.fileName = fileName;
        this.mimeType = mimeType;
    }

    @Override
    public void run() {
        activity.chooseDocumentDestination(fileName, mimeType);
    }
}
