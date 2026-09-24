package com.nearchuckle.farcry;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import com.nearchuckle.farcry.controls.OscManager;
import com.nearchuckle.farcry.driver.DriverHook;
import com.nearchuckle.farcry.driver.DriverInfo;
import com.nearchuckle.farcry.driver.TurnipDriverManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Main game execution activity extending SDLActivity.
 * Configures Mesa Zink, Turnip custom drivers, working paths, and overlays touch controls.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "NearChuckle-GameActivity";
    private OscManager oscManager;
    /** Hardware gamepad -> WASD / mouse look / mouse buttons (see GamepadMapper). */
    private GamepadMapper gamepadMapper;

    @Override
    protected String[] getLibraries() {
        return new String[]{
                "c++_shared",
                "SDL3",
                "FarCry"
        };
    }

    @Override
    public void loadLibraries() {
        Context context = this;
        SharedPreferences prefs = context.getSharedPreferences(LauncherActivity.PREFS_NAME, MODE_PRIVATE);

        String defaultPath = android.os.Environment.getExternalStorageDirectory().getAbsolutePath() + "/FarCry";
        String gamePath = prefs.getString(LauncherActivity.KEY_GAME_PATH, defaultPath);
        boolean useZink = prefs.getBoolean(LauncherActivity.KEY_USE_ZINK, true);
        boolean turbo = TurnipDriverManager.isTurboEnabled(context);
        DriverInfo selectedDriver = TurnipDriverManager.getSelectedDriver(context);

        Log.i(TAG, "Initializing Far Cry Android Port...");
        Log.i(TAG, "Game Path: " + gamePath);
        Log.i(TAG, "Renderer Mesa Zink: " + useZink);
        Log.i(TAG, "GPU Driver: " + (selectedDriver != null ? selectedDriver.getName() : "System Default"));

        // Clean any problematic system.cfg before native engine starts
        LauncherActivity.cleanSystemConfigFiles(gamePath);

        // 1. Set Far Cry Working directory and Module search path
        String nativeLibDir = getApplicationInfo().nativeLibraryDir;
        try {
            if (!gamePath.isEmpty()) {
                Os.setenv("FARCRY_DATA_DIR", gamePath, true);
                Os.setenv("HOME", gamePath, true);
            }
            Os.setenv("MODULE_PATH", nativeLibDir + "/", true);
            Os.setenv("USER", "FarCryPlayer", true);
            Os.setenv("LOGNAME", "FarCryPlayer", true);
            Os.setenv("TMPDIR", context.getCacheDir().getAbsolutePath(), true);
        } catch (ErrnoException e) {
            Log.e(TAG, "Failed setting path environment variables", e);
        }

        // 2. Configure Mesa Zink (OpenGL over Vulkan)
        if (useZink) {
            try {
                Os.setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", true);
                Os.setenv("GALLIUM_DRIVER", "zink", true);
                Os.setenv("ZINK_DESCRIPTORS", "lazy", true);
                Os.setenv("MESA_GL_VERSION_OVERRIDE", "2.1COMPAT", true);
                Os.setenv("MESA_GLSL_VERSION_OVERRIDE", "140", true);
                // Allow ARB shaders for Far Cry CryEngine 1
                Os.setenv("MESA_EXTENSION_OVERRIDE", "+GL_ARB_vertex_program +GL_ARB_fragment_program", true);
                Log.i(TAG, "Configured Mesa Zink environment variables.");
            } catch (ErrnoException e) {
                Log.e(TAG, "Failed setting Zink environment variables", e);
            }
        }

        // Configure gl4es (desktop OpenGL 2.1 translation to GLES)
        try {
            Os.setenv("LIBGL_ES", "2", true);
            Os.setenv("LIBGL_GL", "21", true);
            Os.setenv("LIBGL_NPOT", "2", true);
            Os.setenv("LIBGL_MIPMAP", "1", true);
            Os.setenv("LIBGL_NOBANNER", "1", true);
            Os.setenv("LIBGL_NORMALIZE", "1", true);
            Os.setenv("LIBGL_NOTEXMAT", "0", true);
            Os.setenv("LIBGL_NODOWNSAMPLING", "1", true);
        } catch (ErrnoException e) {
            Log.e(TAG, "Failed setting gl4es environment variables", e);
        }

        // 3. Preload libc++_shared and libGL so native dependencies are resolved
        try {
            System.loadLibrary("c++_shared");
        } catch (Throwable t) {
            Log.w(TAG, "libc++_shared pre-load: " + t.getMessage());
        }
        try {
            System.loadLibrary("GL");
            Log.i(TAG, "libGL.so (gl4es) pre-loaded successfully.");
        } catch (Throwable t) {
            Log.w(TAG, "libGL pre-load: " + t.getMessage());
        }

        // 4. Configure Turnip / Custom Vulkan driver via adrenotools
        try {
            DriverHook.apply(context, selectedDriver, turbo);
        } catch (Throwable t) {
            Log.e(TAG, "Error applying GPU driver hook", t);
        }

        // 5. Load native libraries (c++_shared, SDL3, FarCry)
        super.loadLibraries();
    }

    @Override
    protected String[] getArguments() {
        SharedPreferences prefs = getSharedPreferences(LauncherActivity.PREFS_NAME, MODE_PRIVATE);
        List<String> args = new ArrayList<>();

        // Renderer
        args.add("\"r_Driver OpenGL\"");

        // FOV
        int fov = prefs.getInt(LauncherActivity.KEY_FOV, 90);
        args.add("\"game_fov " + fov + "\"");

        // Resolution
        int resMode = prefs.getInt(LauncherActivity.KEY_RES_MODE, 0);
        DisplayMetrics dm = new DisplayMetrics();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.view.Display display = null;
            try {
                display = getDisplay();
            } catch (Throwable ignored) {}
            if (display != null) {
                display.getRealMetrics(dm);
            } else {
                getWindowManager().getDefaultDisplay().getRealMetrics(dm);
            }
        } else {
            getWindowManager().getDefaultDisplay().getRealMetrics(dm);
        }
        int screenW = Math.max(dm.widthPixels, dm.heightPixels);
        int screenH = Math.min(dm.widthPixels, dm.heightPixels);
        if (resMode == 0) { // Native Display Resolution (true physical display panel size)
            args.add("\"r_Width " + screenW + "\"");
            args.add("\"r_Height " + screenH + "\"");
        } else if (resMode == 1) { // 1080p
            args.add("\"r_Width 1920\"");
            args.add("\"r_Height 1080\"");
        } else if (resMode == 2) { // 720p
            args.add("\"r_Width 1280\"");
            args.add("\"r_Height 720\"");
        } else if (resMode == 3) { // 540p
            args.add("\"r_Width 960\"");
            args.add("\"r_Height 540\"");
        }
        args.add("\"r_Fullscreen 1\"");

        // Graphics quality settings for full SM2.0 lighting, water, and 3D menu background
        args.add("\"r_Quality_BumpMapping 3\"");
        args.add("\"r_NoPS20 0\"");
        args.add("\"r_GL_NV30_PS20 1\"");
        args.add("\"GL_NV30_PS20 1\"");

        // Custom parameters
        String customArgs = prefs.getString(LauncherActivity.KEY_CUSTOM_ARGS, "").trim();
        if (!customArgs.isEmpty()) {
            String[] split = customArgs.split("\\s+");
            for (String arg : split) {
                if (!arg.isEmpty()) {
                    args.add(arg);
                }
            }
        }

        return args.toArray(new String[0]);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        CrashHandler.init(this);
        SharedPreferences prefs = getSharedPreferences(LauncherActivity.PREFS_NAME, MODE_PRIVATE);
        String gamePath = prefs.getString(LauncherActivity.KEY_GAME_PATH, "");
        LauncherActivity.cleanSystemConfigFiles(gamePath);
        super.onCreate(savedInstanceState);

        // Extend edge-to-edge across the camera notch / display cutout
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
        }
        hideSystemUI();

        // Attach listener to continually enforce immersive full screen on insets/visibility change
        View decorView = getWindow().getDecorView();
        if (decorView != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                decorView.setOnApplyWindowInsetsListener((v, insets) -> {
                    hideSystemUI();
                    return WindowInsets.CONSUMED;
                });
            } else {
                decorView.setOnSystemUiVisibilityChangeListener(visibility -> {
                    if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                        hideSystemUI();
                    }
                });
            }
        }

        // Attach On-Screen Controls (OSC) to SDL layout
        setupControlsOverlay();

        // Hardware gamepad support: keyboard and mouse already reach the engine through SDL
        // (SDLSurface.onKey / relative-mouse pointer capture), but CryEngine 1 has no gamepad
        // backend on Android, so the pad is translated into keys / mouse here.
        gamepadMapper = new GamepadMapper(this);
    }

    // Hardware keyboard and mouse events keep flowing through SDL as before; only gamepad
    // events are consumed by the mapper.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (gamepadMapper != null && gamepadMapper.handleKeyEvent(event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    // Joystick axes are consumed by SDLSurface's generic-motion listener at view level, so we
    // intercept one step earlier - at the activity dispatch level - and only for joystick-class
    // devices. Everything else (mouse hover, etc.) flows to SDL untouched.
    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (gamepadMapper != null && gamepadMapper.handleMotionEvent(event)) {
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    protected void onPause() {
        if (gamepadMapper != null) {
            gamepadMapper.releaseAll();
        }
        if (oscManager != null) {
            // A sticky button (AIM) keeps its key/mouse button down without a finger on it -
            // it must not stay pressed while the game is in the background.
            oscManager.releaseAllPressed();
        }
        super.onPause();
    }

    private void setupControlsOverlay() {
        if (mLayout == null) return;

        SharedPreferences prefs = getSharedPreferences(LauncherActivity.PREFS_NAME, MODE_PRIVATE);
        boolean hideControls = prefs.getBoolean(LauncherActivity.KEY_HIDE_CONTROLS, false);

        RelativeLayout oscContainer = new RelativeLayout(this);
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        oscContainer.setLayoutParams(lp);

        oscManager = new OscManager();
        oscManager.init(oscContainer, this, false);

        if (hideControls) {
            oscContainer.setVisibility(View.GONE);
        }

        mLayout.addView(oscContainer);
    }

    private void hideSystemUI() {
        Window window = getWindow();
        if (window == null) return;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars()
                        | WindowInsets.Type.navigationBars()
                        | WindowInsets.Type.captionBar());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }

        View decorView = window.getDecorView();
        if (decorView != null) {
            decorView.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemUI();
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        hideSystemUI();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUI();
        } else {
            // Nothing may stay "pressed" while the game is in the background.
            if (gamepadMapper != null) {
                gamepadMapper.releaseAll();
            }
            if (oscManager != null) {
                oscManager.releaseAllPressed();
            }
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try {
            SharedPreferences prefs = getSharedPreferences(LauncherActivity.PREFS_NAME, MODE_PRIVATE);
            String gamePath = prefs.getString(LauncherActivity.KEY_GAME_PATH, "");
            LauncherActivity.cleanSystemConfigFiles(gamePath);
        } catch (Throwable ignored) {}
        // Ensure clean exit of native engine
        Process.killProcess(Process.myPid());
    }
}
