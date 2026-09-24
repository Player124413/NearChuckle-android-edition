package com.nearchuckle.farcry;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.DisplayMetrics;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Switch;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;

import com.nearchuckle.farcry.driver.DriverHook;
import com.nearchuckle.farcry.driver.DriverInfo;
import com.nearchuckle.farcry.driver.TurnipDriverManager;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * Main launcher activity for Far Cry (NearChuckle) on Android.
 * Provides game file configuration, Mesa Zink settings, Turnip driver management, and controls setup.
 */
public class LauncherActivity extends Activity {
    public static final String PREFS_NAME = "farcry_prefs";
    public static final String KEY_GAME_PATH = "game_path";
    public static final String KEY_USE_ZINK = "use_zink";
    public static final String KEY_RES_MODE = "resolution_mode";
    public static final String KEY_FOV = "game_fov";
    public static final String KEY_DEVMODE = "devmode";
    public static final String KEY_CUSTOM_ARGS = "custom_args";
    public static final String KEY_HIDE_CONTROLS = "hide_controls";
    public static final String KEY_MOUSE_SENSITIVITY = "mouse_sensitivity";

    /** Delay before the automatic update check starts, so the UI is on screen first. */
    private static final long UPDATE_CHECK_DELAY_MS = 600L;

    private static final int REQ_CODE_ZIP = 1001;
    private static final int REQ_CODE_STORAGE_PERMISSION = 1002;

    private EditText editGamePath;
    private TextView tvGamePathStatus;
    private TextView tvGpuDetect;
    private Spinner spinnerDrivers;
    private Button btnDeleteDriver;
    private Switch switchGpuTurbo;
    private Switch switchUseZink;
    private Spinner spinnerResolution;
    private TextView tvFovLabel;
    private SeekBar seekbarFov;
    private EditText editCustomArgs;
    private TextView tvSensLabel;
    private SeekBar seekbarSensitivity;
    private Switch switchHideControls;

    private List<DriverInfo> installedDrivers = new ArrayList<>();
    private ArrayAdapter<String> driverAdapter;

