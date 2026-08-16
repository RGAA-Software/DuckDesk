package com.pixels.yun.client.impl;

import java.util.ArrayList;
import java.util.List;

public class ThunderCallbacks {

    // callbacks
    public interface OnFrameChangedCallback {
        void onFrameChanged(int width, int height);
    }

    public interface OnCursorInfoCallback {
        void onCursorInfo(CursorInfo info);
    }

    // server configuration
    public static class MonitorResolution {

    }

    public static class MonitorInfo {
        public String name;
        public List<MonitorResolution> resolutions;
    }

    public static class ServerConfiguration {
        public List<MonitorInfo> monitors = new ArrayList<>();
        public String capturingMonitorName;
        public int fps;
    }

    public interface OnServerConfigurationCallback {
        void onConfiguration(ServerConfiguration config);
    }

}
