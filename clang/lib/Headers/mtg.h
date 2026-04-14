/*===---- mtg.h - MtG target builtins ---------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __MTG_H
#define __MTG_H

#ifndef __mtg__
#error "mtg.h is only valid for the MtG target"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Write a value to the machine's Output stream. Lowered directly to the MtG
 * `Output` instruction by MtGTargetLowering::LowerCall. */
void __mtg_output(int __val);

/* Read one value from the machine's A/B input stream. Lowered directly to
 * the MtG `AInput` / `BInput` instructions. */
int __mtg_input_a(void);
int __mtg_input_b(void);

#ifdef __cplusplus
}
#endif

#endif /* __MTG_H */
