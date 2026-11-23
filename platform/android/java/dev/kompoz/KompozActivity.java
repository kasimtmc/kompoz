package dev.kompoz;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.WindowManager;

import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * The activity a Kompoz app is: its window is one {@link KompozView}, which Kompoz draws into
 * from native code (the app's libmain.so, run on this activity's UI thread), and its lifecycle,
 * configuration and the system's services go to native code from here. An app's activity
 * extends it (and names another library in {@link #library()} if its own isn't libmain.so).
 */
public class KompozActivity extends Activity {
    static KompozActivity current;  // the app's: the last created (an app has one: singleInstance)
    KompozView view;

    /** The native library with the app (Kompoz's createApplication) in it. */
    protected String library() {
        return "main";
    }

    // rememberSaveable values outlive the process: Android may end the app in the background
    // and create it again from its saved instance state.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        System.loadLibrary(library());
        // Created again in a process that ran one (swiped away from the recent apps while a service kept the
        // process, or started anew in its task): the app starts again in it, its composition new and the
        // process's state (statics, what the services hold) kept, as Compose's setContent in a new activity.
        // One still there (Android ends it after creating this one) hands over: it is finished.
        KompozActivity previous = current;
        current = this;
        KompozInput.onCreate(savedInstanceState);
        super.onCreate(savedInstanceState);
        view = new KompozView(this);
        setContentView(view);
        view.requestFocus();
        nativeCreate(this, getAssets(), getFilesDir().getAbsolutePath(), dir(getExternalFilesDir(null)), getCacheDir().getAbsolutePath());
        if (previous != null && previous != this) previous.finish();
        DisplayManager displays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (displays != null) displays.registerDisplayListener(displayListener, null);
    }

    private static String dir(java.io.File file) {
        return file != null ? file.getAbsolutePath() : "";
    }

    // The display's refresh rate changes as the app asks for one (setFrameRate) or the system picks.
    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {}

        @Override
        public void onDisplayRemoved(int displayId) {}

        @Override
        public void onDisplayChanged(int displayId) {
            Display display = view != null ? view.getDisplay() : null;
            if (display != null && display.getDisplayId() == displayId) lifecycle(EVENT_METRICS);
        }
    };

    // What happens to the app, as native code's platform::EventType takes it (KompozView's
    // nativeEvent for the surface's; these the activity's).
    static final int EVENT_WILL_ENTER_BACKGROUND = 0, EVENT_DID_ENTER_BACKGROUND = 1, EVENT_WILL_ENTER_FOREGROUND = 2,
                     EVENT_DID_ENTER_FOREGROUND = 3, EVENT_LOW_MEMORY = 4, EVENT_FOCUS_GAINED = 5, EVENT_FOCUS_LOST = 6, EVENT_METRICS = 7,
                     EVENT_BACK = 8;

    @Override
    protected void onStart() {
        super.onStart();
        lifecycle(EVENT_WILL_ENTER_FOREGROUND);
    }

    @Override
    protected void onResume() {
        super.onResume();
        lifecycle(EVENT_DID_ENTER_FOREGROUND);
    }

    // The native side keeps the app's saved state as it pauses (KompozInput.stateSaved).
    @Override
    protected void onPause() {
        lifecycle(EVENT_WILL_ENTER_BACKGROUND);
        super.onPause();
    }

    @Override
    protected void onStop() {
        lifecycle(EVENT_DID_ENTER_BACKGROUND);
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        DisplayManager displays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (displays != null) displays.unregisterDisplayListener(displayListener);
        if (current == this) {  // (one another replaced has no app of its own)
            nativeDestroy();
            current = null;
        }
        super.onDestroy();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        KompozInput.onSaveInstanceState(outState);
    }

    // A kompoz:// link opened while the app runs (it is singleInstance): to the NavHost.
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        KompozInput.onNewIntent(intent);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        lifecycle(hasFocus ? EVENT_FOCUS_GAINED : EVENT_FOCUS_LOST);
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (level >= TRIM_MEMORY_RUNNING_LOW) lifecycle(EVENT_LOW_MEMORY);
    }

    // Dark mode, the language, the font size, the screen turned: the manifest keeps the activity
    // through them (configChanges), and Kompoz lays out again.
    @Override
    public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        lifecycle(EVENT_METRICS);
    }

    // ---- Called from native code, on the UI thread ----------------------------------------

    // The screen's density (px per dp), its refresh rate (Hz), and whether the system is dark.
    float[] displayInfo() {
        float density = getResources().getDisplayMetrics().density;
        Display display = view != null ? view.getDisplay() : null;
        float rate = display != null ? display.getRefreshRate() : 60;
        boolean dark = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        return new float[] {density, rate, dark ? 1 : 0};
    }

    String language() {
        Locale locale = Build.VERSION.SDK_INT >= 24 ? getResources().getConfiguration().getLocales().get(0) : Locale.getDefault();
        return locale != null ? locale.getLanguage() : "";
    }

    // Text crosses JNI as UTF-8 bytes (JNI's own strings are modified UTF-8, which emoji break).
    byte[] clipboard() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard == null || !clipboard.hasPrimaryClip()) return new byte[0];
        ClipData clip = clipboard.getPrimaryClip();
        CharSequence text = clip != null && clip.getItemCount() > 0 ? clip.getItemAt(0).coerceToText(this) : null;
        return text != null ? text.toString().getBytes(StandardCharsets.UTF_8) : new byte[0];
    }

    void setClipboard(byte[] text) {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText(null, new String(text, StandardCharsets.UTF_8)));
    }

    // A clip as native code takes it (kompoz::ClipEntry): its MIME types, a line each, then each
    // item's text, HTML and URI (null where it has none), all UTF-8.
    static byte[][] clipParts(ClipData clip) {
        ClipDescription description = clip.getDescription();
        StringBuilder types = new StringBuilder();
        for (int i = 0; description != null && i < description.getMimeTypeCount(); ++i) types.append(description.getMimeType(i)).append('\n');
        byte[][] parts = new byte[1 + clip.getItemCount() * 3][];
        parts[0] = types.toString().getBytes(StandardCharsets.UTF_8);
        for (int i = 0; i < clip.getItemCount(); ++i) {
            ClipData.Item item = clip.getItemAt(i);
            CharSequence text = item.getText();
            String html = item.getHtmlText();
            Uri uri = item.getUri();
            parts[1 + i * 3] = text != null ? text.toString().getBytes(StandardCharsets.UTF_8) : null;
            parts[2 + i * 3] = html != null ? html.getBytes(StandardCharsets.UTF_8) : null;
            parts[3 + i * 3] = uri != null ? uri.toString().getBytes(StandardCharsets.UTF_8) : null;
        }
        return parts;
    }

    // All the clipboard holds (null: empty). Reading it is what Android tells the user of ("pasted").
    byte[][] clipEntry() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = clipboard != null && clipboard.hasPrimaryClip() ? clipboard.getPrimaryClip() : null;
        return clip != null ? clipParts(clip) : null;
    }

    // What it holds, its MIME types a line each, unread (nothing: empty).
    byte[] clipMimeTypes() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipDescription description = clipboard != null && clipboard.hasPrimaryClip() ? clipboard.getPrimaryClipDescription() : null;
        StringBuilder types = new StringBuilder();
        for (int i = 0; description != null && i < description.getMimeTypeCount(); ++i) types.append(description.getMimeType(i)).append('\n');
        return types.toString().getBytes(StandardCharsets.UTF_8);
    }

    // A clip from native code, as clipParts gives one (no items: the clipboard emptied).
    void setClipEntry(byte[][] parts) {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard == null) return;
        String[] types = new String(parts[0], StandardCharsets.UTF_8).split("\n");
        ClipData clip = null;
        for (int i = 1; i + 2 < parts.length; i += 3) {
            String text = parts[i] != null ? new String(parts[i], StandardCharsets.UTF_8) : null;
            String html = parts[i + 1] != null ? new String(parts[i + 1], StandardCharsets.UTF_8) : null;
            Uri uri = parts[i + 2] != null ? Uri.parse(new String(parts[i + 2], StandardCharsets.UTF_8)) : null;
            ClipData.Item item = new ClipData.Item(text, html, null, uri);
            if (clip == null) clip = new ClipData(new ClipDescription(null, types), item);
            else clip.addItem(item);
        }
        if (clip != null) clipboard.setPrimaryClip(clip);
        else if (Build.VERSION.SDK_INT >= 28) clipboard.clearPrimaryClip();
        else clipboard.setPrimaryClip(ClipData.newPlainText(null, ""));
    }

    // The browser, or the app for the address's scheme; false if none takes it.
    boolean openUri(byte[] uri) {
        try {
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(new String(uri, StandardCharsets.UTF_8)));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(intent);
            return true;
        } catch (RuntimeException e) {
            return false;
        }
    }

    void keepScreenOn(boolean on) {
        if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    // Back that nothing in the app took: the system's own (leaving the app).
    @SuppressWarnings("deprecation")
    void leave() {
        super.onBackPressed();
    }

    // Before Android 13 back (the key) goes to Kompoz first, which leaves the app if nothing in it
    // takes it; from 13 it comes as the callback KompozInput registers while Kompoz takes it, the
    // system going back itself otherwise.
    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (Build.VERSION.SDK_INT >= 33) super.onBackPressed();
        else lifecycle(EVENT_BACK);
    }

    // What happens to it, to the app's native side while it is the app's.
    private void lifecycle(int event) {
        if (current == this) nativeLifecycle(event);
    }

    private static native void nativeCreate(KompozActivity activity, android.content.res.AssetManager assets, String files,
                                            String externalFiles, String cache);
    private static native void nativeDestroy();
    static native void nativeLifecycle(int event);
}
