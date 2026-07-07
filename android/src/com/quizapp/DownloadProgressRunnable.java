package com.quizapp;

import android.app.DownloadManager;
import android.content.Context;
import android.database.Cursor;

class DownloadProgressRunnable implements Runnable {
    private final MainActivity activity;
    private final long downloadId;

    DownloadProgressRunnable(MainActivity activity, long downloadId) {
        this.activity = activity;
        this.downloadId = downloadId;
    }

    @Override
    public void run() {
        DownloadManager manager = (DownloadManager) activity.getSystemService(Context.DOWNLOAD_SERVICE);
        boolean done = false;
        while (!done) {
            Cursor cursor = null;
            try {
                DownloadManager.Query query = new DownloadManager.Query();
                query.setFilterById(downloadId);
                cursor = manager.query(query);
                if (cursor == null || !cursor.moveToFirst()) {
                    activity.reportDownloadProgress(0, "无法读取下载进度");
                    return;
                }
                int status = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
                int downloaded = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
                int total = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
                int percent = total > 0 ? Math.min(99, Math.max(0, (int) ((downloaded * 100L) / total))) : 0;

                if (status == DownloadManager.STATUS_SUCCESSFUL) {
                    activity.reportDownloadProgress(100, "下载完成，正在打开安装器");
                    done = true;
                } else if (status == DownloadManager.STATUS_FAILED) {
                    activity.reportDownloadProgress(percent, "下载失败，请打开 Release 链接重试");
                    done = true;
                } else if (total > 0) {
                    activity.reportDownloadProgress(percent, "下载中 " + percent + "%");
                } else {
                    activity.reportDownloadProgress(0, "下载中，正在获取文件大小");
                }
            } catch (Exception e) {
                activity.reportDownloadProgress(0, "无法读取下载进度");
                done = true;
            } finally {
                if (cursor != null) {
                    cursor.close();
                }
            }

            if (!done) {
                try {
                    Thread.sleep(600L);
                } catch (InterruptedException e) {
                    return;
                }
            }
        }
    }
}
