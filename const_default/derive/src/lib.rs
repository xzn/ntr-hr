#![doc(html_root_url = "http://docs.rs/const-default-derive/0.2.0")]

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro2::{Ident, Literal, Span, TokenStream as TokenStream2};
use proc_macro_crate::{crate_name, FoundCrate};
use quote::{quote, quote_spanned};
use syn::{spanned::Spanned, Error};

/// Derives an implementation for the [`ConstDefault`] trait.
///
/// # Note
///
/// Currently only works with `struct` inputs.
///
/// # Example
///
/// ## Struct
///
/// ```
/// # use const_default::ConstDefault;
/// #[derive(ConstDefault)]
/// # #[derive(Debug, PartialEq)]
/// pub struct Color {
///     r: u8,
///     g: u8,
///     b: u8,
/// }
///
/// assert_eq!(
///     <Color as ConstDefault>::DEFAULT,
///     Color { r: 0, g: 0, b: 0 },
/// )
/// ```
///
/// ## Tuple Struct
///
/// ```
/// # use const_default::ConstDefault;
/// #[derive(ConstDefault)]
/// # #[derive(Debug, PartialEq)]
/// pub struct Vec3(f32, f32, f32);
///
/// assert_eq!(
///     <Vec3 as ConstDefault>::DEFAULT,
///     Vec3(0.0, 0.0, 0.0),
/// )
/// ```
#[proc_macro_derive(ConstDefault, attributes(const_default))]
pub fn derive(input: TokenStream) -> TokenStream {
    match derive_default(input.into()) {
        Ok(output) => output.into(),
        Err(error) => error.to_compile_error().into(),
    }
}

/// Implements the derive of `#[derive(ConstDefault)]` for struct types.
fn derive_default(input: TokenStream2) -> Result<TokenStream2, syn::Error> {
    let crate_ident = query_crate_ident()?;
    let input = syn::parse2::<syn::DeriveInput>(input)?;
    let ident = input.ident;
    let data_struct = match input.data {
        syn::Data::Struct(data_struct) => data_struct,
        _ => {
            return Err(Error::new(
                Span::call_site(),
                "ConstDefault derive only works on struct types",
            ))
        }
    };
    let default_impl =
        generate_default_impl_struct(&crate_ident, &data_struct)?;
    let mut generics = input.generics;
    generate_default_impl_where_bounds(
        &crate_ident,
        &data_struct,
        &mut generics,
    )?;
    let (impl_generics, ty_generics, where_clause) = generics.split_for_impl();
    Ok(quote! {
        impl #impl_generics #crate_ident::ConstDefault for #ident #ty_generics #where_clause {
            const DEFAULT: Self = #default_impl;
        }
    })
}

/// Queries the dependencies for the derive root crate name and returns the identifier.
///
/// # Note
///
/// This allows to use crate aliases in `Cargo.toml` files of dependencies.
fn query_crate_ident() -> Result<TokenStream2, syn::Error> {
    let query = crate_name("const-default").map_err(|error| {
        Error::new(
            Span::call_site(),
            format!(
                "could not find root crate for ConstDefault derive: {}",
                error
            ),
        )
    })?;
    match query {
        FoundCrate::Itself => Ok(quote! { crate }),
        FoundCrate::Name(name) => {
            let ident = Ident::new(&name, Span::call_site());
            Ok(quote! { ::#ident })
        }
    }
}

/// Generates the `ConstDefault` implementation for `struct` input types.
///
/// # Note
///
/// The generated code abuses the fact that in Rust struct types can always
/// be represented with braces and either using identifiers for fields or
/// raw number literals in case of tuple-structs.
///
/// For example `struct Foo(u32)` can be represented as `Foo { 0: 42 }`.
fn generate_default_impl_struct(
    crate_ident: &TokenStream2,
    data_struct: &syn::DataStruct,
) -> Result<TokenStream2, syn::Error> {
    let fields_impl =
        data_struct.fields.iter().enumerate().map(|(n, field)| {
            let field_span = field.span();
            let field_type = &field.ty;
            let field_pos = Literal::usize_unsuffixed(n);
            let field_ident = field
                .ident
                .as_ref()
                .map(|ident| quote_spanned!(field_span=> #ident))
                .unwrap_or_else(|| quote_spanned!(field_span=> #field_pos));
            quote_spanned!(field_span=>
                #field_ident: <#field_type as #crate_ident::ConstDefault>::DEFAULT
            )
        });
    Ok(quote! {
        Self {
            #( #fields_impl ),*
        }
    })
}

/// Generates `ConstDefault` where bounds for all fields of the input.
fn generate_default_impl_where_bounds(
    crate_ident: &TokenStream2,
    data_struct: &syn::DataStruct,
    generics: &mut syn::Generics,
) -> Result<(), syn::Error> {
    let where_clause = generics.make_where_clause();
    for field in &data_struct.fields {
        let field_type = &field.ty;
        where_clause.predicates.push(syn::parse_quote!(
            #field_type: #crate_ident::ConstDefault
        ))
    }
    Ok(())
}
