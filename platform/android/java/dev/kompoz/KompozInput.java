package dev.kompoz;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.view.HapticFeedbackConstants;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.SurfaceView;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.util.SparseArray;
import android.view.ViewStructure;
import android.view.WindowInsetsAnimation;
import android.view.autofill.AutofillManager;
import android.view.autofill.AutofillValue;
import android.os.Bundle;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.TextAttribute;
import android.widget.FrameLayout;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * Keyboard / IME input for Kompoz on Android, used instead of SDL's bridge.
 *
 * SDL's SDLInputConnection keeps a private copy of the text and forwards only differences.
 * That copy drifts as soon as the keyboard deletes with key events, applies a suggestion
 * with replaceText(), or the app changes the text, and then autocomplete garbles the text.
 * Here a single Editable is the source of truth: after every change the complete state
 * (text, selection, composing region) goes to native code, and when the app changes the
 * text the input method is restarted so it never works on stale text. This is the same
 * model as Compose's TextInputServiceAndroid.
 *
 * Offsets crossing JNI are UTF-8 byte offsets; Java works in UTF-16 indices.
 */
public final class KompozInput {
    private static InputView view;
    private static int session;  // the showing the view's text belongs to (UI thread)
    private static final Handler ui = new Handler(Looper.getMainLooper());

    private KompozInput() {}

    // ---- Called from native code on the SDL thread -----------------------------------------

    // Adds the (invisible, 1x1) input view at startup. It also reports the window insets by kind
    // (status bars, navigation bars, keyboard, display cutout, gesture edges), like Compose's
    // WindowInsets types. SDL's single safe area mixes the gesture edges in, which would inset
    // content on both sides.
    static void attach() {
        ui.post(() -> {
            ensureView();
            Activity activity = SDLActivity.getContext();
            if (activity != null) deliverLink(activity.getIntent());  // the link the app was opened with
            if (activity != null) watchMedia(activity);
        });
    }

