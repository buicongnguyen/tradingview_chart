package com.buicongnguyen.tradingviewchart;

import android.content.res.Resources;

/** System-bar dimensions converted to Qt's density-independent coordinates. */
public final class AndroidUiMetrics {
    private AndroidUiMetrics() {}

    public static float statusBarHeightDp() {
        return dimensionDp("status_bar_height");
    }

    public static float navigationBarHeightDp() {
        return dimensionDp("navigation_bar_height");
    }

    private static float dimensionDp(String resourceName) {
        Resources resources = Resources.getSystem();
        int identifier = resources.getIdentifier(
            resourceName,
            "dimen",
            "android");
        if (identifier == 0) {
            return 0.0f;
        }
        float density = resources.getDisplayMetrics().density;
        return density > 0.0f
            ? resources.getDimensionPixelSize(identifier) / density
            : 0.0f;
    }
}
