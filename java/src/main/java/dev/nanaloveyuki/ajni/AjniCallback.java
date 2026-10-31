package dev.nanaloveyuki.ajni;

/** An owner-thread MoonBit closure exposed to Java. Close releases its registration. */
public final class AjniCallback implements AutoCloseable {
    private final long token;

    /** Called by the native runtime; tokens are process-local and never reused. */
    public AjniCallback(long token) {
        if (token <= 0) {
            throw new IllegalArgumentException("Invalid callback token");
        }
        this.token = token;
    }

    public Object invoke(Object... arguments) {
        return dispatch(token, arguments);
    }

    /** May be called from any thread; invocation remains restricted to the session owner. */
    @Override
    public void close() {
        close(token);
    }

    private static native Object dispatch(long token, Object[] arguments);
    private static native void close(long token);
}
