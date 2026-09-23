package com.alexbatalov.fallout2ce;

import android.content.Intent;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends SDLActivity {
    private boolean noExit = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final File externalFilesDir = getExternalFilesDir(null);

        // Bundled extras (Chinese font pack + default fallout2.cfg) are unpacked
        // once so devices without MTP-side write access get a working localized
        // setup. Existing user files are never overwritten.
        copyExtraAssets(externalFilesDir, "f2extra", externalFilesDir);

        final File configFile = new File(externalFilesDir, "fallout2.cfg");
        if (!configFile.exists()) {
            final File masterDatFile = new File(externalFilesDir, "master.dat");
            final File critterDatFile = new File(externalFilesDir, "critter.dat");
            if (!masterDatFile.exists() || !critterDatFile.exists()) {
                final Intent intent = new Intent(this, ImportActivity.class);
                startActivity(intent);

                noExit = true;
                finish();
            }
        }
    }

    private static void copyFile(InputStream in, File dest) throws IOException {
        try (OutputStream out = new FileOutputStream(dest)) {
            final byte[] buffer = new byte[16384];
            int bytesRead;
            while ((bytesRead = in.read(buffer)) != -1) {
                out.write(buffer, 0, bytesRead);
            }
        }
    }

    private boolean copyExtraAssets(File externalFilesDir, String assetPath, File destDir) {
        try {
            final String[] entries = getAssets().list(assetPath);
            if (entries == null || entries.length == 0) {
                return true;
            }
            for (final String entry : entries) {
                final String childAssetPath = assetPath + "/" + entry;
                final File dest = new File(destDir, entry);
                if (getAssets().list(childAssetPath).length > 0) {
                    if (!dest.exists() && !dest.mkdir()) {
                        return false;
                    }
                    if (!copyExtraAssets(externalFilesDir, childAssetPath, dest)) {
                        return false;
                    }
                } else {
                    if (dest.exists()) {
                        continue;
                    }
                    try (InputStream in = getAssets().open(childAssetPath)) {
                        copyFile(in, dest);
                    }
                }
            }
            return true;
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if (!noExit) {
            // Needed to make sure libc calls exit handlers, which releases
            // in-game resources.
            System.exit(0);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "fallout2-ce",
        };
    }
}
