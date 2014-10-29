// Copyright 2013 The Rust Project Developers. See the COPYRIGHT
// file at the top-level directory of this distribution and at
// http://rust-lang.org/COPYRIGHT.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![feature(struct_variant)]

enum Foo { C { a: int, b: int } }
struct C { a: int, b: int }         //~ ERROR error: duplicate definition of type or module `C`

struct A { x: int }
enum Bar { A { x: int } }           //~ ERROR error: duplicate definition of type or module `A`

fn main() {}