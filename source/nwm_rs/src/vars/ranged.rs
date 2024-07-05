use super::*;

#[derive(Copy, Clone, ConstDefault, ConstParamTy, Eq, PartialEq)]
pub struct IRanged<const BEG: u32_, const END: u32_>(u32_);

pub struct IRangedIter<const BEG: u32_, const END: u32_>(u32_);

impl<const BEG: u32_, const END: u32_> Iterator for IRangedIter<BEG, END> {
    type Item = IRanged<BEG, END>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0 > END {
            None
        } else {
            let r = unsafe { IRanged::<BEG, END>::init_unchecked(self.0) };
            self.0 += 1;
            Some(r)
        }
    }
}

pub struct IRangedIterN<const BEG: u32_, const END: u32_>(u32_, u32_);

impl<const BEG: u32_, const END: u32_> Iterator for IRangedIterN<BEG, END> {
    type Item = IRanged<BEG, END>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0 >= self.1 {
            None
        } else {
            let r = unsafe { IRanged::<BEG, END>::init_unchecked(self.0) };
            self.0 += 1;
            Some(r)
        }
    }
}

#[allow(dead_code)]
pub struct IRangedIterW<const BEG: u32_, const END: u32_>(u32_, IRanged<BEG, END>, u32_);

impl<const BEG: u32_, const END: u32_> Iterator for IRangedIterW<BEG, END> {
    type Item = IRanged<BEG, END>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0 == self.1 .0 {
            None
        } else {
            let r = unsafe { IRanged::<BEG, END>::init_unchecked(self.0) };
            self.0 += 1;
            if self.0 >= self.2 {
                self.0 = BEG
            }
            Some(r)
        }
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END> {
    pub fn all() -> IRangedIter<BEG, END> {
        IRangedIter::<BEG, END>(BEG)
    }

    pub fn up_to<const B2: u32_, const E2: u32_>(n: &IRanged<B2, E2>) -> IRangedIterN<BEG, END>
    where
        [(); { 1 - B2 } as usize]:,
        [(); { END + 1 - E2 } as usize]:,
    {
        Self::init().from_up_to(n)
    }

    pub fn from_up_to<const B2: u32_, const E2: u32_>(
        &self,
        n: &IRanged<B2, E2>,
    ) -> IRangedIterN<BEG, END>
    where
        [(); { 1 - B2 } as usize]:,
        [(); { END + 1 - E2 } as usize]:,
    {
        IRangedIterN::<BEG, END>(self.0, n.0)
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END> {
    pub const fn init() -> Self {
        Self(BEG)
    }

    pub fn init_val(v: u32_) -> Self {
        let mut r = Self::init();
        r.set(v);
        r
    }

    pub unsafe fn init_unchecked(v: u32_) -> Self {
        Self(v)
    }

    pub const fn get(&self) -> u32_ {
        self.0
    }

    pub fn set(&mut self, v: u32_) {
        if v < BEG {
            self.0 = BEG
        } else if v > END {
            self.0 = END
        } else {
            self.0 = v
        }
    }

    pub fn prev_wrapped(&mut self) {
        if self.0 == BEG {
            self.0 = END
        } else {
            self.0 -= 1
        }
    }

    pub fn next_wrapped(&mut self) {
        if self.0 == END {
            self.0 = BEG
        } else {
            self.0 += 1
        }
    }

    pub fn next_wrapped_n<const B2: u32_, const E2: u32_>(&mut self, n: &IRanged<B2, E2>)
    where
        [(); { 1 - B2 } as usize]:,
        [(); { END + 1 - E2 } as usize]:,
    {
        self.0 += 1;
        if self.0 >= n.0 {
            self.0 = BEG
        }
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END>
where
    [(); { 0 - BEG } as usize]:,
    [(); { END - 1 } as usize]:,
{
    pub fn from_bool(v: bool) -> Self {
        unsafe { Self::init_unchecked(v as u32_) }
    }
}

pub type Ranged<const N: u32_> = IRanged<0, { N - 1 }>;

pub struct RangedArraySlice<'a, T, const N: u32_>(pub &'a mut RangedArray<T, N>)
where
    [(); N as usize]:;

#[derive(ConstDefault)]
pub struct RangedArray<T, const N: u32_>([T; N as usize])
where
    [(); N as usize]:;

impl<T, const N: u32_> Clone for RangedArray<T, N>
where
    [(); N as usize]:,
    T: Clone,
{
    fn clone_from(&mut self, source: &Self) {
        *self = source.clone()
    }

    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

impl<T, const N: u32_> Copy for RangedArray<T, N>
where
    [(); N as usize]:,
    T: Copy,
{
}

impl<T, const N: u32_> RangedArray<T, N>
where
    [(); N as usize]:,
{
    pub fn as_mut_ptr(&mut self) -> *mut T {
        self.0.as_mut_ptr()
    }

    pub fn get(&self, i: &Ranged<N>) -> &T
    where
        [(); { N - 1 } as usize]:,
    {
        unsafe { self.0.get_unchecked(i.0 as usize) }
    }

    pub fn get_mut<const N2: u32_>(&mut self, i: &Ranged<N2>) -> &mut T
    where
        [(); { N - N2 } as usize]:,
        [(); { N2 - 1 } as usize]:,
    {
        unsafe { self.0.get_unchecked_mut(i.0 as usize) }
    }

    pub fn get_b(&self, b: bool) -> &T
    where
        [(); { N - 2 } as usize]:,
    {
        unsafe { self.0.get_unchecked(b as usize) }
    }

    pub fn get_b_mut(&mut self, b: bool) -> &mut T
    where
        [(); { N - 2 } as usize]:,
    {
        unsafe { self.0.get_unchecked_mut(b as usize) }
    }

    pub fn split_at_mut<const I: u32_>(
        &mut self,
    ) -> (RangedArraySlice<T, I>, RangedArraySlice<T, { N - I }>)
    where
        [(); I as usize]:,
        [(); { N - I } as usize]:,
    {
        unsafe {
            let a = self.0.as_mut_ptr();
            let b = a.add(I as usize);
            (
                RangedArraySlice::<T, I>(mem::transmute(a)),
                RangedArraySlice::<T, { N - I }>(mem::transmute(b)),
            )
        }
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut T> + '_ {
        self.0.iter_mut()
    }
}
