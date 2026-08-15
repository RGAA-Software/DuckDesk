export function getBrowserInfo() {
    const info: Record<string, any> = {};
    
    // 浏览器 + 系统基本信息
    info.userAgent = navigator.userAgent;
    info.language = navigator.language;
    info.languages = navigator.languages;
    info.platform = navigator.platform;
    info.cookieEnabled = navigator.cookieEnabled;
    
    // 检测浏览器名字和版本（通过 UA）
    info.browserName = detectBrowserName(navigator.userAgent);
    info.browserVersion = detectBrowserVersion(navigator.userAgent);
    
    // 屏幕信息
    info.screenWidth = window.screen.width;
    info.screenHeight = window.screen.height;
    info.windowInnerWidth = window.innerWidth;
    info.windowInnerHeight = window.innerHeight;
    info.colorDepth = window.screen.colorDepth;
    
    // 网络相关
    info.online = navigator.onLine;
    const connection = (navigator as any).connection || {};
    info.networkType = connection.effectiveType || connection.type || 'unknown';
    info.downlink = connection.downlink || 'unknown';
    info.rtt = connection.rtt || 'unknown';
    
    // 硬件信息
    info.hardwareConcurrency = navigator.hardwareConcurrency || 'unknown';
    info.deviceMemory = (navigator as any).deviceMemory || 'unknown';
    
    // 显卡信息（WebGL）
    try {
        const canvas = document.createElement('canvas');
        const gl = canvas.getContext('webgl');
        if (gl) {
            const debugInfo = gl.getExtension('WEBGL_debug_renderer_info');
            if (debugInfo) {
                info.gpuVendor = gl.getParameter(debugInfo.UNMASKED_VENDOR_WEBGL);
                info.gpuRenderer = gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL);
            }
        }
    } catch (e) {
        info.gpuVendor = 'unknown';
        info.gpuRenderer = 'unknown';
    }
    
    // 时区
    info.timezone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    
    return info;
}

/** 检测浏览器名字 */
function detectBrowserName(ua: string): string {
    ua = ua.toLowerCase();
    if (ua.includes('edg/')) return 'Microsoft Edge';
    if (ua.includes('chrome') && !ua.includes('chromium') && !ua.includes('edg/')) return 'Google Chrome';
    if (ua.includes('firefox')) return 'Mozilla Firefox';
    if (ua.includes('safari') && !ua.includes('chrome')) return 'Apple Safari';
    if (ua.includes('opr/') || ua.includes('opera')) return 'Opera';
    return 'Unknown';
}

/** 检测浏览器版本 */
function detectBrowserVersion(ua: string): string {
    const match = ua.match(/(chrome|firefox|safari|opr|edg)\/([\d.]+)/i);
    return match ? match[2] : 'Unknown';
}
