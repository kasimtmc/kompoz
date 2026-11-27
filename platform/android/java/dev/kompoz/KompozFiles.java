package dev.kompoz;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.util.ArrayList;

/**
 * The system's file dialogs for Kompoz's FileKit launchers, and the content:// files they give read and
 * written for native code. A dialog is shown from this activity (transparent, its own task's: the app's
 * activity is singleInstance, from which a picker's result never comes back): it starts the system's
 * document picker (the photo picker for images and videos), hands what was chosen to native code and
 * finishes. An app declares it in its manifest:
 *
 * <pre>
 * &lt;activity android:name="dev.kompoz.KompozFiles" android:exported="false" android:excludeFromRecents="true"
 *           android:theme="@android:style/Theme.Translucent.NoTitleBar" /&gt;
 * </pre>
 */
public final class KompozFiles extends Activity {
    static Context context;  // the application's (KompozActivity sets it)

    // What native code's FileDialogRequest::Kind and FileKitType::Kind are.
    static final int OPEN_FILES = 0, OPEN_DIRECTORY = 1, SAVE_FILE = 2;
    static final int TYPE_FILE = 0, TYPE_IMAGE = 1, TYPE_VIDEO = 2, TYPE_IMAGE_AND_VIDEO = 3;

    /** What was chosen (none: closed without a choice), or why the dialog couldn't be shown. */
    static native void nativeOnResult(long request, String[] uris, String error);

    // ---- Called from native code ------------------------------------------------------------------

    /** Shows a dialog over `from`: its answer comes to nativeOnResult with `request`. */
    static void show(Activity from, long request, int kind, int type, String[] extensions, boolean multiple, int maxItems,
                     String suggestedName, String initialUri) {
        Intent intent = new Intent(from, KompozFiles.class)
                .putExtra("request", request).putExtra("kind", kind).putExtra("type", type).putExtra("extensions", extensions)
                .putExtra("multiple", multiple).putExtra("maxItems", maxItems).putExtra("name", suggestedName)
                .putExtra("initial", initialUri);
        try {
            from.startActivity(intent);
            from.overridePendingTransition(0, 0);
        } catch (ActivityNotFoundException e) {
            nativeOnResult(request, null, "kompoz: dev.kompoz.KompozFiles isn't in the app's manifest");
        }
    }

    // ---- The dialog -------------------------------------------------------------------------------

