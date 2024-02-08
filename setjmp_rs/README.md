# setjmp / longjmp

**WARNING: this crate is experimental and even careful use is likely
undefined behavior.**

This crate exposes four C standard library functions to Rust:

```
pub fn setjmp(env: *mut jmp_buf) -> c_int;
pub fn sigsetjmp(env: *mut sigjmp_buf, savesigs: c_int) -> c_int;
pub fn longjmp(env: *mut jmp_buf, val: c_int) -> c_void;
pub fn siglongjmp(env: *mut sigjmp_buf, val: c_int) -> c_void;
```
as well as the ``jmp_buf`` and ``sigjmp_buf`` types needed to use them.

See
[``setjmp(3)``](http://man7.org/linux/man-pages/man3/setjmp.3.html)
for details and caveats.

Also see [RFC #2625](https://github.com/rust-lang/rfcs/issues/2625).

# Motivation

To interact better with C code that may use
``setjmp()``/``longjmp()``:

* If C code calls rust code, and the rust code calls C code, and a
  ``longjmp()`` happens, you may want the rust code to catch, the
  ``longjmp()``, transform it into a panic (to safely unwind), then
  [``catch_unwind()``](https://doc.rust-lang.org/std/panic/fn.catch_unwind.html),
  then turn it back into a ``longjmp()`` to return to someplace in the
  C code (the last place that called ``setjmp()``).
* If rust code calls C code, the rust code might want to catch a
  ``longjmp()`` from the C code and handle it somehow.
* Rust code might want to ``longjmp()`` to return control to C code.

It is possible to use ``setjmp()``/``longjmp()`` just for managing
control flow in rust (without interacting with C), but that would be
quite dangerous and has no clear use case.

# Why is the "setjmp" crate necessary?

Ordinarily, using a C function from rust is easy: you just declare
it. Why go to the trouble of making a special crate?

1. Document the numerous problems and caveats, as done in this
   document.
1. Explore the problem space enough that the rust language team might
   feel comfortable defining the behavior (in at least some narrow
   circumstances).
1. Provide tests to see if something breaks in an obvious way.
1. Handle some platform issues:
   1. The ``jmp_buf`` and ``sigjmp_buf`` types are not trivial and are
      best defined using bindgen on the system's ``<setjmp.h>``
      header.
   1. libc implementations often use macros to change the symbols
      actually referenced; and this is done differently on different
      platforms. For instance, instead of ``sigsetjmp`` the actual
      libc symbol might be ``__sigsetjmp``, and there may be a macro
      to rewrite the ``sigsetjmp()`` call into ``__sigsetjmp()``.

# Usage

The invocation of setjmp can appear only in the following contexts
(see
[this](https://github.com/rust-lang/rfcs/issues/2625#issuecomment-455896576)
comment):

* the entire controlling expression of ``match``, e.g. ``match setjmp(env) { ... }``.
* ``if setjmp(env) $integer_relational_operator $integer_constant_expression { ... }``
* the entire expression of an expression statement: ``setjmp(env);``

See tests for examples.

# Problems

Beyond the many challenges using ``setjmp/longjmp`` in C, there are
**additional** challenges using them from rust.

1. The behavior of these functions is defined in terms of C, and
   therefore any application to rust is by analogy (until rust defines
   the behavior).
1. Rust has destructors, and C does not. Any ``longjmp()`` must be
   careful to not jump over any stack frame that owns references to
   variables that have destructors.
1. Rust doesn't have a concept of functions that return multiple
   times, like ``fork()`` or ``setjmp()``, so it's easy to imagine
   that rust might generate incorrect code around such a function.
1. Rust uses LLVM during compilation, which needs to be made aware of
   functions that return multiple times by using the
   [``returns_twice``](https://llvm.org/docs/LangRef.html#function-attributes)
   attribute; but rust has no way to propagate that attribute to
   LLVM. Without this attribute, it's possible that LLVM itself will
   generate incorrect code (See
   [this](https://github.com/rust-lang/rfcs/issues/2625#issuecomment-460849462)
   comment).
1. Jumping can interrupt well-bracketed control flow, circumventing
   guarantees about what code has run.
1. Jumping can return control to a point before a value was moved,
   thereby allowing use-after-drop bugs.
1. Jumping deallocates variables without destructing them (it doesn't
   merely leak them).

# Alternatives

Given these problems, you should seriously consider alternatives.

One alternative is to use C wrappers when entering a rust stack frame
from C or a C stack frame from rust. The wrappers could turn special
return values from rust into a C ``longjmp()`` if necessary, or catch
a ``longjmp()`` from C and turn it into a rust ``panic!()``,
respectively. This is not always practical, however, so sometimes
calling ``setjmp()``/``longjmp()`` from rust is still the best
solution.

# Recommendations

* Mark any function calling ``setjmp()`` with ``#[inline(never)]`` to
  reduce the chances for misoptimizations.
* Code between a ``setjmp()`` returns ``0`` and possible ``longjmp()``
  should be as minimal as possible. Typically, this might just be
  saving/setting global variables and calling a C FFI function (which
  might ``longjmp()``). This code should avoid allocating memory on
  the heap, using types that implement the ``Drop`` trait, or code
  that is complex enough that it might trigger misoptimizations.
* Code before a ``longjmp()`` or any parent stack frames should also
  be minimal. Typically, this would be just enough code to retrieve a
  return value from a callee, or catch a panic with
  [``catch_unwind()``](https://doc.rust-lang.org/std/panic/fn.catch_unwind.html).
  This code should avoid allocating memory on the heap, using types
  that implement the ``Drop`` trait, or code that is complex enough
  that it might trigger misoptimizations.
