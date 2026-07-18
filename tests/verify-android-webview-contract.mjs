import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const manifest = read('android/AndroidManifest.xml');
const activity = read('android/src/com/quizapp/MainActivity.java');
const bridge = read('android/src/com/quizapp/NativeBridge.java');
const build = read('scripts/build-apk.ps1');
const index = read('index.html');

assert.match(manifest, /package="com\.quizapp"/);
assert.match(manifest, /android:name="android\.permission\.INTERNET"/);
assert.match(manifest, /android:name="android\.permission\.REQUEST_INSTALL_PACKAGES"/);
assert.match(manifest, /WRITE_EXTERNAL_STORAGE" android:maxSdkVersion="28"/);
assert.match(manifest, /android:allowBackup="true"/);

assert.match(activity, /file:\/\/\/android_asset\/index\.html/);
assert.match(activity, /setDomStorageEnabled\(true\)/);
assert.match(activity, /addJavascriptInterface\(new NativeBridge\(this\), "QuizAppNative"\)/);
assert.match(activity, /window\.handleNativeBack/);
assert.match(activity, /Intent\.ACTION_OPEN_DOCUMENT/);
assert.match(activity, /Intent\.EXTRA_ALLOW_MULTIPLE/);
assert.match(activity, /DownloadManager\.Request/);
assert.match(activity, /ACTION_MANAGE_UNKNOWN_APP_SOURCES/);
assert.match(activity, /Environment\.DIRECTORY_DOCUMENTS \+ "\/QuizApp\/data\/"/);
assert.match(activity, /MediaStore\.Downloads\.EXTERNAL_CONTENT_URI/);
assert.match(activity, /tryOpenPublicBankDirectory/);
assert.doesNotMatch(activity, /Android\/data\/com\.quizapp\/files\/data/);
assert.doesNotMatch(activity, /getExternalFilesDir\(null\).*"data"/s);

assert.match(bridge, /public void openBankDirectory\(\)/);
assert.match(bridge, /public void downloadAndInstallApk\(String url\)/);
assert.match(bridge, /public void chooseBackupDestination\(String fileName\)/);
assert.match(bridge, /public void chooseDocumentDestination\(String fileName, String mimeType\)/);

assert.match(index, /window\.handleNativeBack = function handleNativeBack/);
assert.match(index, /window\.QuizAppNative\.openBankDirectory/);
assert.match(index, /window\.QuizAppNative\.downloadAndInstallApk/);
assert.match(build, /Copy-Item -LiteralPath \(Join-Path \$projectRoot "index\.html"\)/);
assert.match(build, /Copy-Item -LiteralPath \$sourceDistributionDir/);
assert.match(build, /window\.__QUIZAPP_EMBEDDED_BANKS__ = \$embeddedJson/);
assert.match(build, /Get-ChildItem -LiteralPath \(Join-Path \$androidDir "src"\) -Recurse -Filter "\*\.java"/);

console.log(JSON.stringify({
  stablePackageIdentity: true,
  webViewOriginAndDomStorage: true,
  nativeBackBridge: true,
  multiFilePicker: true,
  updateDownloadAndInstall: true,
  publicBankDirectory: 'Documents/QuizApp/data',
  apkEmbedsCurrentHtmlAndBanks: true,
}, null, 2));
