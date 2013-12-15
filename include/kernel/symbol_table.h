/*
 * Copyright (C) 2013 F4OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef KERNEL_SYMBOL_TABLE_H_INCLUDED
#define KERNEL_SYMBOL_TABLE_H_INCLUDED

#include <stdint.h>

struct symbol_table {
    uintptr_t addr;
    const char *name;
};

/*
 * Symbol table containing all function names add addresses.
 * Generated at link time.  An empty, weak, symbol table is defined in
 * symbol_table_array.c.
 */
extern const struct symbol_table symbol_table[];
extern const unsigned int symbol_table_length;

/*
 * Lookup function name in symbol table
 *
 * @param addr  Address of instruction within function
 * @returns name of function containing addr, NULL if not found
 */
const char *symbol_table_name(uintptr_t addr);

#endif
