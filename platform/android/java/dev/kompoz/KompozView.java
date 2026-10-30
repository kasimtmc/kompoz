package dev.kompoz;

import android.content.Context;
import android.os.Build;
import android.util.SparseArray;
import android.view.Choreographer;
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
    }

    // ---- The surface ----------------------------------------------------------------------

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeSurface(holder.getSurface(), width, height);
    }

    // Native code lets go of the surface before this returns, as it must.
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSurface(null, 0, 0);
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
        nativeFrame(frameTimeNanos);
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

    // Keys when no text field has the keyboard (KompozInput's view gives those that aren't
    // typing to keyEvent too). Back is the activity's (onBackPressed).
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return keyEvent(event) || super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return keyEvent(event) || super.onKeyUp(keyCode, event);
    }

    static boolean keyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN ||
            code == KeyEvent.KEYCODE_VOLUME_MUTE || code == KeyEvent.KEYCODE_POWER || code == KeyEvent.KEYCODE_HOME)
            return false;  // (the system's)
        return nativeKey(code, event.getAction() == KeyEvent.ACTION_DOWN, event.getMetaState());
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
    private static native boolean nativeKey(int keyCode, boolean down, int metaState);
}
