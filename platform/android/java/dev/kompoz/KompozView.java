package dev.kompoz;

import android.app.Activity;
import android.content.ClipDescription;
import android.content.Context;
import android.os.Build;
import android.util.SparseArray;
import java.nio.charset.StandardCharsets;
import android.view.Choreographer;
import android.view.DragAndDropPermissions;
import android.view.DragEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewStructure;
import android.view.WindowInsets;
import android.view.autofill.AutofillValue;

/**
 * The surface Kompoz draws into, and what it is told: the surface's coming and going, touches,
 * the mouse, keys, and the display's frames (Choreographer's, the vsync's time as the frame's, as
 * Compose takes them), all to native code on the UI thread. The insets come from KompozInput's
 * view; a text field's typing goes to it too.
 */
final class KompozView extends SurfaceView implements SurfaceHolder.Callback, Choreographer.FrameCallback {
    private boolean framePosted;
    private int[] ids = new int[10];
    private float[] xy = new float[20];

    KompozView(Context context) {
        super(context);
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
        if (Build.VERSION.SDK_INT >= 26) setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_YES);
        KompozInput.drawSurface = this;
        setOnDragListener((view, event) -> drag(event));
    }

    // ---- Drags from other apps (split screen, a window on a desktop) ------------------------

    private static DragAndDropPermissions dropPermissions;  // to read the last drop's content URIs

    // Over the view: begun (its MIME types told), moved, dropped (all it carries) or gone. Any
    // drag is taken part in: what it carries, the app's dragAndDropTargets and contentReceivers decide.
    private boolean drag(DragEvent event) {
        if (!live()) return false;
        switch (event.getAction()) {
        case DragEvent.ACTION_DRAG_STARTED:
            return true;
        case DragEvent.ACTION_DRAG_ENTERED: {
            ClipDescription description = event.getClipDescription();
            StringBuilder types = new StringBuilder();
            for (int i = 0; description != null && i < description.getMimeTypeCount(); ++i) types.append(description.getMimeType(i)).append('\n');
            nativeDrag(0, 0, 0, types.toString().getBytes(StandardCharsets.UTF_8));
            return true;
        }
        case DragEvent.ACTION_DRAG_LOCATION:
            nativeDrag(1, event.getX(), event.getY(), null);
            return true;
        case DragEvent.ACTION_DRAG_EXITED:
            nativeDrag(2, 0, 0, null);
            return true;
        case DragEvent.ACTION_DROP: {
            Activity activity = KompozActivity.current;
            if (dropPermissions != null) dropPermissions.release();
            dropPermissions = activity != null && event.getClipData() != null ? activity.requestDragAndDropPermissions(event) : null;
            if (event.getClipData() == null) return false;
            nativeDrop(event.getX(), event.getY(), KompozActivity.clipParts(event.getClipData()));
            return true;
        }
        default:
            return true;
        }
    }

    // ---- The surface ----------------------------------------------------------------------

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (live()) nativeSurface(holder.getSurface(), width, height);
    }

    // Native code lets go of the surface before this returns, as it must.
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (live()) nativeSurface(null, 0, 0);
    }

    // Whether its activity is the app's (KompozActivity.current): one that another has replaced in the
    // process, until it ends, tells native code nothing (its app is the other's now).
    private boolean live() {
        return getContext() == KompozActivity.current;
    }

    // ---- Frames ---------------------------------------------------------------------------

    // From native code: a frame at the display's next beat (one at a time).
    void requestFrame() {
        if (framePosted) return;
        framePosted = true;
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        framePosted = false;
        if (live()) nativeFrame(frameTimeNanos);
    }

    // ---- Input ----------------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        motion(event);
        return true;
    }

    // The mouse's moves without a button down, its wheel and buttons (a touchpad's too).
    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_HOVER_EXIT || action == MotionEvent.ACTION_SCROLL ||
            action == MotionEvent.ACTION_BUTTON_PRESS || action == MotionEvent.ACTION_BUTTON_RELEASE) {
            motion(event);
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    // Every pointer's id and position (window pixels), the event's time on System.nanoTime's
    // clock, the tool of the pointer it is about, the button it pressed or let go of, and the
    // wheel's turn.
    private void motion(MotionEvent event) {
        if (!live()) return;
        int count = event.getPointerCount();
        if (ids.length < count) {
            ids = new int[count];
            xy = new float[count * 2];
        }
        for (int i = 0; i < count; i++) {
            ids[i] = event.getPointerId(i);
            xy[i * 2] = event.getX(i);
            xy[i * 2 + 1] = event.getY(i);
        }
        int index = event.getActionIndex();
        long time = Build.VERSION.SDK_INT >= 34 ? event.getEventTimeNanos() : event.getEventTime() * 1_000_000L;
        boolean scroll = event.getActionMasked() == MotionEvent.ACTION_SCROLL;
        nativeMotion(event.getActionMasked(), index, count, ids, xy, time, event.getToolType(index), event.getActionButton(),
                     scroll ? event.getAxisValue(MotionEvent.AXIS_HSCROLL) : 0, scroll ? event.getAxisValue(MotionEvent.AXIS_VSCROLL) : 0,
                     event.getMetaState());
    }

    // Keys when no text field has the keyboard, with what they type (KompozInput's view gives those that
    // aren't typing to keyEvent too, without it: the field takes the typing). Back is the activity's
    // (onBackPressed).
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return keyEvent(event, typed(event)) || super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return keyEvent(event, null) || super.onKeyUp(keyCode, event);
    }

    static boolean keyEvent(KeyEvent event) {
        return keyEvent(event, null);
    }

    private static boolean keyEvent(KeyEvent event, String typed) {
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN ||
            code == KeyEvent.KEYCODE_VOLUME_MUTE || code == KeyEvent.KEYCODE_POWER || code == KeyEvent.KEYCODE_HOME)
            return false;  // (the system's)
        return nativeKey(code, event.getAction() == KeyEvent.ACTION_DOWN, event.getMetaState(), typed);
    }

    // What a hardware keyboard's key types (its layout's character at the meta state, a dead key's accent put
    // on the next): null for none (a control character, a shortcut, the accent itself).
    private static int deadAccent;
    private static String typed(KeyEvent event) {
        if (event.getAction() != KeyEvent.ACTION_DOWN || event.isCtrlPressed() || event.isMetaPressed()) return null;
        int c = event.getUnicodeChar(event.getMetaState());
        if (c == 0) return null;
        if ((c & KeyCharacterMap.COMBINING_ACCENT) != 0) {
            deadAccent = c & KeyCharacterMap.COMBINING_ACCENT_MASK;
            return null;
        }
        if (deadAccent != 0) {
            int composed = KeyEvent.getDeadChar(deadAccent, c);
            deadAccent = 0;
            if (composed != 0) c = composed;
        }
        if (c < 0x20 || c == 0x7f) return null;
        return new String(Character.toChars(c));
    }

    // From native code: the mouse pointer's look (kompoz::Cursor's order).
    void setCursor(int cursor) {
        if (Build.VERSION.SDK_INT < 24) return;
        final int[] types = {PointerIcon.TYPE_ARROW, PointerIcon.TYPE_HAND, PointerIcon.TYPE_TEXT, PointerIcon.TYPE_CROSSHAIR,
                             PointerIcon.TYPE_ALL_SCROLL, PointerIcon.TYPE_HORIZONTAL_DOUBLE_ARROW, PointerIcon.TYPE_VERTICAL_DOUBLE_ARROW,
                             PointerIcon.TYPE_NO_DROP, PointerIcon.TYPE_WAIT};
        setPointerIcon(PointerIcon.getSystemIcon(getContext(), types[cursor >= 0 && cursor < types.length ? cursor : 0]));
    }

    // ---- What the system asks of the view -------------------------------------------------

    // The insets are KompozInput's view's to report.
    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        return insets;
    }

    // Autofill sees the Kompoz text fields as virtual views of the surface, and fills them.
    @Override
    public void onProvideAutofillVirtualStructure(ViewStructure structure, int flags) {
        if (Build.VERSION.SDK_INT >= 26) KompozInput.provideAutofill(this, structure);
    }

    @Override
    public void autofill(SparseArray<AutofillValue> values) {
        if (Build.VERSION.SDK_INT >= 26) KompozInput.autofill(values);
    }

    // TalkBack's touch exploration: the finger hovers over the Kompoz node under it.
    @Override
    public boolean dispatchHoverEvent(MotionEvent event) {
        if (KompozInput.hover(event)) return true;
        return super.dispatchHoverEvent(event);
    }

    private static native void nativeSurface(android.view.Surface surface, int width, int height);
    private static native void nativeFrame(long frameTimeNanos);
    private static native void nativeMotion(int action, int index, int count, int[] ids, float[] xy, long timeNanos, int toolType,
                                            int actionButton, float scrollX, float scrollY, int metaState);
    private static native boolean nativeKey(int keyCode, boolean down, int metaState, String typed);
    private static native void nativeDrag(int kind, float x, float y, byte[] mimeTypes);  // 0 begun, 1 moved, 2 gone
    private static native void nativeDrop(float x, float y, byte[][] clip);  // as KompozActivity.clipParts gives it
}
