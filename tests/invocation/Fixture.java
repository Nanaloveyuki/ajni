package dev.nanaloveyuki.ajni.invocation;

import dev.nanaloveyuki.ajni.AjniCallback;
import java.net.URL;
import java.net.URLClassLoader;

public final class Fixture {
    private static final RuntimeException IDENTITY = new RuntimeException("invocation identity");
    public int number;

    public Fixture(int number) {
        this.number = number;
    }

    public int add(int value) {
        number += value;
        return number;
    }

    public static int invoke(AjniCallback callback, int value) {
        return ((Integer) callback.invoke(value)).intValue();
    }

    public static int invokeFromWorker(AjniCallback callback) throws InterruptedException {
        final boolean[] rejected = {false};
        Thread worker = new Thread(() -> {
            try {
                callback.invoke(99);
            } catch (IllegalStateException expected) {
                rejected[0] = true;
            }
        }, "ajni-invocation-foreign");
        worker.start();
        worker.join(5000);
        if (worker.isAlive()) {
            throw new AssertionError("Foreign callback blocked instead of rejecting");
        }
        return rejected[0] ? 1 : 0;
    }

    public static void throwIdentity() {
        throw IDENTITY;
    }

    public static int verifyIdentity(AjniCallback callback) {
        try {
            callback.invoke();
            return 0;
        } catch (RuntimeException thrown) {
            return thrown == IDENTITY ? 1 : 0;
        }
    }

    public static Class<?> newPluginClass() throws ClassNotFoundException {
        IsolatedLoader loader = new IsolatedLoader();
        return Class.forName(Plugin.class.getName(), false, loader);
    }

    public static Object pluginParameter(Class<?> plugin) throws ReflectiveOperationException {
        Class<?> parameter = Class.forName(PluginParam.class.getName(), true, plugin.getClassLoader());
        return parameter.getDeclaredConstructor().newInstance();
    }

    public static void armParameterLoad(Class<?> plugin, AjniCallback callback) {
        ((IsolatedLoader) plugin.getClassLoader()).onParameterLoad = callback;
    }

    public static final class Plugin {
        public static int accept(PluginParam parameter) {
            return parameter.value;
        }
    }

    public static final class PluginParam {
        public int value = 42;
    }

    private static final class IsolatedLoader extends URLClassLoader {
        private AjniCallback onParameterLoad;

        IsolatedLoader() {
            super(new URL[] {Fixture.class.getProtectionDomain().getCodeSource().getLocation()},
                  Fixture.class.getClassLoader());
        }

        @Override
        protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
            if (!name.equals(Plugin.class.getName()) && !name.equals(PluginParam.class.getName())) {
                return super.loadClass(name, resolve);
            }
            synchronized (getClassLoadingLock(name)) {
                Class<?> loaded = findLoadedClass(name);
                if (loaded == null) {
                    if (name.equals(PluginParam.class.getName()) && onParameterLoad != null) {
                        AjniCallback callback = onParameterLoad;
                        onParameterLoad = null;
                        callback.invoke();
                    }
                    loaded = findClass(name);
                }
                if (resolve) {
                    resolveClass(loaded);
                }
                return loaded;
            }
        }
    }

    public static native int nativeAdd(int value);
}