    private long request;
    private boolean answered;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent in = getIntent();
        request = in.getLongExtra("request", 0);
        if (savedInstanceState != null) return;  // (made again while the picker is shown: its result comes here)
        Intent picker = pickerIntent(in);
        try {
            startActivityForResult(picker, 1);
        } catch (ActivityNotFoundException e) {
            answer(null, "kompoz: no app here picks files (" + picker.getAction() + ")");
        }
    }

    private static Intent pickerIntent(Intent in) {
        int kind = in.getIntExtra("kind", OPEN_FILES), type = in.getIntExtra("type", TYPE_FILE);
        boolean multiple = in.getBooleanExtra("multiple", false);
        int maxItems = in.getIntExtra("maxItems", 0);
        String initial = in.getStringExtra("initial");
        Intent intent;
        if (kind == OPEN_DIRECTORY) {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        } else if (kind == SAVE_FILE) {
            String name = in.getStringExtra("name");
            intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                    .setType(mimeOfName(name)).putExtra(Intent.EXTRA_TITLE, name);
        } else if (type != TYPE_FILE && Build.VERSION.SDK_INT >= 33) {
            // The photo picker (Android 13+): no permission needed, the user's photos and videos.
            intent = new Intent(MediaStore.ACTION_PICK_IMAGES);
            if (type == TYPE_IMAGE) intent.setType("image/*");
            else if (type == TYPE_VIDEO) intent.setType("video/*");
            if (multiple) intent.putExtra(MediaStore.EXTRA_PICK_IMAGES_MAX, maxItems > 1 ? Math.min(maxItems, MediaStore.getPickImagesMaxLimit()) : MediaStore.getPickImagesMaxLimit());
            return intent;
        } else {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE);
            String[] types = mimeTypes(type, in.getStringArrayExtra("extensions"));
            intent.setType(types.length == 1 ? types[0] : "*/*");
            if (types.length > 1) intent.putExtra(Intent.EXTRA_MIME_TYPES, types);
            if (multiple) intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        }
        if (initial != null && initial.startsWith("content://") && Build.VERSION.SDK_INT >= 26)
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(initial));
        return intent;
    }

    // The MIME types of the extensions asked for (any file where one has none known).
    private static String[] mimeTypes(int type, String[] extensions) {
        if (type == TYPE_IMAGE) return new String[] {"image/*"};
        if (type == TYPE_VIDEO) return new String[] {"video/*"};
        if (type == TYPE_IMAGE_AND_VIDEO) return new String[] {"image/*", "video/*"};
        if (extensions == null || extensions.length == 0) return new String[] {"*/*"};
        ArrayList<String> out = new ArrayList<>();
        for (String e : extensions) {
            String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(e.toLowerCase(java.util.Locale.ROOT));
            if (mime == null) {
                // (markdown and others Android doesn't know: as text, else anything)
                if (e.equalsIgnoreCase("md") || e.equalsIgnoreCase("markdown")) mime = "text/markdown";
                else return new String[] {"*/*"};
            }
            if (!out.contains(mime)) out.add(mime);
            if (mime.startsWith("text/") && !out.contains("text/plain")) out.add("text/plain");  // (some providers call them text/plain)
        }
        return out.toArray(new String[0]);
    }

    private static String mimeOfName(String name) {
        int dot = name != null ? name.lastIndexOf('.') : -1;
        String mime = dot >= 0 ? MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase(java.util.Locale.ROOT)) : null;
        return mime != null ? mime : "application/octet-stream";
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        ArrayList<String> uris = new ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            ClipData clip = data.getClipData();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); ++i)
                    if (clip.getItemAt(i).getUri() != null) uris.add(clip.getItemAt(i).getUri().toString());
            } else if (data.getData() != null) {
                Uri uri = data.getData();
                if (getIntent().getIntExtra("kind", OPEN_FILES) == OPEN_DIRECTORY) {
                    // A tree: as its root folder's document (its children are reached through it).
                    uri = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));
                }
                uris.add(uri.toString());
            }
        }
        answer(uris.toArray(new String[0]), null);
    }

    private void answer(String[] uris, String error) {
        if (!answered) {
            answered = true;
            nativeOnResult(request, uris, error);
        }
        finish();
        overridePendingTransition(0, 0);
    }

    @Override
    protected void onDestroy() {
        if (!answered && isFinishing()) {  // (gone without a result: closed)
            answered = true;
            nativeOnResult(request, new String[0], null);
        }
        super.onDestroy();
    }

    // ---- content:// files, for native code (any thread) ---------------------------------------------

    private static ContentResolver resolver() {
        return context.getContentResolver();
    }

    /** A file descriptor of it opened for `mode` ("r", "wt"), its own now; -1 if it can't be. */
    static int open(String uri, String mode) {
        try {
            ParcelFileDescriptor pfd;
            try {
                pfd = resolver().openFileDescriptor(Uri.parse(uri), mode);
            } catch (IllegalArgumentException | UnsupportedOperationException e) {
                if (!mode.equals("wt")) throw e;
                pfd = resolver().openFileDescriptor(Uri.parse(uri), "w");  // (providers without truncation)
            }
            return pfd != null ? pfd.detachFd() : -1;
        } catch (Exception e) {
            return -1;
        }
    }

    /** {name, size, MIME type, last modified (ms), "d" if a folder}; null if it isn't there. */
    static String[] query(String uri) {
        Uri u = Uri.parse(uri);
        boolean document = DocumentsContract.isDocumentUri(context, u);
        String[] columns = document
                ? new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_SIZE,
                                DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_LAST_MODIFIED}
                : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        try (Cursor c = resolver().query(u, columns, null, null, null)) {
            if (c == null || !c.moveToFirst()) return null;
            String name = c.isNull(0) ? u.getLastPathSegment() : c.getString(0);
            String size = c.isNull(1) ? "-1" : Long.toString(c.getLong(1));
            String mime = document ? c.getString(2) : resolver().getType(u);
            String modified = document && !c.isNull(3) ? Long.toString(c.getLong(3)) : "0";
            boolean folder = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
            return new String[] {name, size, mime != null ? mime : "", modified, folder ? "d" : ""};
        } catch (Exception e) {
            return null;
        }
    }

    /** A picked folder's children: their URIs and names, one after the other. */
    static String[] children(String uri) {
        Uri folder = Uri.parse(uri);
        ArrayList<String> out = new ArrayList<>();
        try {
            Uri list = DocumentsContract.buildChildDocumentsUriUsingTree(folder, DocumentsContract.getDocumentId(folder));
            String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME};
            try (Cursor c = resolver().query(list, columns, null, null, null)) {
                while (c != null && c.moveToNext()) {
                    out.add(DocumentsContract.buildDocumentUriUsingTree(folder, c.getString(0)).toString());
                    out.add(c.getString(1));
                }
            }
        } catch (Exception e) {
            // (not a folder of a picked tree: none)
        }
        return out.toArray(new String[0]);
    }

    /** A new file (or folder) in a picked folder: its URI, null if it can't be made. */
    static String create(String parent, String name, boolean directory) {
        try {
            Uri made = DocumentsContract.createDocument(resolver(), Uri.parse(parent),
                                                        directory ? DocumentsContract.Document.MIME_TYPE_DIR : mimeOfName(name), name);
            return made != null ? made.toString() : null;
        } catch (Exception e) {
            return null;
        }
    }

    static boolean remove(String uri) {
        try {
            return DocumentsContract.deleteDocument(resolver(), Uri.parse(uri));
        } catch (Exception e) {
            return false;
        }
    }

    /** Its access kept after the app restarts (bookmarkData): read and write where it was given both. */
    static void persist(String uri) {
        Uri u = Uri.parse(uri);
        int both = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        try {
            resolver().takePersistableUriPermission(u, both);
        } catch (SecurityException e) {
            try {
                resolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException ignored) {
                // (one that can't be kept: a photo picker's before Android 13's later updates)
            }
        }
    }
}
