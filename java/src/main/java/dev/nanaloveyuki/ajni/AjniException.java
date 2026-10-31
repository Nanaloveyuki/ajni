package dev.nanaloveyuki.ajni;

/** A native or MoonBit JNI failure without an original Java throwable. */
public final class AjniException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public AjniException(String message) {
        super(message);
    }
}
