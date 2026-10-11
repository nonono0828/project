package cn.hajimi.spider001;

import android.os.Bundle;
import android.webkit.WebView;

import androidx.activity.OnBackPressedCallback;

import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    private static final String LOCAL_PORTAL_HOST = "192.168.4.1";
    private static final String RELEASE_CONTROLS_SCRIPT =
        "(function(){" +
        "window.dispatchEvent(new CustomEvent('robot-app-release-controls'));" +
        "window.dispatchEvent(new Event('blur'));" +
        "})();";
    private boolean pausedSinceLastResume = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        registerPlugin(WifiSettingsPlugin.class);
        super.onCreate(savedInstanceState);
        getOnBackPressedDispatcher().addCallback(
            this,
            new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    handleAppBack();
                }
            }
        );
    }

    private WebView appWebView() {
        return getBridge() == null ? null : getBridge().getWebView();
    }

    private void releaseMotionControls() {
        WebView webView = appWebView();
        if (webView == null) return;
        webView.evaluateJavascript(RELEASE_CONTROLS_SCRIPT, null);
    }

    @Override
    public void onPause() {
        releaseMotionControls();
        pausedSinceLastResume = true;
        super.onPause();
    }

    @Override
    public void onResume() {
        super.onResume();
        if (!pausedSinceLastResume) return;
        pausedSinceLastResume = false;
        WebView webView = appWebView();
        if (webView != null) {
            webView.evaluateJavascript(
                "window.dispatchEvent(new CustomEvent('robot-app-resume'));",
                null
            );
        }
    }

    @Override
    public void onStop() {
        releaseMotionControls();
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        if (!hasFocus) releaseMotionControls();
        super.onWindowFocusChanged(hasFocus);
    }

    private void handleAppBack() {
        WebView webView = appWebView();
        if (webView == null) {
            finish();
            return;
        }

        releaseMotionControls();
        String currentUrl = webView.getUrl();

        // Recovery path in case the remote portal ever navigates the top frame.
        if (currentUrl != null && currentUrl.contains("://" + LOCAL_PORTAL_HOST)) {
            webView.loadUrl(getBridge().getLocalUrl());
            return;
        }

        webView.evaluateJavascript(
            "window.__ROBOT_APP_SCREEN || ''",
            screen -> {
                if (
                    "\"login\"".equals(screen)
                    || "\"launcher\"".equals(screen)
                    || "\"auth-loading\"".equals(screen)
                    || "\"\"".equals(screen)
                    || "null".equals(screen)
                ) {
                    finish();
                } else {
                    webView.evaluateJavascript(
                        "window.dispatchEvent(new CustomEvent('robot-app-back',{cancelable:true}));",
                        null
                    );
                }
            }
        );
    }
}
