package com.buicongnguyen.tradingviewchart;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Small HTTPS adapter for the Qt/C++ market-data client.
 *
 * Android's platform TLS stack receives OS security updates and uses the
 * device trust store, avoiding a second OpenSSL runtime inside the APK.
 */
public final class AndroidHttpClient {
    private static final ExecutorService EXECUTOR =
        Executors.newFixedThreadPool(2, runnable -> {
            Thread thread = new Thread(runnable, "TradingViewChart-HTTPS");
            thread.setDaemon(true);
            return thread;
        });

    private AndroidHttpClient() {}

    public static void get(
        long requestToken,
        String urlText,
        String userAgent,
        int maximumBytes) {
        EXECUTOR.execute(() -> {
            HttpURLConnection connection = null;
            try {
                URL url = new URL(urlText);
                if (!"https".equalsIgnoreCase(url.getProtocol())) {
                    throw new IllegalArgumentException("Only HTTPS URLs are allowed");
                }

                connection = (HttpURLConnection) url.openConnection();
                connection.setConnectTimeout(15_000);
                connection.setReadTimeout(15_000);
                connection.setInstanceFollowRedirects(false);
                connection.setRequestMethod("GET");
                connection.setRequestProperty("Accept", "application/json");
                connection.setRequestProperty("User-Agent", userAgent);

                int status = connection.getResponseCode();
                InputStream stream = status >= 200 && status < 300
                    ? connection.getInputStream()
                    : connection.getErrorStream();
                byte[] payload = stream == null
                    ? new byte[0]
                    : readBounded(stream, maximumBytes);
                nativeResult(requestToken, status, payload, "");
            } catch (Exception exception) {
                nativeResult(
                    requestToken,
                    0,
                    new byte[0],
                    safeMessage(exception));
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
        });
    }

    private static byte[] readBounded(InputStream stream, int maximumBytes)
        throws Exception {
        try (InputStream input = stream;
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[16 * 1024];
            int total = 0;
            int count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > maximumBytes) {
                    throw new IllegalStateException(
                        "Response exceeded " + maximumBytes + " bytes");
                }
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static String safeMessage(Exception exception) {
        String message = exception.getMessage();
        if (message != null) {
            message = message.replaceAll(
                "(?i)(apikey=)[^&\\s]+",
                "$1<redacted>");
        }
        return exception.getClass().getSimpleName() +
            (message == null || message.isEmpty() ? "" : ": " + message);
    }

    private static native void nativeResult(
        long requestToken,
        int httpStatus,
        byte[] payload,
        String error);
}
