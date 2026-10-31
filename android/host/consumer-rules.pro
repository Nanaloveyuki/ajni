# RegisterNatives and C upcalls use this class and these exact JVM names.
# Keep only JNI-reachable members; ordinary Kotlin callers remain R8-visible.
-keep class dev.nanaloveyuki.ajni.host.NativeBridge {
    private static native void nativeInitialize(android.content.Context, long);
    private static native void nativeShutdown();
    private static native void nativeLifecycle(int);
    private static native void nativeSurfaceCreated(android.view.Surface, int, int);
    private static native void nativeSurfaceChanged(int, int);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeOnUiTask(int);
    private static native void nativeStartWorker();
    private static native java.lang.String nativeEcho(java.lang.String);
    private static native dev.nanaloveyuki.ajni.AjniCallback nativeCreateIntCallback();
    private static native boolean nativeHandledObserverError();
    private static native void nativeWebViewEvent(int, long, java.lang.String, java.lang.String);
    private static boolean postUiCallbackForRuntime(int, long);
    private static boolean webViewCommandForRuntime(int, long, java.lang.String, java.lang.String, long);
    private static boolean webViewSetBoundsForRuntime(long, int, int, int, int, long);
}

-keep class dev.nanaloveyuki.ajni.AjniCallback {
    public <init>(long);
    private static native java.lang.Object dispatch(long, java.lang.Object[]);
    private static native void close(long);
}
-keep class dev.nanaloveyuki.ajni.AjniException {
    public <init>(java.lang.String);
}
