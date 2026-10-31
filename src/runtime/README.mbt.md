# Checked JNI runtime

`Nanaloveyuki/ajni/runtime` calls Java methods, constructors and fields from
MoonBit native code and exposes synchronous Java-to-MoonBit callbacks. A host
must initialize MoonBit and install a session before `Session::current()`.
The descriptor-only root package remains independent of JNI and JDK headers.

## Calls and values

`Session.find_class` accepts a root-package `JniClass`. Methods accept
`JniMethod`; fields and primitive arrays accept `JniType`. A constructor takes
an `ArrayView[JniType]` and creates its void-returning descriptor internally.

```moonbit nocheck
let session = @runtime.Session::current()
let class = session.find_class(@ajni.JniClass::parse("java/lang/StringBuilder"))
let ctor = class.lookup_constructor([])
let object = ctor.new([])
let append = class.instance_method(
  "append",
  @ajni.JniMethod::new(
    [@ajni.JniType::object(@ajni.JniClass::parse("java/lang/String"))],
    return_type=@ajni.JniType::object(@ajni.JniClass::parse("java/lang/StringBuilder")),
  ),
)
let text = session.new_string("Hello\u0000JNI")
ignore(append.call([@runtime.Object(Some(text))], receiver=object))
```

Pass `receiver=` for instance methods and fields, and omit it for static
members. `JavaClass.object()` exposes its checked class object when Java needs
a `Class` argument. A lookup keeps its declaring class alive. Argument count,
value tags, receiver type, reference assignability and member kind are checked
before JNI dispatch; method calls use JNI's non-varargs `A` entry points.
Member reference types resolve with the declaring class's defining loader, even
when that class is imported from a different loader. Callback descriptors use
the session loader. Admitted operations retain JNI snapshots so reentrant code
may close a public handle without invalidating the suspended operation.

`JavaValue` distinguishes `Boolean(Bool)`, signed `Byte(Int)`, unsigned UTF-16
`Char(Int)`, signed `Short(Int)`, `Int(Int)`, `Long(Int64)`, `Float(Float)`,
`Double(Double)`, `Object(JavaObject?)` and `Void`. Byte, Char and Short payloads
are range-checked. `Object(None)` is Java null, not failure. Void is a return
value only. `new_string` and `to_string_value` preserve UTF-16 code units,
including NUL and unpaired surrogates; `to_string_value` reads an existing Java
String and does not invoke an object's `toString` method.

## Arrays and resources

`session.new_primitive_array(element_type, length)` creates a primitive array;
`component_class.new_object_array(length, initial=object)` creates a reference
array, defaulting to null elements. Arrays offer `array_length`, `get_element`,
`set_element`, `read_region(start, length)` and `write_region(start, values)`.
Regions copy their contents without exposing pinned Java memory. Negative or
out-of-range regions and incompatible elements raise `RuntimeError`.

All object handles own global references. Ordinary MoonBit values may retain
them across host calls. `close` is idempotent; aliases share the close state.
Closing a session invalidates every derived handle. A session does not own or
destroy the host JVM. Finalizers release omitted closes on the owner thread;
explicit close is preferable for predictable JNI resource use.

Sessions, handles and callbacks belong to the host's owner OS thread. Only
that thread may enter MoonBit or use managed values. This is not a Send/Sync
promise. Session close during an active entry raises `Busy` without closing
resources. Wrong-thread JNI entries are rejected by the native host gate
before touching MoonBit-managed values.

## Callbacks and exceptions

`session.callback(descriptor, handler)` takes a
`(ArrayView[JavaValue]) -> JavaValue raise RuntimeError` handler. Pass the
result's `object()` to Java's `AjniCallback` companion. The descriptor controls
primitive boxing and reference checks. The companion does not automatically
implement arbitrary Java interfaces; use a Java/Kotlin adapter for those.

The registry owns the handler closure until close. Each admitted invocation
holds its own closure reference, so closing a callback during its invocation
only blocks future calls. Java may close a token on a foreign thread without
entering MoonBit; the owner drains pending closure releases. Late invocation
raises a closed-resource exception. Synchronous owner-thread reentry is
supported; foreign-thread callback invocation fails instead of waiting.

Native failures are consumed immediately after each operation. A Java
exception becomes `JavaThrown(original_throwable)` with the pending JNI
exception cleared, so catching it permits subsequent calls. If it escapes a
callback, Java receives the same Throwable, not a reconstructed message.
A manually constructed `JavaThrown` must contain an open Java Throwable;
non-Throwable payloads are rejected as `IllegalArgumentException` at the Java
callback boundary, without leaving a pending exception behind.
Other `RuntimeError` variants cross the callback boundary as explicit error
payloads and become host exceptions; no typed MoonBit error crosses C ABI.
Panic and out-of-memory aborts are not recoverable Java exceptions.

## Host build and low-level seam

Only the native backend implements JNI. Other backends explicitly raise
`UnsupportedTarget`. Native builds without `jni.h` also report
`UnsupportedTarget`; they do not simulate JNI success. The actual host supplies
JDK or Android NDK include directories. This package does not hardcode a JDK
location or link a JVM library.

Prefer `Nanaloveyuki/ajni/unsafe` for explicitly borrowed JNI integration.
MoonBit has no friend-package visibility, so runtime deliberately exports a
small `unsafe_*` bridge for that package: import object/class, export a local
reference, borrow VM/Env, and register a native method. These functions are
not safe-pointer validation APIs. Every supplied Env and reference must be
live, belong to the same JVM and owner thread, and obey JNI local-frame
lifetimes. Exported local references are caller-owned; borrowed VM/Env pointers
must not outlive their host or entry. Registered function pointers must have
the exact platform JNI ABI and remain alive while registered. No opaque native
runtime record is exposed. `internal_check_status` exists solely to share error
mapping with host packages.