    // The device as Compose's UiMediaScope sees it: the most precise pointer (0 none, 1 blunt,
    // 2 coarse, 3 fine), a physical keyboard, a microphone, a camera, the viewing distance
    // (0 near, 1 medium, 2 far). The window's posture needs androidx.window: not told (flat).
    private static boolean docked;
    static int[] mediaInfo() {
        Activity activity = SDLActivity.getContext();
        if (activity == null) return null;
        android.hardware.input.InputManager input = (android.hardware.input.InputManager) activity.getSystemService(Context.INPUT_SERVICE);
        int pointer = 0, fallback = 0;
        boolean keyboard = false;
        if (input != null) {
            for (int id : input.getInputDeviceIds()) {
                android.view.InputDevice device = input.getInputDevice(id);
                if (device == null) continue;
                if (device.getKeyboardType() == android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC && !device.isVirtual()) keyboard = true;
                if (pointing(device, android.view.InputDevice.SOURCE_MOUSE) || pointing(device, android.view.InputDevice.SOURCE_STYLUS) ||
                    pointing(device, android.view.InputDevice.SOURCE_TOUCHPAD)) {
                    pointer = 3;
                } else if (pointing(device, android.view.InputDevice.SOURCE_TOUCHSCREEN)) {
                    pointer = Math.max(pointer, 2);
                } else if (pointer == 0 && (pointing(device, android.view.InputDevice.SOURCE_JOYSTICK) ||
                                            pointing(device, android.view.InputDevice.SOURCE_GAMEPAD))) {
                    pointer = 1;
                }
                // Emulators: no X/Y range of the primary source, but a scroll or touch axis.
                android.view.InputDevice.MotionRange x = device.getMotionRange(android.view.MotionEvent.AXIS_X);
                if (x != null && x.isFromSource(android.view.InputDevice.SOURCE_MOUSE) &&
                    device.getMotionRange(android.view.MotionEvent.AXIS_VSCROLL) != null) {
                    fallback = 3;
                } else if (fallback != 3 && x != null && x.isFromSource(android.view.InputDevice.SOURCE_TOUCHSCREEN) &&
                           (device.getMotionRange(android.view.MotionEvent.AXIS_TOUCH_MAJOR) != null ||
                            device.getMotionRange(android.view.MotionEvent.AXIS_TOUCH_MINOR) != null)) {
                    fallback = 2;
                }
            }
        }
        if (pointer == 0) pointer = fallback;
        android.content.pm.PackageManager pm = activity.getPackageManager();
        int distance = pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_LEANBACK) ? 2
                       : pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_AUTOMOTIVE) || docked ? 1 : 0;
        return new int[] {pointer, keyboard ? 1 : 0, pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_MICROPHONE) ? 1 : 0,
                          pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_CAMERA_ANY) ? 1 : 0, distance};
    }

    private static boolean pointing(android.view.InputDevice device, int source) {
        return (device.getSources() & source) == source && device.getMotionRange(android.view.MotionEvent.AXIS_X, source) != null;
    }

    // Input devices coming and going, and docking: the native side asks mediaInfo() again.
    private static boolean watchingMedia;
    private static void watchMedia(Activity activity) {
        if (watchingMedia) return;
        watchingMedia = true;
        android.hardware.input.InputManager input = (android.hardware.input.InputManager) activity.getSystemService(Context.INPUT_SERVICE);
        if (input != null)
            input.registerInputDeviceListener(new android.hardware.input.InputManager.InputDeviceListener() {
                @Override public void onInputDeviceAdded(int id) { nativeOnMediaChanged(); }
                @Override public void onInputDeviceRemoved(int id) { nativeOnMediaChanged(); }
                @Override public void onInputDeviceChanged(int id) { nativeOnMediaChanged(); }
            }, ui);
        android.content.BroadcastReceiver dock = new android.content.BroadcastReceiver() {
            @Override public void onReceive(Context context, Intent intent) {
                docked = isDocked(intent);
                nativeOnMediaChanged();
            }
        };
        android.content.IntentFilter filter = new android.content.IntentFilter(Intent.ACTION_DOCK_EVENT);
        Intent sticky = Build.VERSION.SDK_INT >= 33 ? activity.getApplicationContext().registerReceiver(dock, filter, Context.RECEIVER_EXPORTED)
                                                    : activity.getApplicationContext().registerReceiver(dock, filter);
        docked = isDocked(sticky);
        nativeOnMediaChanged();
    }

    private static boolean isDocked(Intent intent) {
        return intent != null && intent.getIntExtra(Intent.EXTRA_DOCK_STATE, Intent.EXTRA_DOCK_STATE_UNDOCKED) != Intent.EXTRA_DOCK_STATE_UNDOCKED;
    }

    // The window insets by kind, in window pixels: left, top, right, bottom of the status bars,
    // the navigation bars, the keyboard, the display cutout and the gesture edges (0-19); where
    // the keyboard's slide started and where it goes (20-27); whether it is up or coming up (28),
    // and whether it is sliding (29). While it slides (Android 11+) its insets are where it is
    // that frame, not where the window already says it ends up.
    private static int[] imeNow;  // mid-slide (l, t, r, b), null when it isn't sliding
    private static final int[] imeSource = new int[4], imeTarget = new int[4];

    @android.annotation.TargetApi(29)
    private static void put(int[] values, int at, android.graphics.Insets i) {
        values[at] = i.left; values[at + 1] = i.top; values[at + 2] = i.right; values[at + 3] = i.bottom;
    }

    static void sendInsets(WindowInsets insets) {
        if (insets == null) return;
        int[] values = new int[46];
        if (Build.VERSION.SDK_INT >= 30) {
            int[] kinds = {WindowInsets.Type.statusBars(), WindowInsets.Type.navigationBars(), WindowInsets.Type.ime(),
                           WindowInsets.Type.displayCutout(), WindowInsets.Type.systemGestures()};
            for (int k = 0; k < kinds.length; k++) put(values, k * 4, insets.getInsets(kinds[k]));
            // Then (after the keyboard's slide, 20..29): the caption bar, the mandatory gesture
            // edges, the tappable elements and the waterfall.
            put(values, 30, insets.getInsets(WindowInsets.Type.captionBar()));
            put(values, 34, insets.getInsets(WindowInsets.Type.mandatorySystemGestures()));
            put(values, 38, insets.getInsets(WindowInsets.Type.tappableElement()));
            android.view.DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null) put(values, 42, cutout.getWaterfallInsets());
            if (imeNow != null) System.arraycopy(imeNow, 0, values, 8, 4);
            System.arraycopy(imeSource, 0, values, 20, 4);
            System.arraycopy(imeTarget, 0, values, 24, 4);
            WindowInsets now = view != null ? view.getRootWindowInsets() : null;
            values[28] = (now != null ? now : insets).isVisible(WindowInsets.Type.ime()) ? 1 : 0;
            values[29] = imeNow != null ? 1 : 0;
        } else {  // older: the status bar on top, the navigation bar on the other sides
            values[1] = insets.getSystemWindowInsetTop();
            values[4] = insets.getSystemWindowInsetLeft();
            values[6] = insets.getSystemWindowInsetRight();
            values[7] = insets.getSystemWindowInsetBottom();
        }
        nativeOnInsets(values);
    }

    // The keyboard's slide (Android 11+), frame by frame, as Compose's WindowInsets.ime follows it.
    @android.annotation.TargetApi(30)
    private static final class ImeAnimation extends WindowInsetsAnimation.Callback {
        ImeAnimation() {
            super(DISPATCH_MODE_CONTINUE_ON_SUBTREE);
        }

        private static boolean isIme(WindowInsetsAnimation animation) {
            return (animation.getTypeMask() & WindowInsets.Type.ime()) != 0;
        }

        private static void copy(WindowInsets insets, int[] into) {
            android.graphics.Insets i = insets != null ? insets.getInsets(WindowInsets.Type.ime()) : android.graphics.Insets.NONE;
            into[0] = i.left; into[1] = i.top; into[2] = i.right; into[3] = i.bottom;
        }

        // Before the window takes its new insets: the keyboard stays where it is until it moves.
        @Override
        public void onPrepare(WindowInsetsAnimation animation) {
            if (!isIme(animation)) return;
            copy(view.getRootWindowInsets(), imeSource);
            imeNow = imeSource.clone();
        }

        @Override
        public WindowInsetsAnimation.Bounds onStart(WindowInsetsAnimation animation, WindowInsetsAnimation.Bounds bounds) {
            if (isIme(animation)) {
                copy(view.getRootWindowInsets(), imeTarget);  // the new insets: where it goes
                sendInsets(view.getRootWindowInsets());
            }
            return bounds;
        }

        @Override
        public WindowInsets onProgress(WindowInsets insets, java.util.List<WindowInsetsAnimation> running) {
            for (WindowInsetsAnimation animation : running)
                if (isIme(animation) && imeNow != null) {
                    copy(insets, imeNow);
                    sendInsets(insets);
                    break;
                }
            return insets;
        }

        @Override
        public void onEnd(WindowInsetsAnimation animation) {
            if (!isIme(animation) || imeNow == null) return;
            imeNow = null;
            WindowInsets now = view.getRootWindowInsets();
            copy(now, imeSource);
            copy(now, imeTarget);
            sendInsets(now);
        }
    }

    // ---- The keyboard moved by hand (imeNestedScroll, Android 11+) ---------------------------
    // Asked for on the first move; moves before the control comes are kept, as is a let go.

    private static Object imeControl;  // WindowInsetsAnimationController, once it is ready
    private static boolean imeControlAsked;
    private static float imeControlPending;  // px moved before it was ready
    private static Float imeEndPending;      // the velocity of a let go before it was ready

    static void imeControlBy(float px) {  // SDL thread
        ui.post(() -> {
            if (Build.VERSION.SDK_INT < 30 || view == null) return;
            if (imeControl != null) moveIme(px);
            else {
                imeControlPending += px;
                askImeControl();
            }
        });
    }

    static void imeControlEnd(float velocity) {  // SDL thread
        ui.post(() -> {
            if (Build.VERSION.SDK_INT < 30) return;
            if (imeControl != null) finishIme(velocity);
            else if (imeControlAsked) imeEndPending = velocity;
        });
    }

    @android.annotation.TargetApi(30)
    private static void askImeControl() {
        if (imeControlAsked) return;
        imeControlAsked = true;
        android.view.WindowInsetsController controller = view.getWindowInsetsController();
        if (controller == null) {
            imeControlAsked = false;
            return;
        }
        controller.controlWindowInsetsAnimation(WindowInsets.Type.ime(), -1, new android.view.animation.LinearInterpolator(),
            new android.os.CancellationSignal(), new android.view.WindowInsetsAnimationControlListener() {
                @Override
                public void onReady(android.view.WindowInsetsAnimationController c, int types) {
                    imeControl = c;
                    moveIme(imeControlPending);
                    imeControlPending = 0;
                    if (imeEndPending != null) {
                        finishIme(imeEndPending);
                        imeEndPending = null;
                    }
                }

                @Override
                public void onFinished(android.view.WindowInsetsAnimationController c) { reset(); }

                @Override
                public void onCancelled(android.view.WindowInsetsAnimationController c) { reset(); }

                private void reset() {
                    imeControl = null;
                    imeControlAsked = false;
                    imeControlPending = 0;
                    imeEndPending = null;
                }
            });
    }

    @android.annotation.TargetApi(30)
    private static void moveIme(float px) {
        android.view.WindowInsetsAnimationController c = (android.view.WindowInsetsAnimationController) imeControl;
        if (c == null || c.isFinished() || c.isCancelled()) return;
        int hidden = c.getHiddenStateInsets().bottom, shown = c.getShownStateInsets().bottom;
        int next = Math.max(hidden, Math.min(shown, c.getCurrentInsets().bottom + Math.round(px)));
        c.setInsetsAndAlpha(android.graphics.Insets.of(0, 0, 0, next), 1f, shown > hidden ? (next - hidden) / (float) (shown - hidden) : 1f);
    }

    // Let go: up or down by the fling, else whichever it is nearer.
    @android.annotation.TargetApi(30)
    private static void finishIme(float velocity) {
        android.view.WindowInsetsAnimationController c = (android.view.WindowInsetsAnimationController) imeControl;
        if (c == null || c.isFinished() || c.isCancelled()) return;
        int hidden = c.getHiddenStateInsets().bottom, shown = c.getShownStateInsets().bottom, now = c.getCurrentInsets().bottom;
        float fast = 600 * view.getResources().getDisplayMetrics().density;  // px/s
        boolean show = velocity > fast || (velocity >= -fast && now - hidden > (shown - hidden) / 2);
        c.finish(show);
    }

    // ---- Called by the app's activity ------------------------------------------------------

    // From SDLActivity.createSDLSurface: SDL's surface, but deaf to the keyboard. Told that one is
    // up, SDL takes it for its own and shows it again — its own input view taking the focus from
    // ours — whenever the window is resized, which a keyboard showing does: typed text went to SDL.
    // Kompoz gets the insets (keyboard included) from its own view.
    public static SDLSurface createSurface(Context context) {
        Surface s = new Surface(context);
        drawSurface = s;
        return s;
    }
    private static SurfaceView drawSurface;  // the one it draws on (setFrameRate)

    private static final class Surface extends SDLSurface {
        Surface(Context context) {
            super(context);
            if (Build.VERSION.SDK_INT >= 26) setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_YES);
        }

        @Override
        public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
            return insets;
        }

        // Autofill sees the Kompoz text fields as virtual views of the surface, and fills them.
        @Override
        public void onProvideAutofillVirtualStructure(ViewStructure structure, int flags) {
            if (Build.VERSION.SDK_INT >= 26) Autofill.provide(this, structure);
        }

        @Override
        public void autofill(SparseArray<AutofillValue> values) {
            if (Build.VERSION.SDK_INT >= 26) Autofill.fill(values);
        }

        // TalkBack's touch exploration: the finger hovers over the Kompoz node under it.
        @Override
        public boolean dispatchHoverEvent(android.view.MotionEvent event) {
            if (semantics != null && semantics.hover(event)) return true;
            return super.dispatchHoverEvent(event);
        }
    }

    // From Activity.onNewIntent: a link sent to the running app goes to its NavHost (deep links).
    public static void onNewIntent(Intent intent) {
        deliverLink(intent);
    }

    // Saved instance state: Kompoz's rememberSaveable values, written out by native code as the
    // app goes to the background, kept in the Bundle while Android may end the process, and
    // given back to native code when the activity is created again from it. The activity calls
    // these from onCreate (before super.onCreate), onPause (before super.onPause) and
    // onSaveInstanceState (after super).
    private static final String STATE_KEY = "dev.kompoz.state";
    private static final Object stateLock = new Object();
    private static byte[] savedState, restoredState;
    private static boolean saving;  // paused, the native side's state not in yet

    public static void onCreate(android.os.Bundle saved) {
        restoredState = saved != null ? saved.getByteArray(STATE_KEY) : null;
    }

    public static void onPause() {
        synchronized (stateLock) {
            saving = true;
        }
    }

    public static void onSaveInstanceState(android.os.Bundle out) {
        synchronized (stateLock) {
            long until = SystemClock.uptimeMillis() + 500;  // the native side saves as it pauses
            while (saving) {
                long left = until - SystemClock.uptimeMillis();
                if (left <= 0) break;
                try {
                    stateLock.wait(left);
                } catch (InterruptedException e) {
                    break;
                }
            }
            if (savedState != null) out.putByteArray(STATE_KEY, savedState);
        }
    }

    // From native code (the SDL thread).
    static void stateSaved(byte[] state) {
        synchronized (stateLock) {
            savedState = state;
            saving = false;
            stateLock.notifyAll();
        }
    }

    static byte[] takeRestoredState() {
        byte[] state = restoredState;
        restoredState = null;
        return state;
    }

    private static void deliverLink(Intent intent) {
        String link = intent != null && Intent.ACTION_VIEW.equals(intent.getAction()) ? intent.getDataString() : null;
        if (link != null) nativeOnDeepLink(link);
    }

    // `session`: this showing's number, which the edits made from here on carry; the native side
    // drops those still coming from the field before (the keyboard reports them a moment late).
    // `showKeyboard` false: the view takes the typing, the keyboard isn't asked to come up.
    static void show(byte[] text, int selStart, int selEnd, int x, int y, int w, int h, int inputType, int imeAction, int session,
                     boolean showKeyboard) {
        final String s = new String(text, StandardCharsets.UTF_8);
        final int start = charIndex(text, selStart), end = charIndex(text, selEnd);
        ui.post(() -> {
            if (!ensureView()) return;
            // At the field, so the IME places its popups there; touches still reach SDL's surface.
            FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(1, 1);
            params.leftMargin = x;
            params.topMargin = y + h;
            view.setLayoutParams(params);
            view.setOptions(inputType, imeAction);
            view.setState(s, start, end);
            // Only now: restarting the keyboard (above) closes its connection, which reports the
            // text it had, the field before's, as it goes.
            KompozInput.session = session;
            view.setFocusable(true);
            view.setFocusableInTouchMode(true);
            view.requestFocus();
            if (showKeyboard) imm(view.getContext()).showSoftInput(view, 0);
        });
    }

    private static boolean ensureView() {
        if (view != null && view.getParent() != null) return true;
        Activity activity = SDLActivity.getContext();
        if (activity == null) return false;
        if (view == null) {
            view = new InputView(activity);
            view.setOnApplyWindowInsetsListener((v, insets) -> {
                sendInsets(insets);
                return insets;
            });
            if (Build.VERSION.SDK_INT >= 30) view.setWindowInsetsAnimationCallback(new ImeAnimation());
        }
        activity.addContentView(view, new FrameLayout.LayoutParams(1, 1));
        view.requestApplyInsets();
        attachAccessibility(activity);
        // When a field gives focus back to SDL's surface after a hardware key (Enter), Android
        // would draw its keyboard-navigation highlight over the whole surface.
        if (Build.VERSION.SDK_INT >= 26) disableFocusHighlight(activity.getWindow().getDecorView());
        return true;
    }

    private static void disableFocusHighlight(View v) {
        if (Build.VERSION.SDK_INT < 26) return;
        v.setDefaultFocusHighlightEnabled(false);
        if (v instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) v;
            for (int i = 0; i < group.getChildCount(); i++) disableFocusHighlight(group.getChildAt(i));
        }
    }

    // The system font size setting at startup; changes arrive through onConfigurationChanged
    // (the manifest handles fontScale without recreating the activity).
    static float fontScale() {
        Activity activity = SDLActivity.getContext();
        return activity != null ? activity.getResources().getConfiguration().fontScale : 1f;
    }

    // Dark icons over light system bars (lightStatus / lightNavigation) or light ones over dark.
    static void setSystemBarsAppearance(boolean lightStatus, boolean lightNavigation) {
        ui.post(() -> {
            Activity activity = SDLActivity.getContext();
            if (activity == null) return;
            android.view.Window window = activity.getWindow();
            if (Build.VERSION.SDK_INT >= 30) {
                android.view.WindowInsetsController controller = window.getInsetsController();
                if (controller == null) return;
                int status = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS;
                int navigation = android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                controller.setSystemBarsAppearance((lightStatus ? status : 0) | (lightNavigation ? navigation : 0),
                                                   status | navigation);
            } else if (Build.VERSION.SDK_INT >= 26) {
                View decor = window.getDecorView();
                int flags = decor.getSystemUiVisibility() & ~(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                                                              | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
                if (lightStatus) flags |= View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                if (lightNavigation) flags |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                decor.setSystemUiVisibility(flags);
            }
        });
    }

    // The wallpaper palette of Android 12+ (Material You): accent1, accent2, accent3, neutral1 and
    // neutral2, each in 13 tones (0 = white ... 1000 = black) as ARGB; null before Android 12.
    static int[] dynamicPalette() {
        Activity activity = SDLActivity.getContext();
        if (activity == null || Build.VERSION.SDK_INT < 31) return null;
        final int[] ids = {
            android.R.color.system_accent1_0, android.R.color.system_accent1_10, android.R.color.system_accent1_50,
            android.R.color.system_accent1_100, android.R.color.system_accent1_200, android.R.color.system_accent1_300,
            android.R.color.system_accent1_400, android.R.color.system_accent1_500, android.R.color.system_accent1_600,
            android.R.color.system_accent1_700, android.R.color.system_accent1_800, android.R.color.system_accent1_900,
            android.R.color.system_accent1_1000,
            android.R.color.system_accent2_0, android.R.color.system_accent2_10, android.R.color.system_accent2_50,
            android.R.color.system_accent2_100, android.R.color.system_accent2_200, android.R.color.system_accent2_300,
            android.R.color.system_accent2_400, android.R.color.system_accent2_500, android.R.color.system_accent2_600,
            android.R.color.system_accent2_700, android.R.color.system_accent2_800, android.R.color.system_accent2_900,
            android.R.color.system_accent2_1000,
            android.R.color.system_accent3_0, android.R.color.system_accent3_10, android.R.color.system_accent3_50,
            android.R.color.system_accent3_100, android.R.color.system_accent3_200, android.R.color.system_accent3_300,
            android.R.color.system_accent3_400, android.R.color.system_accent3_500, android.R.color.system_accent3_600,
            android.R.color.system_accent3_700, android.R.color.system_accent3_800, android.R.color.system_accent3_900,
            android.R.color.system_accent3_1000,
            android.R.color.system_neutral1_0, android.R.color.system_neutral1_10, android.R.color.system_neutral1_50,
            android.R.color.system_neutral1_100, android.R.color.system_neutral1_200, android.R.color.system_neutral1_300,
            android.R.color.system_neutral1_400, android.R.color.system_neutral1_500, android.R.color.system_neutral1_600,
            android.R.color.system_neutral1_700, android.R.color.system_neutral1_800, android.R.color.system_neutral1_900,
            android.R.color.system_neutral1_1000,
            android.R.color.system_neutral2_0, android.R.color.system_neutral2_10, android.R.color.system_neutral2_50,
            android.R.color.system_neutral2_100, android.R.color.system_neutral2_200, android.R.color.system_neutral2_300,
            android.R.color.system_neutral2_400, android.R.color.system_neutral2_500, android.R.color.system_neutral2_600,
            android.R.color.system_neutral2_700, android.R.color.system_neutral2_800, android.R.color.system_neutral2_900,
            android.R.color.system_neutral2_1000,
        };
        int[] colors = new int[ids.length];
        for (int i = 0; i < ids.length; i++) colors[i] = activity.getColor(ids[i]);
        return colors;
    }

    // Draws text (an emoji) with Android's own text stack, which renders the system's color
    // emoji font (COLRv1, beyond FreeType). Returns {width, height, left, top, advance * 64}
    // followed by the ARGB pixels, or null.
    static int[] renderGlyph(byte[] utf8, int px) {
        String text = new String(utf8, StandardCharsets.UTF_8);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setTextSize(px);
        Rect bounds = new Rect();
        paint.getTextBounds(text, 0, text.length(), bounds);
        if (bounds.isEmpty()) return null;
        int w = bounds.width(), h = bounds.height();
        Bitmap bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        new Canvas(bitmap).drawText(text, -bounds.left, -bounds.top, paint);
        int[] out = new int[5 + w * h];
        out[0] = w;
        out[1] = h;
        out[2] = bounds.left;
        out[3] = -bounds.top;
        out[4] = Math.round(paint.measureText(text) * 64);
        bitmap.getPixels(out, 5, w, 0, 0, w, h);
        bitmap.recycle();
        return out;
    }

    // ---- Images (AsyncImage), called from native worker threads ----------------------------

    // An image file's size ({width, height}, 0 if it isn't one); and its bytes decoded by
    // BitmapFactory (JPEG, PNG, WebP, GIF, HEIF...) at 1/sampleSize: {width, height} then the
    // ARGB pixels, or null.
    static int[] imageBounds(byte[] data) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(data, 0, data.length, bounds);
        return new int[] {bounds.outWidth, bounds.outHeight};
    }

    static int[] decodeImage(byte[] data, int sampleSize) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sampleSize;  // a power of two, from imageSampleSize
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap bitmap = BitmapFactory.decodeByteArray(data, 0, data.length, options);
        if (bitmap == null) return null;
        int w = bitmap.getWidth(), h = bitmap.getHeight();
        int[] out = new int[2 + w * h];
        out[0] = w;
        out[1] = h;
        bitmap.getPixels(out, 2, w, 0, 0, w, h);
        bitmap.recycle();
        return out;
    }

    // The body of an http(s) GET (it needs the INTERNET permission), or null.
    static byte[] httpGet(String url) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(20000);
            connection.setInstanceFollowRedirects(true);
            if (connection.getResponseCode() / 100 != 2) return null;
            try (InputStream in = connection.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buffer = new byte[16384];
                for (int n; (n = in.read(buffer)) > 0;) out.write(buffer, 0, n);
                return out.toByteArray();
            }
        } catch (Exception e) {  // plain http:// needs android:usesCleartextTraffic="true" (Android 9+)
            android.util.Log.w("Kompoz", "can't load " + url + ": " + e);
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    static void update(byte[] text, int selStart, int selEnd) {
        final String s = new String(text, StandardCharsets.UTF_8);
        final int start = charIndex(text, selStart), end = charIndex(text, selEnd);
        ui.post(() -> {
            if (view != null && view.getParent() != null) view.setState(s, start, end);
        });
    }

    static void hide() {
        ui.post(() -> {
            if (view == null || view.getParent() == null) return;
            imm(view.getContext()).hideSoftInputFromWindow(view.getWindowToken(), 0);
            view.clearFocus();
            view.setFocusable(false);  // give focus back to SDL's surface (hardware keys)
        });
    }

    // HapticFeedback: Kompoz's HapticFeedbackType (its order) as the system's constant, older
    // releases getting the nearest one they have (as Compose does). The view's own setting and the
    // user's touch feedback setting decide whether it is felt.
    static void performHapticFeedback(int type) {
        ui.post(() -> {
            if (view == null || view.getParent() == null) return;
            view.performHapticFeedback(hapticConstant(type));
        });
    }

    private static int hapticConstant(int type) {
        int sdk = Build.VERSION.SDK_INT;
        switch (type) {
            case 1: return sdk >= 27 ? HapticFeedbackConstants.TEXT_HANDLE_MOVE : HapticFeedbackConstants.CLOCK_TICK;
            case 2: return sdk >= 30 ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.LONG_PRESS;
            case 3: return sdk >= 30 ? HapticFeedbackConstants.REJECT : HapticFeedbackConstants.LONG_PRESS;
            case 4: return sdk >= 23 ? HapticFeedbackConstants.CONTEXT_CLICK : HapticFeedbackConstants.LONG_PRESS;
            case 5: return sdk >= 30 ? HapticFeedbackConstants.GESTURE_END : HapticFeedbackConstants.CLOCK_TICK;
            case 6: return sdk >= 34 ? HapticFeedbackConstants.GESTURE_THRESHOLD_ACTIVATE : HapticFeedbackConstants.LONG_PRESS;
            case 7: return HapticFeedbackConstants.KEYBOARD_TAP;
            case 8: return sdk >= 34 ? HapticFeedbackConstants.SEGMENT_TICK : HapticFeedbackConstants.CLOCK_TICK;
            case 9: return sdk >= 34 ? HapticFeedbackConstants.SEGMENT_FREQUENT_TICK : HapticFeedbackConstants.CLOCK_TICK;
            case 10: return sdk >= 34 ? HapticFeedbackConstants.TOGGLE_ON : HapticFeedbackConstants.CONTEXT_CLICK;
            case 11: return sdk >= 34 ? HapticFeedbackConstants.TOGGLE_OFF : HapticFeedbackConstants.CONTEXT_CLICK;
            case 12: return HapticFeedbackConstants.VIRTUAL_KEY;
            default: return HapticFeedbackConstants.LONG_PRESS;
        }
    }

    // ---- Accessibility -------------------------------------------------------------------------

    // TalkBack and UI Automator see the Kompoz semantics tree as virtual views of SDL's surface.
    private static View surface;
    private static SemanticsProvider semantics;  // also what autofill reads
    private static long lastContentChange;

    private static void attachAccessibility(Activity activity) {
        if (surface != null) return;
        View content = SDLActivity.getContentView();
        if (!(content instanceof ViewGroup)) return;
        ViewGroup group = (ViewGroup) content;
        for (int i = 0; i < group.getChildCount(); i++)
            if (group.getChildAt(i) instanceof SurfaceView) surface = group.getChildAt(i);
        if (surface == null) return;
        final SemanticsProvider provider = new SemanticsProvider(surface);
        semantics = provider;
        surface.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            @Override
            public AccessibilityNodeProvider getAccessibilityNodeProvider(View host) {
                return provider;
            }
        });
        surface.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        AccessibilityManager am = (AccessibilityManager) activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
        if (am != null) {
            nativeSetAccessibilityEnabled(am.isEnabled());
            am.addAccessibilityStateChangeListener(KompozInput::nativeSetAccessibilityEnabled);
        }
    }

    // Called from native code when a new semantics tree was published, with the live region
    // nodes whose text changed: each of those is announced.
    static void semanticsChanged(int[] spoken) {
        ui.post(() -> {
            if (surface == null) return;
            AccessibilityManager am = (AccessibilityManager) surface.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
            if (am == null || !am.isEnabled()) return;
            for (int id : spoken) {
                AccessibilityEvent event = AccessibilityEvent.obtain(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
                event.setPackageName(surface.getContext().getPackageName());
                event.setSource(surface, id);
                event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_TEXT);
                if (surface.getParent() != null) surface.getParent().requestSendAccessibilityEvent(surface, event);
            }
            long now = android.os.SystemClock.uptimeMillis();
            if (now - lastContentChange < 100) return;
            lastContentChange = now;
            surface.sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
        });
    }

    static final class SemanticsProvider extends AccessibilityNodeProvider {
        // Per node: id, parent, flags, role, left, top, right, bottom (window pixels), live region.
        static final int STRIDE = 9;
        static final int CLICKABLE = 1, ENABLED = 2, FOCUSED = 4, EDITABLE = 8, SCROLLABLE = 16, HEADING = 32,
                CHECKABLE = 64, CHECKED = 128, SETTABLE = 256, PASSWORD = 512, SELECTABLE_GROUP = 1024;
        // A node's custom actions go by these ids (clear of the standard ones), in their order.
        static final int CUSTOM_ACTION = 0x01ff0000, MAX_CUSTOM_ACTIONS = 64;
        static final String[] CLASSES = {"android.view.View", "android.widget.Button", "android.widget.CheckBox",
                "android.widget.Switch", "android.widget.RadioButton", "android.widget.TabWidget",
                "android.widget.ImageView", "android.widget.SeekBar"};
        final View host;
        int[] data = new int[0];
        String[] labels = new String[0], values = new String[0], states = new String[0], actions = new String[0];
        String[] contentTypes = new String[0];  // per node: its autofill hints, joined by \u001f
        float[] ranges = new float[0];  // per node: has one (0/1), current, start, end
        int accessibilityFocused = View.NO_ID, hovered = View.NO_ID;  // TalkBack's focus, the node under its finger

        SemanticsProvider(View host) {
            this.host = host;
        }

        // A hover from touch exploration: the node under the finger is told (TalkBack then puts its
        // focus there), as ExploreByTouchHelper does. Other hovers (a mouse) are the surface's.
        boolean hover(android.view.MotionEvent e) {
            AccessibilityManager am = (AccessibilityManager) host.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
            if (am == null || !am.isEnabled() || !am.isTouchExplorationEnabled()) return false;
            switch (e.getActionMasked()) {
                case android.view.MotionEvent.ACTION_HOVER_ENTER:
                case android.view.MotionEvent.ACTION_HOVER_MOVE:
                    refresh();
                    setHovered(nodeAt(e.getX(), e.getY()));
                    return true;
                case android.view.MotionEvent.ACTION_HOVER_EXIT:
                    setHovered(View.NO_ID);
                    return true;
                default:
                    return false;
            }
        }

        // The smallest node under a point that has something to say or do.
        int nodeAt(float x, float y) {
            int best = View.NO_ID;
            long bestArea = Long.MAX_VALUE;
            for (int i = 0; i * STRIDE < data.length; i++) {
                int b = i * STRIDE, flags = data[b + 2];
                boolean speaks = (flags & (CLICKABLE | EDITABLE | SCROLLABLE | CHECKABLE | HEADING)) != 0 || !labels[i].isEmpty() || !states[i].isEmpty();
                if (!speaks || x < data[b + 4] || x >= data[b + 6] || y < data[b + 5] || y >= data[b + 7]) continue;
                long area = (long) (data[b + 6] - data[b + 4]) * (data[b + 7] - data[b + 5]);
                if (area < bestArea) {
                    bestArea = area;
                    best = data[b];
                }
            }
            return best;
        }

        void setHovered(int id) {
            if (id == hovered) return;
            int old = hovered;
            hovered = id;
            if (id != View.NO_ID) send(id, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
            if (old != View.NO_ID) send(old, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
        }

        void send(int id, int type) {
            AccessibilityManager am = (AccessibilityManager) host.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
            if (am == null || !am.isEnabled() || host.getParent() == null) return;
            AccessibilityEvent event = AccessibilityEvent.obtain(type);
            event.setPackageName(host.getContext().getPackageName());
            event.setSource(host, id);
            int i = indexOf(id);
            if (i >= 0) {
                event.setClassName(className(i));
                String text = (data[i * STRIDE + 2] & EDITABLE) != 0 ? values[i] : labels[i];
                if (!text.isEmpty()) event.getText().add(text);
            }
            host.getParent().requestSendAccessibilityEvent(host, event);
        }

        String className(int i) {
            int flags = data[i * STRIDE + 2], role = data[i * STRIDE + 3];
            return (flags & EDITABLE) != 0 ? "android.widget.EditText" : role >= 0 && role < CLASSES.length ? CLASSES[role] : CLASSES[0];
        }

        @Override
        public AccessibilityNodeInfo findFocus(int focus) {
            if (focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY && accessibilityFocused != View.NO_ID)
                return createAccessibilityNodeInfo(accessibilityFocused);
            return null;
        }

        void refresh() {
            Object[] snapshot = nativeSemantics();
            if (snapshot == null || ((int[]) snapshot[0]).length == 0) {
                nativeSetAccessibilityEnabled(true);  // first query (UI Automator): wait for one
                for (int i = 0; i < 30 && (snapshot == null || ((int[]) snapshot[0]).length == 0); i++) {
                    try { Thread.sleep(10); } catch (InterruptedException e) { break; }
                    snapshot = nativeSemantics();
                }
            }
            if (snapshot == null) return;
            data = (int[]) snapshot[0];
            labels = (String[]) snapshot[1];
            values = (String[]) snapshot[2];
            states = (String[]) snapshot[3];
            actions = (String[]) snapshot[4];
            ranges = (float[]) snapshot[5];
            contentTypes = snapshot.length > 6 ? (String[]) snapshot[6] : new String[0];
        }

        int indexOf(int id) {
            for (int i = 0; i * STRIDE < data.length; i++)
                if (data[i * STRIDE] == id) return i;
            return -1;
        }

        @Override
        public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
            refresh();
            if (virtualViewId == View.NO_ID) {
                AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(host);
                host.onInitializeAccessibilityNodeInfo(info);
                for (int i = 0; i * STRIDE < data.length; i++)
                    if (data[i * STRIDE + 1] == -1) info.addChild(host, data[i * STRIDE]);
                return info;
            }
            int i = indexOf(virtualViewId);
            if (i < 0) return null;
            int b = i * STRIDE, flags = data[b + 2], role = data[b + 3];
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(host, virtualViewId);
            info.setPackageName(host.getContext().getPackageName());
            info.setClassName(className(i));
            // TalkBack's focus: where it is, and that it can come here (or go).
            info.setAccessibilityFocused(virtualViewId == accessibilityFocused);
            info.addAction(virtualViewId == accessibilityFocused ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS
                                                                 : AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
            if (data[b + 1] == -1) info.setParent(host); else info.setParent(host, data[b + 1]);
            int[] location = new int[2];
            host.getLocationOnScreen(location);
            Rect r = new Rect(data[b + 4], data[b + 5], data[b + 6], data[b + 7]);
            r.offset(location[0], location[1]);
            info.setBoundsInScreen(r);
            if ((flags & EDITABLE) != 0) {
                info.setEditable(true);
                info.setText(values[i]);
                if (!labels[i].isEmpty()) info.setHintText(labels[i]);
                info.setFocused((flags & FOCUSED) != 0);
                info.setPassword((flags & PASSWORD) != 0);
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_TEXT);
            } else if (!labels[i].isEmpty()) {
                info.setText(labels[i]);
            }
            if (!states[i].isEmpty() && Build.VERSION.SDK_INT >= 30) info.setStateDescription(states[i]);
            if ((flags & CHECKABLE) != 0) {
                info.setCheckable(true);
                info.setChecked((flags & CHECKED) != 0);
            }
            if (Build.VERSION.SDK_INT >= 28) info.setHeading((flags & HEADING) != 0);
            collectionInfo(info, virtualViewId, flags, data[b + 1]);
            info.setEnabled((flags & ENABLED) != 0);
            info.setVisibleToUser(true);
            if ((flags & (CLICKABLE | EDITABLE)) != 0) {
                info.setClickable(true);
                info.setFocusable(true);
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK);
            }
            if ((flags & SCROLLABLE) != 0) {
                info.setScrollable(true);
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
            }
            if (ranges.length >= (i + 1) * 4 && ranges[i * 4] != 0) {  // a slider's or a progress bar's value
                info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,
                        ranges[i * 4 + 2], ranges[i * 4 + 3], ranges[i * 4 + 1]));
                if ((flags & SETTABLE) != 0 && Build.VERSION.SDK_INT >= 24)
                    info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS);
            }
            int live = data[b + 8];
            if (live != 0) info.setLiveRegion(live == 1 ? View.ACCESSIBILITY_LIVE_REGION_POLITE : View.ACCESSIBILITY_LIVE_REGION_ASSERTIVE);
            if (i < actions.length && !actions[i].isEmpty()) {
                String[] custom = actions[i].split("\u001f", -1);
                for (int k = 0; k < custom.length && k < MAX_CUSTOM_ACTIONS; k++)
                    info.addAction(new AccessibilityNodeInfo.AccessibilityAction(CUSTOM_ACTION + k, custom[k]));
            }
            for (int j = 0; j * STRIDE < data.length; j++)
                if (data[j * STRIDE + 1] == virtualViewId) info.addChild(host, data[j * STRIDE]);
            return info;
        }

        // A choice among its siblings (Compose's selectableGroup): the selectables in it, one row or
        // one column as they are laid out, a single one chosen.
        boolean isChoice(int j) {
            int role = data[j * STRIDE + 3], flags = data[j * STRIDE + 2];
            return (flags & CHECKABLE) != 0 || role == 4 || role == 5;  // checkable, a radio button, a tab
        }

        java.util.List<Integer> choicesIn(int groupId) {
            java.util.List<Integer> items = new java.util.ArrayList<>();
            for (int j = 0; j * STRIDE < data.length; j++)
                if (data[j * STRIDE + 1] == groupId && isChoice(j)) items.add(j);
            return items;
        }

        boolean inOneRow(java.util.List<Integer> items) {
            for (int j : items)
                if (Math.abs(data[j * STRIDE + 5] - data[items.get(0) * STRIDE + 5]) > 4) return false;
            return items.size() > 1;
        }

        void collectionInfo(AccessibilityNodeInfo info, int id, int flags, int parent) {
            if ((flags & SELECTABLE_GROUP) != 0) {
                java.util.List<Integer> items = choicesIn(id);
                boolean row = inOneRow(items);
                info.setCollectionInfo(AccessibilityNodeInfo.CollectionInfo.obtain(row ? 1 : items.size(), row ? items.size() : 1, false,
                        AccessibilityNodeInfo.CollectionInfo.SELECTION_MODE_SINGLE));
            }
            int p = parent >= 0 ? indexOf(parent) : -1;
            if (p >= 0 && (data[p * STRIDE + 2] & SELECTABLE_GROUP) != 0) {
                java.util.List<Integer> items = choicesIn(parent);
                int k = items.indexOf(indexOf(id));
                if (k < 0) return;
                boolean row = inOneRow(items);
                info.setCollectionItemInfo(AccessibilityNodeInfo.CollectionItemInfo.obtain(row ? 0 : k, 1, row ? k : 0, 1, false,
                        (flags & CHECKED) != 0));
            }
        }

        @Override
        public boolean performAction(int virtualViewId, int action, Bundle arguments) {
            if (virtualViewId == View.NO_ID) return host.performAccessibilityAction(action, arguments);
            if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
                if (accessibilityFocused == virtualViewId) return false;
                if (accessibilityFocused != View.NO_ID) send(accessibilityFocused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                accessibilityFocused = virtualViewId;
                host.invalidate();
                send(virtualViewId, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);
                return true;
            }
            if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
                if (accessibilityFocused != virtualViewId) return false;
                accessibilityFocused = View.NO_ID;
                host.invalidate();
                send(virtualViewId, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                return true;
            }
            float argument = 0;
            int code = action == AccessibilityNodeInfo.ACTION_CLICK ? 0
                    : action == AccessibilityNodeInfo.ACTION_LONG_CLICK ? 1
                    : action == AccessibilityNodeInfo.ACTION_FOCUS ? 2
                    : action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? 3
                    : action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD ? 4 : -1;
            if (action == AccessibilityNodeInfo.ACTION_SET_TEXT) {
                if (indexOf(virtualViewId) < 0) return false;
                CharSequence text = arguments != null ? arguments.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE) : null;
                nativeSetText(virtualViewId, (text != null ? text.toString() : "").getBytes(StandardCharsets.UTF_8));
                return true;
            }
            if (Build.VERSION.SDK_INT >= 24 && action == android.R.id.accessibilityActionSetProgress && arguments != null) {
                code = 5;
                argument = arguments.getFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE);
            } else if (action >= CUSTOM_ACTION && action < CUSTOM_ACTION + MAX_CUSTOM_ACTIONS) {
                code = 6;
                argument = action - CUSTOM_ACTION;
            }
            if (code < 0 || indexOf(virtualViewId) < 0) return false;
            nativePerformAction(virtualViewId, code, argument);
            return true;
        }
    }

    // From native code: android.view.ViewConfiguration's long press and double tap timeouts (ms),
    // touch slop (px) and maximum fling velocity (px/s); null without the activity.
    static float[] viewConfiguration() {
        Activity activity = SDLActivity.getContext();
        if (activity == null) return null;
        android.view.ViewConfiguration c = android.view.ViewConfiguration.get(activity);
        return new float[] {android.view.ViewConfiguration.getLongPressTimeout(), android.view.ViewConfiguration.getDoubleTapTimeout(),
                            c.getScaledTouchSlop(), c.getScaledMaximumFlingVelocity()};
    }

    // From native code: the window's rects (left, top, right, bottom in pixels) that the
    // system's edge swipes are kept off (systemGestureExclusion; Android 10+).
    static void setSystemGestureExclusion(int[] rects) {
        if (Build.VERSION.SDK_INT < 29) return;
        ui.post(() -> {
            View content = SDLActivity.getContentView();
            if (content == null) return;
            java.util.List<Rect> list = new java.util.ArrayList<>();
            for (int i = 0; i + 3 < rects.length; i += 4) list.add(new Rect(rects[i], rects[i + 1], rects[i + 2], rects[i + 3]));
            content.setSystemGestureExclusionRects(list);
        });
    }

    // From native code: the refresh rate the app would like (preferredFrameRate): a rate (Hz; 0
    // none), else a category (2 normal: 60 Hz, 3 high: the screen's fastest; 0, 1: none) for the
    // surface it draws on (Android 11+).
    static void setFrameRate(float rate, int category) {
        if (Build.VERSION.SDK_INT < 30) return;
        ui.post(() -> {
            SurfaceView surface = drawSurface;
            if (surface == null || surface.getHolder().getSurface() == null || !surface.getHolder().getSurface().isValid()) return;
            float hz = rate;
            if (hz <= 0 && category == 2) hz = 60;
            if (hz <= 0 && category == 3 && surface.getDisplay() != null)
                for (android.view.Display.Mode mode : surface.getDisplay().getSupportedModes()) hz = Math.max(hz, mode.getRefreshRate());
            try {
                surface.getHolder().getSurface().setFrameRate(Math.max(0, hz), android.view.Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
            } catch (IllegalArgumentException | IllegalStateException ignored) {
            }
        });
    }

    // From native code: sensitive content shown (sensitiveContent): the window kept out of
    // screen shares and recordings (Android 15+).
    static void setContentSensitive(boolean sensitive) {
        if (Build.VERSION.SDK_INT < 35) return;
        ui.post(() -> {
            View content = SDLActivity.getContentView();
            if (content != null)
                content.setContentSensitivity(sensitive ? View.CONTENT_SENSITIVITY_SENSITIVE : View.CONTENT_SENSITIVITY_AUTO);
        });
    }

    // ---- Autofill (Android 8+) ---------------------------------------------------------------

    // From native code: a text field (semantics id; window pixels) got the focus or lost it, its
    // text changed, it came into the layout or went; the form was sent (commit) or dropped.
    static void autofillFocus(int id, int left, int top, int right, int bottom, boolean entered) {
        if (Build.VERSION.SDK_INT < 26) return;
        ui.post(() -> Autofill.focus(id, new Rect(left, top, right, bottom), entered));
    }

    static void autofillValue(int id, byte[] text) {
        if (Build.VERSION.SDK_INT < 26) return;
        String value = new String(text, StandardCharsets.UTF_8);
        ui.post(() -> Autofill.value(id, value));
    }

    static void autofillVisibility(int id, boolean visible) {
        if (Build.VERSION.SDK_INT < 27) return;
        ui.post(() -> Autofill.visibility(id, visible));
    }

    static void autofillDone(boolean commit) {
        if (Build.VERSION.SDK_INT < 26) return;
        ui.post(() -> Autofill.done(commit));
    }

    @android.annotation.TargetApi(26)
    private static final class Autofill {
        static AutofillManager manager() {
            return surface != null ? surface.getContext().getSystemService(AutofillManager.class) : null;
        }

        // The text fields of the semantics tree, as children of the surface.
        static void provide(View host, ViewStructure structure) {
            SemanticsProvider p = semantics;
            if (p == null) return;
            p.refresh();
            final int stride = SemanticsProvider.STRIDE;
            int[] data = p.data;
            java.util.List<Integer> fields = new java.util.ArrayList<>();
            for (int i = 0; i * stride < data.length; i++)
                if ((data[i * stride + 2] & SemanticsProvider.EDITABLE) != 0) fields.add(i);
            int index = structure.addChildCount(fields.size());
            String pkg = host.getContext().getPackageName();
            for (int i : fields) {
                int b = i * stride, id = data[b], flags = data[b + 2];
                boolean password = (flags & SemanticsProvider.PASSWORD) != 0;
                ViewStructure child = structure.newChild(index++);
                child.setAutofillId(structure.getAutofillId(), id);
                child.setId(id, pkg, null, null);
                child.setClassName("android.widget.EditText");
                child.setAutofillType(View.AUTOFILL_TYPE_TEXT);
                child.setAutofillValue(AutofillValue.forText(p.values[i]));
                String hints = i < p.contentTypes.length ? p.contentTypes[i] : "";
                if (!hints.isEmpty()) child.setAutofillHints(hints.split("\u001f"));
                if (!p.labels[i].isEmpty()) child.setHint(p.labels[i]);
                child.setInputType(password ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_CLASS_TEXT);
                child.setDataIsSensitive(password);
                child.setDimens(data[b + 4], data[b + 5], 0, 0, data[b + 6] - data[b + 4], data[b + 7] - data[b + 5]);
                child.setVisibility(View.VISIBLE);
                child.setEnabled((flags & SemanticsProvider.ENABLED) != 0);
                child.setFocusable(true);
                child.setFocused((flags & SemanticsProvider.FOCUSED) != 0);
            }
        }

        static void fill(SparseArray<AutofillValue> values) {
            for (int i = 0; i < values.size(); i++) {
                AutofillValue value = values.valueAt(i);
                if (value != null && value.isText())
                    nativeSetText(values.keyAt(i), value.getTextValue().toString().getBytes(StandardCharsets.UTF_8));
            }
        }

        static void focus(int id, Rect bounds, boolean entered) {
            AutofillManager m = manager();
            if (m == null) return;
            if (entered) {
                int[] at = new int[2];
                surface.getLocationOnScreen(at);
                bounds.offset(at[0], at[1]);
                m.notifyViewEntered(surface, id, bounds);
            } else {
                m.notifyViewExited(surface, id);
            }
        }

        static void value(int id, String text) {
            AutofillManager m = manager();
            if (m != null) m.notifyValueChanged(surface, id, AutofillValue.forText(text));
        }

        @android.annotation.TargetApi(27)
        static void visibility(int id, boolean visible) {
            AutofillManager m = manager();
            if (m != null) m.notifyViewVisibilityChanged(surface, id, visible);
        }

        static void done(boolean commit) {
            AutofillManager m = manager();
            if (m == null) return;
            if (commit) m.commit();
            else m.cancel();
        }
    }

    // ---- Back (Android 13+) -----------------------------------------------------------------

    // The app's back callback is registered only while Kompoz takes back (a popup, the focus, a
    // BackHandler): otherwise the system goes back itself, leaving the app with its animation.
    // From Android 14 the gesture's steps come too (PredictiveBackHandler). Before 13 the back
    // key comes through SDL.
    private static Object backCallback;  // UI thread

    // From native code (the SDL thread).
    static void setBackEnabled(boolean enabled) {
        if (Build.VERSION.SDK_INT < 33) return;
        ui.post(() -> {
            Activity activity = SDLActivity.getContext();
            if (activity == null || enabled == (backCallback != null)) return;
            if (enabled) {
                backCallback = Back.register(activity);
            } else {
                Back.unregister(activity, backCallback);
                backCallback = null;
            }
        });
    }

    @android.annotation.TargetApi(33)
    private static final class Back {
        static Object register(Activity activity) {
            android.window.OnBackInvokedCallback callback;
            if (Build.VERSION.SDK_INT >= 34) callback = new Gesture(activity);
            else callback = () -> nativeOnBack(3, 0, 0, 0, 0);
            activity.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
            return callback;
        }

        static void unregister(Activity activity, Object callback) {
            activity.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback((android.window.OnBackInvokedCallback) callback);
        }
    }

    // The gesture's steps, the touch in window pixels.
    @android.annotation.TargetApi(34)
    private static final class Gesture implements android.window.OnBackAnimationCallback {
        private final Activity activity;

        Gesture(Activity activity) {
            this.activity = activity;
        }

        @Override
        public void onBackStarted(android.window.BackEvent e) {
            send(0, e);
        }

        @Override
        public void onBackProgressed(android.window.BackEvent e) {
            send(1, e);
        }

        @Override
        public void onBackCancelled() {
            nativeOnBack(2, 0, 0, 0, 0);
        }

        @Override
        public void onBackInvoked() {
            nativeOnBack(3, 0, 0, 0, 0);
        }

        private void send(int phase, android.window.BackEvent e) {
            int[] at = new int[2];
            activity.getWindow().getDecorView().getLocationOnScreen(at);
            nativeOnBack(phase, e.getTouchX() - at[0], e.getTouchY() - at[1], e.getProgress(), e.getSwipeEdge());
        }
    }

    // ---- Implemented in platform/sdl3/android_ime.cpp ----------------------------------------

    static native void nativeOnEdit(int session, byte[] text, int selStart, int selEnd, int compStart, int compEnd);
    static native void nativeOnAction(int action);
    static native void nativeOnInsets(int[] values);  // see sendInsets
    static native void nativeOnFontScale(float scale);
    static native void nativeOnMediaChanged();  // see mediaInfo
    static native void nativeSetAccessibilityEnabled(boolean enabled);
    static native Object[] nativeSemantics();  // {int[] nodes, String[] labels, String[] values, String[] states}
    static native void nativePerformAction(int id, int action, float argument);
    static native void nativeSetText(int id, byte[] text);  // a text field's whole text (UTF-8): autofill, ACTION_SET_TEXT  // SemanticsAction's value, its argument
    static native void nativeOnDeepLink(String link);
    static native void nativeOnBack(int phase, float x, float y, float progress, int edge);  // started, progressed, cancelled, invoked

    // ---- Helpers ----------------------------------------------------------------------------

    static InputMethodManager imm(Context context) {
        return (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
    }

    static int charIndex(byte[] utf8, int byteOffset) {
        int clamped = Math.max(0, Math.min(byteOffset, utf8.length));
        return new String(utf8, 0, clamped, StandardCharsets.UTF_8).length();
    }

    static int byteIndex(String s, int charIndex) {
        if (charIndex < 0) return -1;
        return s.substring(0, Math.min(charIndex, s.length())).getBytes(StandardCharsets.UTF_8).length;
    }

    /** Invisible view that owns the text and IME focus while a Kompoz text field is focused. */
    static final class InputView extends View {
        final Editable editable = Editable.Factory.getInstance().newEditable("");
        Connection connection;
        int inputType = InputType.TYPE_CLASS_TEXT, imeAction = EditorInfo.IME_ACTION_DONE;

        InputView(Context context) {
            super(context);
            setFocusable(false);  // only while a text field is focused, see show()
        }

        /** Keyboard type and action key of the focused field (from its KeyboardOptions). */
        void setOptions(int type, int action) {
            if (type == inputType && action == imeAction) return;
            inputType = type;
            imeAction = action;
            imm(getContext()).restartInput(this);
        }

        /** The app's text replaces ours; the IME is restarted so it drops stale context. */
        void setState(String text, int selStart, int selEnd) {
            boolean changed = !editable.toString().equals(text)
                    || Selection.getSelectionStart(editable) != selStart
                    || Selection.getSelectionEnd(editable) != selEnd;
            if (!changed) return;
            editable.replace(0, editable.length(), text);
            BaseInputConnection.removeComposingSpans(editable);
            int length = editable.length();
            Selection.setSelection(editable, Math.min(selStart, length), Math.min(selEnd, length));
            imm(getContext()).restartInput(this);
        }

        @Override
        protected void onConfigurationChanged(Configuration config) {
            super.onConfigurationChanged(config);
            nativeOnFontScale(config.fontScale);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            info.inputType = inputType;
            info.imeOptions = imeAction
                    | EditorInfo.IME_FLAG_NO_FULLSCREEN
                    | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            info.initialSelStart = Selection.getSelectionStart(editable);
            info.initialSelEnd = Selection.getSelectionEnd(editable);
            if (Build.VERSION.SDK_INT >= 30) info.setInitialSurroundingText(editable);
            connection = new Connection(this);
            return connection;
        }

        // Hardware keys: text editing here, everything else (back without a keyboard on screen,
        // arrows, Tab, Escape) to SDL as key events, as if this view weren't focused.
        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            if (connection != null && connection.handleKey(event)) return true;
            return SDLActivity.handleKeyEvent(this, keyCode, event, null) || super.onKeyDown(keyCode, event);
        }

        @Override
        public boolean onKeyUp(int keyCode, KeyEvent event) {
            if (connection != null && connection.handleKey(event)) return true;
            return SDLActivity.handleKeyEvent(this, keyCode, event, null) || super.onKeyUp(keyCode, event);
        }
    }

    static final class Connection extends BaseInputConnection {
        private final InputView owner;
        private int batchDepth;
        private boolean dirty;
        private String lastReported;  // text + offsets, to skip no-op reports

        Connection(InputView owner) {
            super(owner, true);
            this.owner = owner;
        }

        @Override
        public Editable getEditable() {
            return owner.editable;
        }

        // Report once per batch, after the IME finished a group of operations.
        @Override
        public boolean beginBatchEdit() {
            batchDepth++;
            return true;
        }

        @Override
        public boolean endBatchEdit() {
            if (batchDepth > 0 && --batchDepth == 0) flush();
            return batchDepth > 0;
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            return changed(super.commitText(text, newCursorPosition));
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            return changed(super.setComposingText(text, newCursorPosition));
        }

        @Override
        public boolean setComposingRegion(int start, int end) {
            return changed(super.setComposingRegion(start, end));
        }

        @Override
        public boolean finishComposingText() {
            return changed(super.finishComposingText());
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            return changed(super.deleteSurroundingText(beforeLength, afterLength));
        }

        @Override
        public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
            return changed(super.deleteSurroundingTextInCodePoints(beforeLength, afterLength));
        }

        @Override
        public boolean setSelection(int start, int end) {
            return changed(super.setSelection(start, end));
        }

        // Gboard applies suggestions and autocorrections with this (API 34+).
        @Override
        public boolean replaceText(int start, int end, CharSequence text, int newCursorPosition,
                                   TextAttribute textAttribute) {
            return changed(super.replaceText(start, end, text, newCursorPosition, textAttribute));
        }

        @Override
        public boolean sendKeyEvent(KeyEvent event) {
            return handleKey(event) || super.sendKeyEvent(event);
        }

        @Override
        public boolean performEditorAction(int actionCode) {
            nativeOnAction(actionCode);
            return true;
        }

        boolean handleKey(KeyEvent event) {
            int code = event.getKeyCode();
            boolean handled = code == KeyEvent.KEYCODE_DEL || code == KeyEvent.KEYCODE_ENTER
                    || code == KeyEvent.KEYCODE_NUMPAD_ENTER;
            if (event.getAction() != KeyEvent.ACTION_DOWN) return handled;
            if (code == KeyEvent.KEYCODE_DEL) {
                deleteBackward();
                return true;
            }
            if (code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_NUMPAD_ENTER) {
                boolean multiLine = (owner.inputType & android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE) != 0;
                int action = owner.imeAction & EditorInfo.IME_MASK_ACTION;
                if (multiLine && action == EditorInfo.IME_ACTION_NONE)
                    commitText("\n", 1);  // a multi-line field without an action: a new line
                else
                    nativeOnAction(action);
                return true;
            }
            int unicode = event.getUnicodeChar();
            if ((unicode & KeyCharacterMap.COMBINING_ACCENT) != 0) return true;  // a dead key: not a character
            if (unicode != 0 && !Character.isISOControl(unicode)) {
                commitText(new String(Character.toChars(unicode)), 1);
                return true;
            }
            return false;
        }

        private void deleteBackward() {
            Editable e = getEditable();
            int start = Selection.getSelectionStart(e), end = Selection.getSelectionEnd(e);
            if (start < 0) return;
            if (start > end) { int t = start; start = end; end = t; }
            if (start == end) {
                if (start == 0) return;
                start -= Character.charCount(Character.codePointBefore(e, start));
            }
            e.delete(start, end);
            changed(true);
        }

        private boolean changed(boolean result) {
            dirty = true;
            if (batchDepth == 0) flush();
            return result;
        }

        private void flush() {
            if (!dirty) return;
            dirty = false;
            Editable e = getEditable();
            String s = e.toString();
            int selStart = Selection.getSelectionStart(e), selEnd = Selection.getSelectionEnd(e);
            int compStart = getComposingSpanStart(e), compEnd = getComposingSpanEnd(e);
            String key = selStart + "," + selEnd + "," + compStart + "," + compEnd + ":" + s;
            if (key.equals(lastReported)) return;
            lastReported = key;
            nativeOnEdit(session, s.getBytes(StandardCharsets.UTF_8), byteIndex(s, selStart), byteIndex(s, selEnd),
                    byteIndex(s, compStart), byteIndex(s, compEnd));
            // A plain View doesn't tell the IME about its selection the way EditText does.
            imm(owner.getContext()).updateSelection(owner, selStart, selEnd, compStart, compEnd);
        }
    }
}
