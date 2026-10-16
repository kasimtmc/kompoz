package dev.kompoz.showcase;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

import dev.kompoz.KompozInput;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

// All UI is drawn by Kompoz in native code (libmain.so); SDLActivity provides the surface,
// input, soft keyboard and lifecycle.
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    // SDL's surface without its keyboard tracking: Kompoz's own input view owns the keyboard.
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return KompozInput.createSurface(context);
    }

    // rememberSaveable values outlive the process: Android may end the app in the background
    // and create it again from its saved instance state.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        KompozInput.onCreate(savedInstanceState);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onPause() {
        KompozInput.onPause();
        super.onPause();
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
}