    /** Set when an APK was downloaded but the user still has to allow "install unknown apps". */
    private File pendingInstallApk;
    /** The automatic check runs once per activity instance (not after every onResume). */
    private boolean autoUpdateCheckStarted = false;
    /** Currently visible update dialog, so it is never shown twice at once. */
    private AlertDialog updateDialog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        CrashHandler.init(this);
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);

        requestStoragePermissions();

        initViews();
        loadPreferences();
        setupGpuDetection();
        setupDriverSpinner();
        setupListeners();

        // Remove APK files left over by previous sessions (the fresh one is downloaded on demand).
        UpdateManager.clearDownloadedUpdates(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (CrashHandler.hasUnreadCrash(this)) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_crash_detected_title)
                    .setMessage(R.string.dialog_crash_detected_msg)
                    .setPositiveButton(R.string.btn_open_report, (dialog, which) -> {
                        Intent intent = new Intent(this, CrashReportActivity.class);
                        startActivity(intent);
                    })
                    .setNegativeButton(R.string.cancel, null)
                    .show();
        }

        // The user may have just granted "install unknown apps" - finish the pending install.
        if (pendingInstallApk != null && UpdateManager.canInstallPackages(this)) {
            File apk = pendingInstallApk;
            pendingInstallApk = null;
            installDownloadedApk(apk);
        }

        if (!autoUpdateCheckStarted) {
            autoUpdateCheckStarted = true;
            new Handler(Looper.getMainLooper())
                    .postDelayed(() -> checkForUpdates(false), UPDATE_CHECK_DELAY_MS);
        }
    }

    private void initViews() {
        editGamePath = findViewById(R.id.edit_game_path);
        tvGamePathStatus = findViewById(R.id.tv_game_path_status);
        tvGpuDetect = findViewById(R.id.tv_gpu_detect);
        spinnerDrivers = findViewById(R.id.spinner_drivers);
        btnDeleteDriver = findViewById(R.id.btn_delete_driver);
        switchGpuTurbo = findViewById(R.id.switch_gpu_turbo);
        switchUseZink = findViewById(R.id.switch_use_zink);
        spinnerResolution = findViewById(R.id.spinner_resolution);
        tvFovLabel = findViewById(R.id.tv_fov_label);
        seekbarFov = findViewById(R.id.seekbar_fov);
        editCustomArgs = findViewById(R.id.edit_custom_args);
        tvSensLabel = findViewById(R.id.tv_sensitivity_label);
        seekbarSensitivity = findViewById(R.id.seekbar_sensitivity);
        switchHideControls = findViewById(R.id.switch_hide_controls);

        // Resolution options
        String[] resOptions = new String[]{
                getString(R.string.res_auto),
                getString(R.string.res_1080p),
                getString(R.string.res_720p),
                getString(R.string.res_540p)
        };
        ArrayAdapter<String> resAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, resOptions);
        spinnerResolution.setAdapter(resAdapter);
    }

    private void setupGpuDetection() {
        boolean isAdreno = DriverHook.isQualcommAdreno();
        if (isAdreno) {
            tvGpuDetect.setText(R.string.gpu_adreno_detected);
            tvGpuDetect.setTextColor(getColor(R.color.status_green));
            switchGpuTurbo.setEnabled(true);
        } else {
            tvGpuDetect.setText(R.string.gpu_other_detected);
            tvGpuDetect.setTextColor(getColor(R.color.text_secondary));
            switchGpuTurbo.setEnabled(false);
        }
    }

    private void setupDriverSpinner() {
        installedDrivers = TurnipDriverManager.getInstalledDrivers(this);
        List<String> names = new ArrayList<>();
        int selectedIndex = 0;
        DriverInfo selected = TurnipDriverManager.getSelectedDriver(this);

        for (int i = 0; i < installedDrivers.size(); i++) {
            DriverInfo d = installedDrivers.get(i);
            names.add(d.toString());
            if (d.getId().equals(selected.getId())) {
                selectedIndex = i;
            }
        }

        driverAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, names);
        spinnerDrivers.setAdapter(driverAdapter);
        spinnerDrivers.setSelection(selectedIndex);

        btnDeleteDriver.setEnabled(!selected.isSystem());
    }

    private void loadPreferences() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        // Default Far Cry path guess
        String defaultPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/FarCry";
        String path = prefs.getString(KEY_GAME_PATH, defaultPath);
        editGamePath.setText(path);
        validateGamePath(path);

        switchUseZink.setChecked(prefs.getBoolean(KEY_USE_ZINK, true));
        spinnerResolution.setSelection(prefs.getInt(KEY_RES_MODE, 0));

        int fov = prefs.getInt(KEY_FOV, 90);
        seekbarFov.setProgress(Math.max(0, Math.min(50, fov - 70)));
        tvFovLabel.setText(getString(R.string.label_fov, fov));

        editCustomArgs.setText(prefs.getString(KEY_CUSTOM_ARGS, ""));

        switchGpuTurbo.setChecked(TurnipDriverManager.isTurboEnabled(this));

        float sens = prefs.getFloat(KEY_MOUSE_SENSITIVITY, 1.0f);
        int sensProgress = Math.round((sens - 0.5f) * 10f);
        seekbarSensitivity.setProgress(Math.max(0, Math.min(25, sensProgress)));
        tvSensLabel.setText(getString(R.string.label_mouse_sensitivity, sens));

        switchHideControls.setChecked(prefs.getBoolean(KEY_HIDE_CONTROLS, false));
    }

    private void savePreferences() {
        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
        editor.putString(KEY_GAME_PATH, editGamePath.getText().toString().trim());
        editor.putBoolean(KEY_USE_ZINK, switchUseZink.isChecked());
        editor.putInt(KEY_RES_MODE, spinnerResolution.getSelectedItemPosition());

        int fov = seekbarFov.getProgress() + 70;
        editor.putInt(KEY_FOV, fov);
        editor.putString(KEY_CUSTOM_ARGS, editCustomArgs.getText().toString().trim());

        float sens = 0.5f + (seekbarSensitivity.getProgress() / 10.0f);
        editor.putFloat(KEY_MOUSE_SENSITIVITY, sens);
        editor.putBoolean(KEY_HIDE_CONTROLS, switchHideControls.isChecked());
        editor.apply();

        TurnipDriverManager.setTurboEnabled(this, switchGpuTurbo.isChecked());
    }

    private void setupListeners() {
        // Path input change validation
        editGamePath.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int count, int after) {
                validateGamePath(s.toString().trim());
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        // Folder browse button
        findViewById(R.id.btn_browse_folder).setOnClickListener(v -> showFolderPickerDialog());

        // Driver selection
        spinnerDrivers.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position >= 0 && position < installedDrivers.size()) {
                    DriverInfo chosen = installedDrivers.get(position);
                    TurnipDriverManager.setSelectedDriver(LauncherActivity.this, chosen.getId());
                    btnDeleteDriver.setEnabled(!chosen.isSystem());
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });

        // Install Driver ZIP button
        findViewById(R.id.btn_install_driver_zip).setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.setType("application/zip");
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            try {
                startActivityForResult(Intent.createChooser(intent, getString(R.string.choose_driver_zip_title)), REQ_CODE_ZIP);
            } catch (Exception e) {
                Toast.makeText(this, R.string.error_no_file_manager, Toast.LENGTH_SHORT).show();
            }
        });

        // Delete Driver button
        btnDeleteDriver.setOnClickListener(v -> {
            DriverInfo sel = TurnipDriverManager.getSelectedDriver(this);
            if (sel.isSystem()) return;

            new AlertDialog.Builder(this)
                    .setTitle(R.string.btn_delete_driver)
                    .setMessage(getString(R.string.dialog_delete_driver_prompt, sel.getName()))
                    .setPositiveButton(R.string.yes, (dialog, which) -> {
                        TurnipDriverManager.deleteDriver(this, sel.getId());
                        setupDriverSpinner();
                        Toast.makeText(this, R.string.toast_driver_removed, Toast.LENGTH_SHORT).show();
                    })
                    .setNegativeButton(R.string.no, null)
                    .show();
        });

        // FOV seekbar
        seekbarFov.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int fov = progress + 70;
                tvFovLabel.setText(getString(R.string.label_fov, fov));
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        // Sensitivity seekbar
        seekbarSensitivity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float sens = 0.5f + (progress / 10.0f);
                tvSensLabel.setText(getString(R.string.label_mouse_sensitivity, sens));
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        // NOTE: the standalone "Configure On-Screen Controls" button was removed from the
        // launcher UI on purpose. Controls are still adjustable in-game via the EDIT button
        // (OscManager edit mode); ConfigureControlsActivity stays available for debugging.

        // View Logs & Crash Reports button
        findViewById(R.id.btn_view_logs).setOnClickListener(v -> {
            Intent intent = new Intent(this, CrashReportActivity.class);
            startActivity(intent);
        });

        // Download GL Shaders button
        Button btnDownloadShaders = findViewById(R.id.btn_download_shaders);
        if (btnDownloadShaders != null) {
            btnDownloadShaders.setOnClickListener(v -> downloadShadersPack());
        }

        // Manual "Check for updates" button
        Button btnCheckUpdates = findViewById(R.id.btn_check_updates);
        if (btnCheckUpdates != null) {
            btnCheckUpdates.setOnClickListener(v -> checkForUpdates(true));
        }

        // Launch Game Button
        findViewById(R.id.btn_launch_game).setOnClickListener(v -> launchGame());
    }

    private static final String SHADERS_URL = "https://rohitcodes.fyi/nearchuckle/files/shadercache/GL_Shaders_20260517.pak";

    private void downloadShadersPack() {
        String path = editGamePath.getText().toString().trim();
        if (path.isEmpty()) {
            Toast.makeText(this, R.string.toast_specify_game_path_first, Toast.LENGTH_SHORT).show();
            return;
        }
        File gameDir = new File(path);
        if (!gameDir.exists() || !gameDir.isDirectory()) {
            Toast.makeText(this, R.string.error_no_game_folder, Toast.LENGTH_SHORT).show();
            return;
        }

        File fcData = new File(gameDir, "FCData");
        if (!fcData.exists()) {
            fcData.mkdirs();
        }
        File targetPak = new File(fcData, "GL_Shaders_20260517.pak");
        if (targetPak.exists() && targetPak.length() > 1024) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.btn_download_shaders)
                    .setMessage(getString(R.string.dialog_shaders_exists_prompt, (int) (targetPak.length() / 1024)))
                    .setPositiveButton(R.string.yes, (dialog, which) -> startShaderDownload(targetPak))
                    .setNegativeButton(R.string.no, null)
                    .show();
        } else {
            startShaderDownload(targetPak);
        }
    }

    private void startShaderDownload(File targetFile) {
        android.app.ProgressDialog progress = new android.app.ProgressDialog(this);
        progress.setTitle(R.string.btn_download_shaders);
        progress.setMessage(getString(R.string.dialog_shaders_downloading));
        progress.setProgressStyle(android.app.ProgressDialog.STYLE_HORIZONTAL);
        progress.setMax(100);
        progress.setCancelable(false);
        progress.show();

        new Thread(() -> {
            java.io.InputStream in = null;
            java.io.OutputStream out = null;
            java.net.HttpURLConnection conn = null;
            try {
                java.net.URL url = new java.net.URL(SHADERS_URL);
                conn = (java.net.HttpURLConnection) url.openConnection();
                conn.setConnectTimeout(15000);
                conn.setReadTimeout(30000);
                conn.connect();

                if (conn.getResponseCode() != java.net.HttpURLConnection.HTTP_OK) {
                    throw new java.io.IOException("HTTP " + conn.getResponseCode() + " " + conn.getResponseMessage());
                }

                int fileLength = conn.getContentLength();
                in = conn.getInputStream();
                out = new java.io.FileOutputStream(targetFile);

                byte[] buffer = new byte[8192];
                long total = 0;
                int count;
                while ((count = in.read(buffer)) != -1) {
                    total += count;
                    if (fileLength > 0) {
                        int pct = (int) (total * 100 / fileLength);
                        runOnUiThread(() -> progress.setProgress(pct));
                    }
                    out.write(buffer, 0, count);
                }
                out.flush();

                runOnUiThread(() -> {
                    progress.dismiss();
                    Toast.makeText(this, R.string.dialog_shaders_success, Toast.LENGTH_LONG).show();
                    new AlertDialog.Builder(this)
                            .setTitle(R.string.dialog_shaders_downloaded_title)
                            .setMessage(R.string.dialog_shaders_success)
                            .setPositiveButton(R.string.ok, null)
                            .show();
                });
            } catch (Exception e) {
                targetFile.delete();
                runOnUiThread(() -> {
                    progress.dismiss();
                    new AlertDialog.Builder(this)
                            .setTitle(R.string.error_title)
                            .setMessage(getString(R.string.dialog_shaders_failed) + e.getMessage())
                            .setPositiveButton(R.string.ok, null)
                            .show();
                });
            } finally {
                try { if (out != null) out.close(); } catch (Exception ignored) {}
                try { if (in != null) in.close(); } catch (Exception ignored) {}
                if (conn != null) conn.disconnect();
            }
        }).start();
    }

    // ---------------------------------------------------------------------------------------------
    // Launcher self-update (new APK version notification)
    // ---------------------------------------------------------------------------------------------

    /**
     * Asks the update server whether a newer APK exists.
     *
     * @param manual true when the user pressed "Check for updates" (result is always reported)
     */
    private void checkForUpdates(final boolean manual) {
        if (manual) {
            Toast.makeText(this, R.string.toast_update_checking, Toast.LENGTH_SHORT).show();
        }

        UpdateManager.checkAsync(this, manual, (info, error) -> {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            if (info != null) {
                showUpdateDialog(info);
            } else if (manual) {
                if (error != null) {
                    Toast.makeText(this, R.string.toast_update_check_failed, Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(this,
                            getString(R.string.toast_update_up_to_date,
                                    UpdateManager.getInstalledVersionName(this)),
                            Toast.LENGTH_LONG).show();
                }
            }
        });
    }

    /** "Update available" dialog: [Download] / [Cancel]. */
    private void showUpdateDialog(final UpdateManager.UpdateInfo info) {
        if (updateDialog != null && updateDialog.isShowing()) {
            return;
        }

        String message = getString(R.string.dialog_update_msg,
                info.versionName != null ? info.versionName : String.valueOf(info.versionCode),
                info.versionCode,
                UpdateManager.getInstalledVersionName(this),
                UpdateManager.getInstalledVersionCode(this));
        if (info.notes != null && !info.notes.isEmpty()) {
            message = message + "\n\n" + info.notes;
        }

        updateDialog = new AlertDialog.Builder(this)
                .setTitle(R.string.dialog_update_title)
                .setMessage(message)
                .setPositiveButton(R.string.btn_update_download, (dialog, which) -> startUpdateDownload(info))
                .setNegativeButton(R.string.cancel, null)
                .setCancelable(!info.mandatory)
                .show();
    }

    /** Downloads the new APK with a progress bar, then hands it to the system installer. */
    private void startUpdateDownload(final UpdateManager.UpdateInfo info) {
        if (info.apkUrl == null || info.apkUrl.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_update_failed_title)
                    .setMessage(getString(R.string.dialog_update_failed_msg, "no APK URL in update manifest"))
                    .setPositiveButton(R.string.ok, null)
                    .show();
            return;
        }

        final android.app.ProgressDialog progress = new android.app.ProgressDialog(this);
        progress.setTitle(R.string.dialog_update_title);
        progress.setMessage(getString(R.string.dialog_update_downloading));
        progress.setProgressStyle(android.app.ProgressDialog.STYLE_HORIZONTAL);
        progress.setMax(100);
        progress.setCancelable(false);
        progress.show();

        UpdateManager.downloadApk(this, info, new UpdateManager.DownloadListener() {
            @Override
            public void onProgress(final int percent, final long downloadedBytes, final long totalBytes) {
                runOnUiThread(() -> {
                    progress.setProgress(percent);
                    progress.setMessage(getString(R.string.dialog_update_downloading_progress,
                            percent,
                            UpdateManager.formatSize(downloadedBytes),
                            UpdateManager.formatSize(totalBytes)));
                });
            }

            @Override
            public void onSuccess(final File apk) {
                runOnUiThread(() -> {
                    progress.dismiss();
                    if (isFinishing() || isDestroyed()) {
                        return;
                    }
                    if (!UpdateManager.canInstallPackages(LauncherActivity.this)) {
                        askForUnknownSourcesPermission(apk);
                    } else {
                        installDownloadedApk(apk);
                    }
                });
            }

            @Override
            public void onError(final Exception e) {
                runOnUiThread(() -> {
                    progress.dismiss();
                    if (isFinishing() || isDestroyed()) {
                        return;
                    }
                    new AlertDialog.Builder(LauncherActivity.this)
                            .setTitle(R.string.dialog_update_failed_title)
                            .setMessage(getString(R.string.dialog_update_failed_msg,
                                    UpdateManager.describe(e)))
                            .setPositiveButton(R.string.ok, null)
                            .show();
                });
            }
        });
    }

    /** Android 8+: the user has to allow "install unknown apps" before the installer can run. */
    private void askForUnknownSourcesPermission(final File apk) {
        pendingInstallApk = apk;
        new AlertDialog.Builder(this)
                .setTitle(R.string.dialog_unknown_sources_title)
                .setMessage(R.string.dialog_unknown_sources_msg)
                .setPositiveButton(R.string.btn_permission_allow, (dialog, which) -> {
                    try {
                        startActivity(UpdateManager.unknownSourcesSettingsIntent(this));
                    } catch (Exception e) {
                        pendingInstallApk = null;
                        Toast.makeText(this, R.string.toast_update_check_failed, Toast.LENGTH_LONG).show();
                    }
                })
                .setNegativeButton(R.string.cancel, (dialog, which) -> pendingInstallApk = null)
                .setOnCancelListener(dialog -> pendingInstallApk = null)
                .show();
    }

    private void installDownloadedApk(File apk) {
        try {
            UpdateManager.installApk(this, apk);
            Toast.makeText(this, R.string.toast_update_installing, Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_update_failed_title)
                    .setMessage(getString(R.string.dialog_update_failed_msg, UpdateManager.describe(e)))
                    .setPositiveButton(R.string.ok, null)
                    .show();
        }
    }

    private void validateGamePath(String path) {
        if (path == null || path.isEmpty()) {
            tvGamePathStatus.setText(R.string.status_files_missing);
            tvGamePathStatus.setTextColor(getColor(R.color.status_red));
            return;
        }

        File folder = new File(path);
        if (!folder.exists() || !folder.isDirectory()) {
            tvGamePathStatus.setText(R.string.folder_not_exist);
            tvGamePathStatus.setTextColor(getColor(R.color.status_red));
            return;
        }

        // Clean any problematic system.cfg in game folder and profiles
        cleanSystemConfigFiles(path);

        // Check for FCData or Levels or pak files
        File fcData = new File(folder, "FCData");
        File levels = new File(folder, "Levels");
        boolean hasFcData = fcData.exists() && fcData.isDirectory();
        boolean hasLevels = levels.exists() && levels.isDirectory();

        if (hasFcData || hasLevels) {
            tvGamePathStatus.setText(R.string.status_files_found);
            tvGamePathStatus.setTextColor(getColor(R.color.status_green));
        } else {
            tvGamePathStatus.setText(R.string.status_files_missing);
            tvGamePathStatus.setTextColor(getColor(R.color.status_orange));
        }
    }

    private void showFolderPickerDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_folder_picker, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();

        TextView tvCurrent = dialogView.findViewById(R.id.tv_picker_current_path);
        TextView tvStatus = dialogView.findViewById(R.id.tv_picker_status);
        ListView lvItems = dialogView.findViewById(R.id.lv_folder_items);
        Button btnSelect = dialogView.findViewById(R.id.btn_picker_select);
        Button btnCancel = dialogView.findViewById(R.id.btn_picker_cancel);

        // Start in currently entered folder if valid, otherwise external storage
        String currentInput = editGamePath.getText().toString().trim();
        File initialDir = new File(currentInput);
        if (!initialDir.exists() || !initialDir.isDirectory()) {
            initialDir = Environment.getExternalStorageDirectory();
        }
        final File[] currentDir = new File[]{initialDir};
        final List<String> itemNames = new ArrayList<>();
        final List<File> itemFiles = new ArrayList<>();

        Runnable updateList = () -> {
            tvCurrent.setText(currentDir[0].getAbsolutePath());
            itemNames.clear();
            itemFiles.clear();

            if (currentDir[0].getParentFile() != null) {
                itemNames.add(getString(R.string.folder_level_up));
                itemFiles.add(currentDir[0].getParentFile());
            }

            File[] files = currentDir[0].listFiles(File::isDirectory);
            if (files != null) {
                Arrays.sort(files, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
                for (File f : files) {
                    itemNames.add(f.getName());
                    itemFiles.add(f);
                }
            }

            // Check if Far Cry files are present in the currently selected directory
            if (tvStatus != null) {
                File fcData = new File(currentDir[0], "FCData");
                File levels = new File(currentDir[0], "Levels");
                File[] paks = currentDir[0].listFiles((dir, name) -> name.toLowerCase().endsWith(".pak"));
                boolean hasFiles = (fcData.exists() && fcData.isDirectory()) ||
                                   (levels.exists() && levels.isDirectory()) ||
                                   (paks != null && paks.length > 0);
                if (hasFiles) {
                    tvStatus.setText(R.string.status_fc_found_in_folder);
                    tvStatus.setTextColor(Color.rgb(80, 220, 100));
                } else {
                    tvStatus.setText(R.string.status_fc_select_folder_hint);
                    tvStatus.setTextColor(Color.rgb(180, 190, 200));
                }
            }

            ArrayAdapter<String> adapter = new ArrayAdapter<String>(this,
                    android.R.layout.simple_list_item_1, itemNames) {
                @NonNull
                @Override
                public View getView(int position, View convertView, @NonNull ViewGroup parent) {
                    TextView tv = (TextView) super.getView(position, convertView, parent);
                    tv.setTextColor(Color.WHITE);
                    tv.setTextSize(14f);
                    int padH = (int) (12 * getResources().getDisplayMetrics().density);
                    int padV = (int) (8 * getResources().getDisplayMetrics().density);
                    tv.setPadding(padH, padV, padH, padV);
                    String name = itemNames.get(position);
                    if (name.startsWith("..")) {
                        tv.setText("📁  " + name);
                        tv.setTextColor(Color.rgb(120, 200, 255));
                    } else {
                        tv.setText("📁  " + name);
                    }
                    return tv;
                }
            };
            lvItems.setAdapter(adapter);
        };

        updateList.run();

        lvItems.setOnItemClickListener((parent, view, position, id) -> {
            if (position < itemFiles.size()) {
                currentDir[0] = itemFiles.get(position);
                updateList.run();
            }
        });

        btnSelect.setOnClickListener(v -> {
            editGamePath.setText(currentDir[0].getAbsolutePath());
            validateGamePath(currentDir[0].getAbsolutePath());
            dialog.dismiss();
        });

        btnCancel.setOnClickListener(v -> dialog.dismiss());

        dialog.setOnShowListener(d -> {
            Window window = dialog.getWindow();
            if (window != null) {
                DisplayMetrics dm = getResources().getDisplayMetrics();
                int width = (int) (dm.widthPixels * 0.90);
                int height = (int) (dm.heightPixels * 0.90);
                window.setLayout(width, height);
            }
        });

        dialog.show();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_CODE_ZIP && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                installDriverZip(uri);
            }
        }
    }

    private void installDriverZip(Uri uri) {
        String fileName = "Turnip_Driver.zip";
        try {
            android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (nameIndex != -1) {
                    fileName = cursor.getString(nameIndex);
                }
                cursor.close();
            }
        } catch (Exception ignored) {}

        Toast.makeText(this, getString(R.string.toast_installing_driver, fileName), Toast.LENGTH_SHORT).show();

        try {
            DriverInfo installed = TurnipDriverManager.installDriverFromUri(this, uri, fileName);
            TurnipDriverManager.setSelectedDriver(this, installed.getId());
            setupDriverSpinner();

            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_driver_installed)
                    .setMessage(getString(R.string.dialog_driver_installed_msg, installed.getName()))
                    .setPositiveButton(R.string.ok, null)
                    .show();
        } catch (Exception e) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_driver_install_failed_title)
                    .setMessage(getString(R.string.dialog_driver_install_failed) + e.getMessage())
                    .setPositiveButton(R.string.ok, null)
                    .show();
        }
    }

    private boolean checkGameFilesExist(String path) {
        if (path == null || path.isEmpty()) return false;
        File folder = new File(path);
        if (!folder.exists() || !folder.isDirectory()) return false;
        File fcData = new File(folder, "FCData");
        File levels = new File(folder, "Levels");
        if ((fcData.exists() && fcData.isDirectory()) || (levels.exists() && levels.isDirectory())) {
            return true;
        }
        File[] paks = folder.listFiles((dir, name) -> name.toLowerCase().endsWith(".pak"));
        return paks != null && paks.length > 0;
    }

    private void launchGame() {
        savePreferences();
        String gamePath = editGamePath.getText().toString().trim();
        File f = new File(gamePath);
        if (!f.exists()) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_folder_not_found_title)
                    .setMessage(getString(R.string.dialog_folder_not_found_msg, gamePath))
                    .setPositiveButton(R.string.ok, null)
                    .show();
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_storage_permission_title)
                    .setMessage(R.string.dialog_storage_permission_msg)
                    .setPositiveButton(R.string.btn_permission_allow, (dialog, which) -> {
                        try {
                            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                            intent.setData(Uri.parse("package:" + getPackageName()));
                            startActivity(intent);
                        } catch (Exception e) {
                            startGameActivity();
                        }
                    })
                    .setNegativeButton(R.string.btn_permission_continue, (dialog, which) -> startGameActivity())
                    .show();
            return;
        }

        if (!checkGameFilesExist(gamePath)) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_game_files_missing_title)
                    .setMessage(getString(R.string.dialog_game_files_missing_msg, gamePath))
                    .setPositiveButton(R.string.btn_launch, (dialog, which) -> startGameActivity())
                    .setNegativeButton(R.string.cancel, null)
                    .show();
            return;
        }

        startGameActivity();
    }

    private void startGameActivity() {
        String gamePath = editGamePath.getText().toString().trim();
        cleanSystemConfigFiles(gamePath);
        Intent intent = new Intent(this, GameActivity.class);
        startActivity(intent);
    }

    /**
     * Automatically remove system.cfg and systemcfgoverride.cfg files because they
     * cause crashes and break graphics on Android mobile environment.
     */
    public static void cleanSystemConfigFiles(String gamePath) {
        if (gamePath == null || gamePath.trim().isEmpty()) return;
        try {
            File dir = new File(gamePath);
            if (!dir.exists() || !dir.isDirectory()) return;

            // Delete system.cfg and systemcfgoverride.cfg in root
            File[] files = dir.listFiles();
            if (files != null) {
                for (File f : files) {
                    if (f.isFile()) {
                        String name = f.getName().toLowerCase();
                        if (name.equals("system.cfg") || name.equals("systemcfgoverride.cfg")) {
                            boolean deleted = f.delete();
                            android.util.Log.i("FarCry", "Cleaned problematic config: " + f.getAbsolutePath() + " (deleted=" + deleted + ")");
                        }
                    }
                }
            }

            // Also check Profiles directory
            File profilesDir = new File(dir, "Profiles");
            if (profilesDir.exists() && profilesDir.isDirectory()) {
                cleanProfilesFolder(profilesDir);
            }
        } catch (Throwable t) {
            android.util.Log.w("FarCry", "Failed to clean system configs: " + t.getMessage());
        }
    }

    private static void cleanProfilesFolder(File folder) {
        File[] files = folder.listFiles();
        if (files == null) return;
        for (File f : files) {
            if (f.isDirectory()) {
                cleanProfilesFolder(f);
            } else if (f.isFile()) {
                String name = f.getName().toLowerCase();
                if (name.endsWith("system.cfg") || name.equals("systemcfgoverride.cfg")) {
                    boolean deleted = f.delete();
                    android.util.Log.i("FarCry", "Cleaned profile config: " + f.getAbsolutePath() + " (deleted=" + deleted + ")");
                }
            }
        }
    }

    private void requestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivity(intent);
                } catch (Exception ignored) {}
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQ_CODE_STORAGE_PERMISSION);
            }
        }
    }
}
