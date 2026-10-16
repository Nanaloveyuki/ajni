package dev.nanaloveyuki.ajni.fixture;

import java.util.concurrent.atomic.AtomicReference;

public final class Smoke {
    static native void initialize(ClassLoader loader);
    static native void reopen(ClassLoader loader);
    static native int run(int stage);
    static native void close();
    static native int entries();

    private interface Action {
        void run() throws Throwable;
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void rejected(Action action, String message) throws Throwable {
        try {
            action.run();
        } catch (IllegalStateException expected) {
            return;
        }
        throw new AssertionError(message);
    }

    private static void invalid(Action action, String message) throws Throwable {
        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            return;
        }
        throw new AssertionError(message);
    }

    private static void foreignThread(Action action) throws Throwable {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread thread = new Thread(() -> {
            try {
                action.run();
            } catch (Throwable error) {
                failure.set(error);
            }
        }, "ajni-foreign-owner");
        thread.start();
        thread.join();
        if (failure.get() != null) throw failure.get();
    }

    public static void main(String[] arguments) throws Throwable {
        require(arguments.length == 1, "Pass the native fixture library path");
        System.load(arguments[0]);
        require(entries() == 0, "JNI_OnLoad entered managed code");
        rejected(() -> run(0), "Entry before initialize succeeded");
        require(entries() == 0, "Uninitialized entry touched managed code");
        initialize(Smoke.class.getClassLoader());
        require(entries() == 0, "Initialize ran exported fixture");
        require(run(0) == 0, "MoonBit setup failed");
        require(((Integer) Fixture.counter.invoke(3)) == 7, "Captured state / nested callback first call");
        require(((Integer) Fixture.counter.invoke(2)) == 11, "Captured state / nested callback second call");
        int beforeInvalid = entries();
        invalid(() -> Fixture.counter.invoke(true), "Boolean accepted for int callback");
        invalid(() -> Fixture.counter.invoke((Object) null), "Null accepted for primitive callback");
        invalid(() -> Fixture.counter.invoke(), "Wrong callback arity accepted");
        invalid(() -> Fixture.counter.invoke((Object[]) null), "Null argument array accepted");
        require(entries() == beforeInvalid, "Invalid callback arguments entered MoonBit");
        int beforeForeign = entries();
        foreignThread(() -> {
            rejected(() -> run(1), "Foreign native entry succeeded");
            rejected(() -> Fixture.counter.invoke(100), "Foreign callback succeeded");
            rejected(Smoke::close, "Foreign session close succeeded");
            require(entries() == beforeForeign, "Wrong thread entered managed code");
            Fixture.foreign.close();
            Fixture.foreign.close();
            require(entries() == beforeForeign, "Foreign close entered managed callback");
        });
        rejected(() -> Fixture.foreign.invoke(1), "Foreign-closed callback remained callable");
        require(run(1) == 0, "Owner tombstone drain failed");
        require(((Integer) Fixture.counter.invoke(1)) == 13, "Foreign rejection mutated captured state");
        try {
            Fixture.throwing.invoke();
            throw new AssertionError("Java throwable was swallowed");
        } catch (RuntimeException error) {
            require(error == Fixture.identity, "Throwable identity changed returning to Java");
        }
        require(((Integer) Fixture.recovering.invoke()) == 73, "Handled Java throwable leaked from callback");
        invalid(() -> Fixture.invalidThrowable.invoke(), "Non-Throwable JavaThrown value accepted");
        require(((Integer) Fixture.recovering.invoke()) == 73, "Handled throwable poisoned later callback");
        require(Fixture.staticNumber == 31, "Exception recovery changed fixture state");
        require(((Integer) Fixture.closing.invoke()) == 17, "Active callback lost closure during close");
        rejected(() -> Fixture.closing.invoke(), "Closed callback token remained callable");
        Fixture.counter.close();
        Fixture.counter.close();
        rejected(() -> Fixture.counter.invoke(1), "Closed callback accepted stale token");
        Fixture.throwing.close();
        Fixture.recovering.close();
        Fixture.invalidThrowable.close();
        close();
        rejected(() -> run(1), "Closed session accepted managed entry");
        rejected(() -> Fixture.inner.invoke(1), "Session close left callback live");
        Fixture.inner.close();
        Fixture.foreign.close();
        reopen(Smoke.class.getClassLoader());
        require(run(2) == 0, "Old generation handles survived close/reopen");
        rejected(() -> Fixture.inner.invoke(1), "Old callback token revived after reopen");
        close();
        System.out.println("ajni checked JVM smoke passed");
    }
}
