#![doc(html_root_url = "http://docs.rs/const-default-derive/0.2.0")]

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro2::{Ident, Literal, Span, TokenStream as TokenStream2};
use proc_macro_crate::{crate_name, FoundCrate};
use quote::{quote, quote_spanned};
use syn::{spanned::Spanned, Error};

/// Derives an implementation for the [`ConstDefaultUnion`] trait.
///
/// # Note
///
/// Currently only works with `union` inputs.
///
/// # Example
///
/// ## Union
///
/// ```
/// const CAT_SIZE: usize = 0x16;
/// use const_default_union_derive::ConstDefaultUnion;
/// use const_default::ConstDefault;
/// #[derive(ConstDefaultUnion)]
/// pub union Animal {
///     Dog: u8,
///     Cat: [u8; CAT_SIZE],
/// }
///
/// assert_eq!(
///     unsafe { <Animal as ConstDefault>::DEFAULT.Cat },
///     [0; CAT_SIZE],
/// )
/// ```
#[proc_macro_derive(ConstDefaultUnion, attributes(const_default))]
pub fn derive(input: TokenStream) -> TokenStream {
    match derive_default(input.into()) {
        Ok(output) => output.into(),
        Err(error) => error.to_compile_error().into(),
    }
}

/// Implements the derive of `#[derive(ConstDefaultUnion)]` for union types.
fn derive_default(input: TokenStream2) -> Result<TokenStream2, syn::Error> {
    let crate_ident = query_crate_ident()?;
    let input = syn::parse2::<syn::DeriveInput>(input)?;
    let ident = input.ident;
    let data_union = match input.data {
        syn::Data::Union(data_union) => data_union,
        _ => {
            return Err(Error::new(
                Span::call_site(),
                "ConstDefaultUnion derive only works on union types",
            ))
        }
    };
    let default_impl = generate_default_impl_union(&crate_ident, &data_union)?;
    let mut generics = input.generics;
    generate_default_impl_where_bounds(&crate_ident, &data_union, &mut generics)?;
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

/// Generates the `ConstDefaultUnion` implementation for `union` input types.
///
/// # Note
///
/// Zero initialize the first field in the union, rest is handled automatically.
fn generate_default_impl_union(
    crate_ident: &TokenStream2,
    data_union: &syn::DataUnion,
) -> Result<TokenStream2, syn::Error> {
    let fields_impl = data_union
        .fields
        .named
        .first()
        .map(|field| {
            let field_span = field.span();
            let field_type = &field.ty;
            let field_pos = Literal::usize_unsuffixed(0);
            let field_ident = field
                .ident
                .as_ref()
                .map(|ident| quote_spanned!(field_span=> #ident))
                .unwrap_or_else(|| quote_spanned!(field_span=> #field_pos));
            quote_spanned!(field_span=>
                #field_ident: <#field_type as #crate_ident::ConstDefault>::DEFAULT
            )
        })
        .unwrap();
    Ok(quote! {
        Self {
            #fields_impl
        }
    })
}

/// Generates `ConstDefault` where bounds for all fields of the input.
fn generate_default_impl_where_bounds(
    crate_ident: &TokenStream2,
    data_union: &syn::DataUnion,
    generics: &mut syn::Generics,
) -> Result<(), syn::Error> {
    let where_clause = generics.make_where_clause();
    if let Some(field) = &data_union.fields.named.first() {
        let field_type = &field.ty;
        where_clause.predicates.push(syn::parse_quote!(
            #field_type: #crate_ident::ConstDefault
        ))
    }
    Ok(())
}
