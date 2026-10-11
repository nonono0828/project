package cn.hajimi.spider001;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.os.Build;
import android.provider.Settings;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

@CapacitorPlugin(name = "WifiSettings")
public class WifiSettingsPlugin extends Plugin {
    @PluginMethod
    public void open(PluginCall call) {
        Activity activity = getActivity();
        if (activity == null) {
            call.reject("当前无法打开 Wi-Fi 设置");
            return;
        }

        activity.runOnUiThread(() -> openSettings(activity, call));
    }

    private void openSettings(Activity activity, PluginCall call) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            try {
                activity.startActivity(new Intent(Settings.Panel.ACTION_WIFI));
                resolveOpened(call, "panel");
                return;
            } catch (ActivityNotFoundException | SecurityException ignored) {
                // Continue with the full Wi-Fi settings page.
            }
        }

        try {
            activity.startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS));
            resolveOpened(call, "wifi_settings");
            return;
        } catch (ActivityNotFoundException | SecurityException ignored) {
            // Some vendor ROMs do not expose the dedicated Wi-Fi settings action.
        }

        try {
            activity.startActivity(new Intent(Settings.ACTION_WIRELESS_SETTINGS));
            resolveOpened(call, "wireless_settings");
        } catch (ActivityNotFoundException | SecurityException exception) {
            call.reject("此设备无法打开 Wi-Fi 设置", null, exception);
        }
    }

    private void resolveOpened(PluginCall call, String mode) {
        JSObject result = new JSObject();
        result.put("opened", true);
        result.put("mode", mode);
        call.resolve(result);
    }
}
