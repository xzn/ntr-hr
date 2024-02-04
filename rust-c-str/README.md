# c_str! macro for Rust

![Maintenance Status](https://img.shields.io/badge/maintenance-as--is-yellowgreen.svg)

This macro-only crate provides the `c_str!` macro to facilitate creation
of C-compatible string values from Rust string literals.

## Legacy code

This repository also contains source code for crate `c_string`,
which is no longer maintained. It provided some utility types to
facilitate working with C-compatible strings in Rust, but most of
them were poorly designed or do not fit well with the evolution
of the language. The topmost commit for that crate is available
under branch `c-string`.

## License

Licensed under either of

 * Apache License, Version 2.0
   ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license
   ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

## Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.
