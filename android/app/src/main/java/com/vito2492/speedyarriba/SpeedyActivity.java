package com.vito2492.speedyarriba;

import org.libsdl.app.SDLActivity;

public class SpeedyActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
