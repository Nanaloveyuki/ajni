package dev.nanaloveyuki.ajni.fixture;

import dev.nanaloveyuki.ajni.AjniCallback;

public final class Fixture {
    public static int staticNumber = 7;
    public static boolean booleanValue = true;
    public static byte byteValue = -128;
    public static char charValue = '\ud800';
    public static short shortValue = -32768;
    public static long longValue = 9007199254740993L;
    public static float floatValue = 1.25f;
    public static double doubleValue = -3.5;
    public static AjniCallback counter;
    public static AjniCallback inner;
    public static AjniCallback closing;
    public static AjniCallback foreign;
    public static AjniCallback throwing;
    public static AjniCallback recovering;
    public static AjniCallback invalidThrowable;
    public static final RuntimeException identity = new RuntimeException("original throwable");
    public static final String text = new String(new char[] {
        'A', 0, '\u4e2d', '\u6587', '\ud83d', '\ude00', '\ud800', 'Z', '\udc00'
    });
    public int number;
    public String label;

    public static final class 中文 {
        public int 数值;

        public 中文(int value) {
            数值 = value;
        }

        public int 增加(int value) {
            数值 += value;
            return 数值;
        }
    }

    public static 中文 回声(中文 value) {
        return value;
    }

    public Fixture(int number, String label) {
        this.number = number;
        this.label = label;
    }

    public int add(int amount) {
        number += amount;
        return number;
    }

    public static int twice(int value) {
        return value * 2;
    }

    public static long mixed(boolean z, byte b, char c, short s, int i,
                             long j, float f, double d) {
        if (!z || b != -128 || c != '\ud800' || s != -32768 || i != 42
                || j != 9007199254740993L || f != 1.25f || d != -3.5) {
            throw new AssertionError("Primitive argument conversion");
        }
        return j;
    }

    public String echo(String value) {
        if (!text.equals(value)) {
            throw new AssertionError("UTF-16 code units changed");
        }
        return value;
    }

    public static int nested(AjniCallback callback, int value) {
        return ((Integer) callback.invoke(value)) + 1;
    }

    public static void throwIdentity() {
        throw identity;
    }

    public static int closeDuringCall() {
        closing.close();
        return 17;
    }

    public static int closeBusySession() {
        try {
            Smoke.close();
            throw new AssertionError("Closing active session succeeded");
        } catch (IllegalStateException expected) {
            return 19;
        }
    }
}
